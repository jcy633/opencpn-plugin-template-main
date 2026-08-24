//===================================================================
// Auto-split from ncdfoverlayfactory.cpp
//===================================================================

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"
#include "shader/ncdf_shader.h"

extern void ncdfLog(const char* format, ...);

#ifndef PI
#define PI 3.14159265
#endif

#include "IsoLine2.h"

//===================================================================
// Mask-aware cubic B-spline IIR prefilter
// Processes each row/column's contiguous ocean segments independently.
// Land boundaries reset the IIR state, preventing land values from
// contaminating ocean coefficients near coastlines.
//===================================================================
static void BicubicSplinePrefilterMasked(float* dst, const float* src,
                                          const unsigned char* mask, int w, int h) {
    const float z = -0.26794919243112270f;
    // inv = 1/(1-z): correct for 2D separable B-spline prefilter
    // causal steady state = K/(1-z), anti-causal produces same
    // reconstruction = (2/3)² * c * inv² * (1-z)² = (4/9) * K/(1-z)² * 1/(1-z)² * (1-z)² = K
    const float inv = 1.0f / (1.0f - z);

    for (int k = 0; k < w * h; k++) dst[k] = src[k];


    // Horizontal pass
    for (int j = 0; j < h; j++) {
        float* row = dst + j * w;
        const unsigned char* mrow = mask + j * w;
        bool allValid = true;
        for (int i = 0; i < w; i++) { if (!mrow[i]) { allValid = false; break; } }

        if (allValid) {
            row[0] *= inv;
            for (int i = 1; i < w; i++) row[i] += z * row[i-1];
            // Anti-causal: NO inv scaling (already applied at causal boundary)
            for (int i = w-2; i >= 0; i--) row[i] += z * row[i+1];
        } else {
            int segStart = -1;
            for (int i = 0; i <= w; i++) {
                bool valid = (i < w) && mrow[i];
                if (valid && segStart < 0) segStart = i;
                if ((!valid || i == w) && segStart >= 0) {
                    int segEnd = i - 1;
                    if (segEnd == segStart) {
                        row[segStart] *= inv;
                    } else {
                        row[segStart] *= inv;
                        for (int k = segStart + 1; k <= segEnd; k++) row[k] += z * row[k-1];
                        // Anti-causal: NO inv scaling
                        for (int k = segEnd - 1; k >= segStart; k--) row[k] += z * row[k+1];
                    }
                    segStart = -1;
                }
            }
        }
    }

    // Vertical pass
    for (int i = 0; i < w; i++) {
        bool allValid = true;
        for (int j = 0; j < h; j++) { if (!mask[j * w + i]) { allValid = false; break; } }

        if (allValid) {
            dst[i] *= inv;
            for (int j = 1; j < h; j++) dst[j*w+i] += z * dst[(j-1)*w+i];
            for (int j = h-2; j >= 0; j--) dst[j*w+i] += z * dst[(j+1)*w+i];
        } else {
            int segStart = -1;
            for (int j = 0; j <= h; j++) {
                bool valid = (j < h) && mask[j * w + i];
                if (valid && segStart < 0) segStart = j;
                if ((!valid || j == h) && segStart >= 0) {
                    int segEnd = j - 1;
                    if (segEnd == segStart) {
                        dst[segStart*w+i] *= inv;
                    } else {
                        dst[segStart*w+i] *= inv;
                        for (int k = segStart + 1; k <= segEnd; k++) dst[k*w+i] += z * dst[(k-1)*w+i];
                        for (int k = segEnd - 1; k >= segStart; k--) dst[k*w+i] += z * dst[(k+1)*w+i];
                    }
                    segStart = -1;
                }
            }
        }
    }
}

//===================================================================
// Wrap-aware B-spline prefilter for global data
// Extends data by N columns on each side (wrapping longitude),
// runs prefilter on extended data, then extracts original range.
// This ensures coefficients are continuous across the dateline.
//===================================================================
static const int WRAP_EXT = 8;  // IIR convergence: |z|^8 ≈ 0.0002

static void BicubicSplinePrefilterWrapAware(float* dst, const float* src,
                                             const unsigned char* mask,
                                             int w, int h, bool isGlobal) {
    if (!isGlobal) {
        // Non-global: just run the normal prefilter
        BicubicSplinePrefilterMasked(dst, src, mask, w, h);
        return;
    }

    // Global data: extend columns for wrap-around
    int extW = w + 2 * WRAP_EXT;
    int totalPixels = extW * h;

    float* extSrc = (float*)calloc(totalPixels, sizeof(float));
    float* extDst = (float*)calloc(totalPixels, sizeof(float));
    unsigned char* extMask = (unsigned char*)calloc(totalPixels, 1);
    if (!extSrc || !extDst || !extMask) {
        if (extSrc) free(extSrc);
        if (extDst) free(extDst);
        if (extMask) free(extMask);
        // Fallback: run without wrap
        BicubicSplinePrefilterMasked(dst, src, mask, w, h);
        return;
    }

    // Fill extended buffer with wrap-around
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < extW; i++) {
            // Map extended column back to original with wrapping
            int origI = ((i - WRAP_EXT) % w + w) % w;
            int extOff = j * extW + i;
            int origOff = j * w + origI;
            extSrc[extOff] = src[origOff];
            extMask[extOff] = mask[origOff];
        }
    }

    // Run prefilter on extended data
    BicubicSplinePrefilterMasked(extDst, extSrc, extMask, extW, h);

    // Extract original range (columns WRAP_EXT to WRAP_EXT+w-1)
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            dst[j * w + i] = extDst[j * extW + WRAP_EXT + i];
        }
    }

    free(extSrc);
    free(extDst);
    free(extMask);
}

//===================================================================
// Shared scalar overlay rendering (SeaTemp / Salinity)
//===================================================================
bool ncdfOverlayFactory::RenderScalarColorMap(
    PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
    bool useShader, ScalarOverlayState &st,
    ColorFunc colorFunc, float lutMin, float lutMax,
    bool (*hasData)(MainDialog&),
    void (*fillGrid)(MainDialog&, std::unique_ptr<double*[]>&, int, int),
    bool smoothColors, bool sharpen, bool anisoDiffusion,
    double** animatedGrid)
{
    if (!gui) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    if (animatedGrid) {
        // Animation: use provided grid directly
        if (*st.p_cachedGrid) {
            for (int j = 0; j < *st.p_cachedNj; j++) delete[] (*st.p_cachedGrid)[j];
            st.p_cachedGrid->reset();
        }
        *st.p_cachedNj = nj; *st.p_cachedNi = ni;
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = animatedGrid[jj][ii];
                if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                    if (v < dMin) dMin = v; if (v > dMax) dMax = v;
                }
            }
        *st.p_cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        *st.p_cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
        *st.p_needsRebuild = true;
    } else if (!hasData(*gui)) {
        return false;
    } else {
        if (*st.p_needsRebuild || !*st.p_cachedGrid) {
            if (*st.p_cachedGrid) { for (int j = 0; j < *st.p_cachedNj; j++) delete[] (*st.p_cachedGrid)[j]; }
            st.p_cachedGrid->reset();
            *st.p_cachedNj = nj; *st.p_cachedNi = ni;
            fillGrid(*gui, *st.p_cachedGrid, nj, ni);
            if (anisoDiffusion) {
                double** ad = BuildAnisoDiffusedGrid(st.p_cachedGrid->get(), nj, ni);
                if (ad) { for (int j = 0; j < nj; j++) delete[] (*st.p_cachedGrid)[j]; st.p_cachedGrid->reset(ad); }
            }
            if (sharpen) {
                double** sh = BuildSharpenedGrid(st.p_cachedGrid->get(), nj, ni);
                if (sh) { for (int j = 0; j < nj; j++) delete[] (*st.p_cachedGrid)[j]; st.p_cachedGrid->reset(sh); }
            }
            if (*st.p_cachedGrid) {
                double dMin = 1e30, dMax = -1e30;
                for (int jj = 0; jj < nj; jj++)
                    for (int ii = 0; ii < ni; ii++) {
                        double v = (*st.p_cachedGrid)[jj][ii];
                        if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                            if (v < dMin) dMin = v; if (v > dMax) dMax = v;
                        }
                    }
                *st.p_cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
                *st.p_cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
            }
        }
    }

    RenderSettings settings;
    settings.useShader = useShader;
    settings.smoothColors = smoothColors;
    settings.vectorMode = false;
    settings.uGrid = NULL;
    settings.vGrid = NULL;
    if (*st.p_hasTexture) {
        settings.dataMin = *st.p_cachedCoefMin;
        settings.dataMax = *st.p_cachedCoefMax;
        settings.physMin = *st.p_cachedPhysMin;
        settings.physMax = *st.p_cachedPhysMax;
    } else {
        settings.dataMin = *st.p_cachedDataMin;
        settings.dataMax = *st.p_cachedDataMax;
        settings.physMin = 0.0f;
        settings.physMax = 1.0f;
    }
    settings.dataMinV = 0.0f; settings.dataMaxV = 1.0f;
    settings.physMinV = 0.0f; settings.physMaxV = 1.0f;
    settings.lutMin = lutMin;
    settings.lutMax = lutMax;

    double** renderGrid = animatedGrid ? animatedGrid : st.p_cachedGrid->get();
    RenderGridOverlay(vp, renderGrid, colorFunc, settings,
        *st.p_glTexture, *st.p_hasTexture, *st.p_needsRebuild,
        st.p_dataDim, st.p_glDim, *st.p_lutID, *st.p_hasLUT,
        *st.p_uploadBuf, *st.p_uploadBufSize);

    if (*st.p_hasTexture) {
        *st.p_cachedCoefMin = settings.dataMin;
        *st.p_cachedCoefMax = settings.dataMax;
        *st.p_cachedPhysMin = settings.physMin;
        *st.p_cachedPhysMax = settings.physMax;
    }
    return true;
}

//===================================================================
// Shared utilities
//===================================================================

// Sea temperature color map (-2°C to 32°C, purple→red, smoothstep)
wxColour ncdfOverlayFactory::InterpolateStops(const double stops[][4], int nStops, double val, bool smooth)
{
    if (val >= stops[nStops - 1][0])
        return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
    if (val <= stops[0][0])
        return wxColour((unsigned char)stops[0][1], (unsigned char)stops[0][2], (unsigned char)stops[0][3]);

    for (int i = 1; i < nStops; i++) {
        if (val <= stops[i][0]) {
            double range = stops[i][0] - stops[i-1][0];
            double t = (range > 0) ? (val - stops[i-1][0]) / range : 0;
            if (smooth) t = t * t * (3.0 - 2.0 * t);
            unsigned char r = (unsigned char)(stops[i-1][1] + t * (stops[i][1] - stops[i-1][1]));
            unsigned char g = (unsigned char)(stops[i-1][2] + t * (stops[i][2] - stops[i-1][2]));
            unsigned char b = (unsigned char)(stops[i-1][3] + t * (stops[i][3] - stops[i-1][3]));
            return wxColour(r, g, b);
        }
    }
    return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
}

//===================================================================
// Texture build context — shared state for helper functions
//===================================================================
struct TexBuildContext {
    MainDialog *gui;
    int ni, nj;
    int tw, th;
    int borderH;
    bool repeat;
    unsigned char *texData;
    double tlon, tlat, blon, blat;
};

//===================================================================
// (a) Vector mode coefficient texture fill
//===================================================================
static void FillVectorCoeffTex(TexBuildContext &ctx,
                               ncdfOverlayFactory::RenderSettings &settings)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    int borderH = ctx.borderH;
    unsigned char *texData = ctx.texData;
    bool repeat = ctx.repeat;

    double dMin = 1e30, dMax = -1e30;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            double u = settings.uGrid[j][i], v = settings.vGrid[j][i];
            if (u != ncdf_NOTDEF && v != ncdf_NOTDEF && isfinite(u) && isfinite(v)) {
                double spd = sqrt(u*u + v*v);
                if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
            }
        }
    if (dMax <= dMin) { dMin = 0; dMax = 1; }
    settings.dataMin = (float)dMin;
    settings.dataMax = (float)dMax;

    float* rawU = (float*)calloc(nj * ni, sizeof(float));
    float* rawV = (float*)calloc(nj * ni, sizeof(float));
    float* coeffU = (float*)calloc(nj * ni, sizeof(float));
    float* coeffV = (float*)calloc(nj * ni, sizeof(float));
    unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);

    if (rawU && rawV && coeffU && coeffV && validMask) {
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                double u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || !isfinite(u) || !isfinite(v)) {
                    rawU[j*ni+i] = 0; rawV[j*ni+i] = 0;
                    validMask[j*ni+i] = 0;
                } else {
                    rawU[j*ni+i] = (float)u;
                    rawV[j*ni+i] = (float)v;
                    validMask[j*ni+i] = 1;
                }
            }
        // Mask-aware B-spline prefilter with dateline wrap
        BicubicSplinePrefilterWrapAware(coeffU, rawU, validMask, ni, nj, repeat);
        BicubicSplinePrefilterWrapAware(coeffV, rawV, validMask, ni, nj, repeat);

        // Compute independent coefMin/coefMax for U and V
        float coefMinU = 1e30f, coefMaxU = -1e30f;
        float coefMinV = 1e30f, coefMaxV = -1e30f;
        for (int k = 0; k < nj * ni; k++) {
            if (!validMask[k]) continue;
            if (coeffU[k] < coefMinU) coefMinU = coeffU[k];
            if (coeffU[k] > coefMaxU) coefMaxU = coeffU[k];
            if (coeffV[k] < coefMinV) coefMinV = coeffV[k];
            if (coeffV[k] > coefMaxV) coefMaxV = coeffV[k];
        }
        if (coefMaxU <= coefMinU) { coefMinU = 0; coefMaxU = 1; }
        if (coefMaxV <= coefMinV) { coefMinV = 0; coefMaxV = 1; }
        float coefRangeU = coefMaxU - coefMinU;
        float coefRangeV = coefMaxV - coefMinV;
        if (coefRangeU < 1e-10f) coefRangeU = 1.0f;
        if (coefRangeV < 1e-10f) coefRangeV = 1.0f;

        const float bsplineScale = 2.5858f;
        settings.dataMin = coefMinU * bsplineScale;
        settings.dataMax = coefMaxU * bsplineScale;
        settings.dataMinV = coefMinV * bsplineScale;
        settings.dataMaxV = coefMaxV * bsplineScale;

        for (int j = 0; j < nj; j++) {
            int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
            for (int i = 0; i < ni; i++) {
                int x = i + borderH, y = texRow + 1;
                if (x >= tw - 1 || y >= th - 1) continue;
                int off = 4 * (y * tw + x);
                if (!validMask[j*ni+i]) {
                    texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 0;
                } else {
                    texData[off]   = (unsigned char)(fmin(fmax((coeffU[j*ni+i] - coefMinU) / coefRangeU, 0.0f), 1.0f) * 255.0f + 0.5f);
                    texData[off+1] = (unsigned char)(fmin(fmax((coeffV[j*ni+i] - coefMinV) / coefRangeV, 0.0f), 1.0f) * 255.0f + 0.5f);
                    texData[off+2] = 0; texData[off+3] = 255;
                }
            }
        }
    }
    if (rawU) free(rawU); if (rawV) free(rawV);
    if (coeffU) free(coeffU); if (coeffV) free(coeffV);
    if (validMask) free(validMask);
}

//===================================================================
// (b) Scalar mode coefficient texture fill
//===================================================================
static void FillScalarCoeffTex(TexBuildContext &ctx,
                               double **grid,
                               ncdfOverlayFactory::RenderSettings &settings)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    int borderH = ctx.borderH;
    unsigned char *texData = ctx.texData;
    bool repeat = ctx.repeat;

    // Build flat array for B-spline prefilter
    float* rawBuf = (float*)calloc(nj * ni, sizeof(float));
    float* coeffBuf = (float*)calloc(nj * ni, sizeof(float));
    unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);
    if (!rawBuf || !coeffBuf || !validMask) {
        if (rawBuf) free(rawBuf);
        if (coeffBuf) free(coeffBuf);
        if (validMask) free(validMask);
    } else {
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                double val = grid[j][i];
                if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) {
                    rawBuf[j*ni+i] = 0.0f;
                    validMask[j*ni+i] = 0;
                } else {
                    rawBuf[j*ni+i] = (float)val;
                    validMask[j*ni+i] = 1;
                }
            }
        float physMin = 1e30f, physMax = -1e30f;
        for (int k = 0; k < nj * ni; k++) {
            if (!validMask[k]) continue;
            if (rawBuf[k] < physMin) physMin = rawBuf[k];
            if (rawBuf[k] > physMax) physMax = rawBuf[k];
        }

        // Stage 2: Mask-aware B-spline prefilter with dateline wrap
        BicubicSplinePrefilterWrapAware(coeffBuf, rawBuf, validMask, ni, nj, repeat);

        // Stage 3: Coefficient range
        float coefMin = 1e30f, coefMax = -1e30f;
        for (int k = 0; k < nj * ni; k++) {
            if (!validMask[k]) continue;
            if (coeffBuf[k] < coefMin) coefMin = coeffBuf[k];
            if (coeffBuf[k] > coefMax) coefMax = coeffBuf[k];
        }
        if (coefMax <= coefMin) { coefMin = 0; coefMax = 1; }
        float coefRange = coefMax - coefMin;
        if (coefRange < 1e-10f) coefRange = 1.0f;

        const float bsplineScale = 2.5858f;  // (1-z)^4
        settings.dataMin = coefMin * bsplineScale;
        settings.dataMax = coefMax * bsplineScale;

        // R8 normalization and texture fill
        for (int j = 0; j < nj; j++) {
            int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
            for (int i = 0; i < ni; i++) {
                int x = i + borderH, y = texRow + 1;
                if (x >= tw - 1 || y >= th - 1) continue;
                int off = 4 * (y * tw + x);
                if (!validMask[j*ni+i]) {
                    texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 0;
                } else {
                    // R = B-spline coefficient (normalized)
                    float normalized = (coeffBuf[j*ni+i] - coefMin) / coefRange;
                    unsigned char q = (unsigned char)(fmin(fmax(normalized, 0.0f), 1.0f) * 255.0f + 0.5f);
                    texData[off] = q;
                    // G = physical value (normalized) for bilinear fallback
                    float physNorm = (physMax > physMin) ? (rawBuf[j*ni+i] - physMin) / (physMax - physMin) : 0.0f;
                    texData[off+1] = (unsigned char)(fmin(fmax(physNorm, 0.0f), 1.0f) * 255.0f + 0.5f);
                    texData[off+2] = 0; texData[off+3] = 255;
                }
            }
        }

        settings.physMin = physMin;
        settings.physMax = physMax;

        free(rawBuf); free(coeffBuf); free(validMask);
    }
}

//===================================================================
// (c) LUT texture creation
//===================================================================
static void BuildLUTTex(ncdfOverlayFactory::ColorFunc colorFunc,
                        ncdfOverlayFactory::RenderSettings &settings,
                        GLuint &lutID, bool &hasLUT)
{
    // Only rebuild when lutID doesn't exist (per-overlay, no static cache)
    if (!hasLUT || !lutID) {
        float lutRange = settings.lutMax - settings.lutMin;
        if (lutRange < 1e-10f) lutRange = 1.0f;
        unsigned char lutData[256 * 4];
        for (int k = 0; k < 256; k++) {
            double val = settings.lutMin + (k / 255.0) * lutRange;
            wxColour c = colorFunc(val);
            lutData[k*4] = c.Red(); lutData[k*4+1] = c.Green();
            lutData[k*4+2] = c.Blue(); lutData[k*4+3] = 255;
        }
        glGenTextures(1, &lutID);
        glBindTexture(GL_TEXTURE_2D, lutID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
        hasLUT = true;
    }
}

//===================================================================
// (d) Fixed-function path removed — shader only
//===================================================================

//===================================================================
// (e) Border/repeat handling
//===================================================================
static void ApplyTextureBorders(TexBuildContext &ctx)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    bool repeat = ctx.repeat;
    unsigned char *texData = ctx.texData;

    // Repeat mode: duplicate first column at end for seamless wrapping
    if (repeat) {
        for (int j = 0; j < nj; j++) {
            int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
            int y = texRow + 1;
            if (y >= th - 1) continue;
            memcpy(texData + 4 * (y * tw + ni), texData + 4 * (y * tw + 0), 4);
        }
    }

    // GRIB-style border: copy adjacent row/col, then set border alpha=0
    memcpy(texData, texData + 4 * tw, 4 * tw);
    memcpy(texData + 4 * tw * (th - 1), texData + 4 * tw * (th - 2), 4 * tw);
    for (int x = 0; x < tw; x++) {
        texData[4 * x + 3] = 0;
        texData[4 * ((th - 1) * tw + x) + 3] = 0;
    }
    if (!repeat) {
        for (int y = 0; y < th; y++) {
            memcpy(texData + 4 * y * tw, texData + 4 * (y * tw + 1), 4);
            memcpy(texData + 4 * (y * tw + tw - 1), texData + 4 * (y * tw + tw - 2), 4);
        }
        for (int y = 0; y < th; y++) {
            texData[4 * y * tw + 3] = 0;
            texData[4 * (y * tw + tw - 1) + 3] = 0;
        }
    }
}

//===================================================================
// (f) Texture upload (create or update)
//===================================================================
static void UploadCoeffTexture(GLuint &texID, bool &hasTex, int tw, int th,
                               bool repeat, int dataDim[2], int glDim[2],
                               unsigned char *texData)
{
    if (!hasTex) {
        // First time: create texture
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
        hasTex = true;
        dataDim[0] = tw; dataDim[1] = th;
        glDim[0] = tw;   glDim[1] = th;
    } else {
        // Update existing texture data (no GPU realloc, no OOM)
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, texData);
    }
}

//===================================================================
// (g) Physical texture creation (bilinear fallback)
//===================================================================
static void BuildPhysicalTex(TexBuildContext &ctx,
                             double **grid,
                             ncdfOverlayFactory::RenderSettings &settings,
                             bool repeat,
                             GLuint *physicalTexID, bool *hasPhysicalTex)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    int borderH = ctx.borderH;

    // === Create physical value texture for bilinear fallback ===
    if (physicalTexID && hasPhysicalTex && settings.useShader) {
        unsigned char *physData = (unsigned char*)malloc(tw * th * 4);
        if (physData) {
            memset(physData, 0, tw * th * 4);
            // Find physical data range for normalization
            double physMin = 1e30, physMax = -1e30;
            double uMin = 1e30, uMax = -1e30;
            double vMin = 1e30, vMax = -1e30;
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                        double u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                        if (u != ncdf_NOTDEF && v != ncdf_NOTDEF && isfinite(u) && isfinite(v)) {
                            if (u < uMin) uMin = u; if (u > uMax) uMax = u;
                            if (v < vMin) vMin = v; if (v > vMax) vMax = v;
                        }
                    } else if (grid[j]) {
                        double val = grid[j][i];
                        if (val != ncdf_NOTDEF && isfinite(val)) {
                            if (val < physMin) physMin = val;
                            if (val > physMax) physMax = val;
                        }
                    }
                }
            if (settings.vectorMode) {
                // For vector: independent u/v ranges
                physMin = uMin; physMax = uMax;
            }
            if (physMax <= physMin) { physMin = 0; physMax = 1; }
            double physRange = physMax - physMin;
            if (physRange < 1e-10) physRange = 1.0;
            // V range (vector mode only)
            double physMinV = vMin, physMaxV = vMax;
            if (physMaxV <= physMinV) { physMinV = 0; physMaxV = 1; }
            double physRangeV = physMaxV - physMinV;
            if (physRangeV < 1e-10) physRangeV = 1.0;

            // Fill physical texture with normalized raw values
            for (int j = 0; j < nj; j++) {
                int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                for (int i = 0; i < ni; i++) {
                    int x = i + borderH, y = texRow + 1;
                    if (x >= tw - 1 || y >= th - 1) continue;
                    int off = 4 * (y * tw + x);
                    if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                        double u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                        if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || !isfinite(u) || !isfinite(v)) {
                            physData[off] = 0; physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 0;
                        } else {
                            // Normalize u,v independently to [0,1]
                            physData[off]   = (unsigned char)(fmin(fmax((u - physMin) / physRange, 0.0), 1.0) * 255.0 + 0.5);
                            physData[off+1] = (unsigned char)(fmin(fmax((v - physMinV) / physRangeV, 0.0), 1.0) * 255.0 + 0.5);
                            physData[off+2] = 0; physData[off+3] = 255;
                        }
                    } else if (grid[j]) {
                        double val = grid[j][i];
                        if (val == ncdf_NOTDEF || !isfinite(val)) {
                            physData[off] = 0; physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 0;
                        } else {
                            physData[off] = (unsigned char)(fmin(fmax((val - physMin) / physRange, 0.0), 1.0) * 255.0 + 0.5);
                            physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 255;
                        }
                    }
                }
            }

            // Upload physical texture
            if (!(*hasPhysicalTex)) {
                glGenTextures(1, physicalTexID);
                glBindTexture(GL_TEXTURE_2D, *physicalTexID);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, physData);
                *hasPhysicalTex = true;
            } else {
                glBindTexture(GL_TEXTURE_2D, *physicalTexID);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, physData);
            }
            settings.physMin = (float)physMin;
            settings.physMax = (float)physMax;
            settings.physMinV = (float)physMinV;
            settings.physMaxV = (float)physMaxV;
            free(physData);
        }
    }
}

//===================================================================
// (h) Tile UV computation
//===================================================================
struct TileGrid {
    double *lva;
    int xs, ys, gridW, gridH;
    bool repeat;
    double lon_min, lon_range, lonstep;
    double lat_min, latstep;
};

static TileGrid ComputeTileUVs(PlugIn_ViewPort *vp, TexBuildContext &ctx,
                               int dataDim[2], int glDim[2])
{
    TileGrid tg;
    memset(&tg, 0, sizeof(tg));

    int ni = ctx.ni, nj = ctx.nj, tw = glDim[0], th = glDim[1];
    double potNormX = (double)dataDim[0] / tw;
    double potNormY = (double)dataDim[1] / th;
    tg.lat_min = ctx.blat; tg.lon_min = ctx.tlon;
    double lat_max = ctx.tlat, lon_max = ctx.blon;
    tg.latstep = fabs(lat_max - tg.lat_min) / (nj - 1);
    tg.lonstep = (lon_max - tg.lon_min) / (ni - 1);
    if (tg.latstep < 1e-10 || tg.lonstep < 1e-10) return tg;
    double clon = (tg.lon_min + lon_max) / 2;

    tg.repeat = (lon_max - tg.lon_min + tg.lonstep >= 360);
    tg.lon_range = lon_max - tg.lon_min;

    // Tile grid for non-linear projection handling
    double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
    if (pw < 20) pw = 20;
    tg.xs = (int)ceil(vp->pix_width / pw);
    tg.ys = (int)ceil(vp->pix_height / pw);
    if (vp->rotation == 0) tg.xs = 1;
    if (tg.xs < 2) tg.xs = 2; if (tg.ys < 2) tg.ys = 2;
    // Global data needs more tiles to avoid degenerate texture coords
    if (tg.repeat && tg.xs < 4) tg.xs = 4;
    if (tg.xs > 16) tg.xs = 16; if (tg.ys > 16) tg.ys = 16;
    tg.gridW = tg.xs + 1; tg.gridH = tg.ys + 1;

    tg.lva = new(std::nothrow) double[tg.gridW * tg.gridH * 2];
    if (!tg.lva) return tg;

    for (int i = 0; i < tg.gridW; i++) {
        double px = vp->pix_width / (double)tg.xs * i;
        for (int j = 0; j < tg.gridH; j++) {
            double py = vp->pix_height / (double)tg.ys * j;
            double lat, lon;
            wxPoint pt((int)px, (int)py);
            GetCanvasLLPix(vp, pt, &lat, &lon);
            if (tg.repeat) {
                // Wrap longitude into data range for GL_REPEAT
                double d = fmod(lon - tg.lon_min, tg.lon_range);
                if (d < 0) d += tg.lon_range;
                lon = tg.lon_min + d;
            } else {
                if (clon - lon > 180) lon += 360;
                else if (lon - clon > 180) lon -= 360;
            }
            int idx = (i * tg.gridH + j) * 2;
            tg.lva[idx]     = ((lon - tg.lon_min) / tg.lonstep - tg.repeat + 1.5) / tw * potNormX;
            tg.lva[idx + 1] = ((lat - tg.lat_min) / tg.latstep + 1.5) / th * potNormY;
        }
    }
    return tg;
}

//===================================================================
// (i) Common tile iteration with UV correction
//===================================================================
struct TileQuad {
    double u0, v0, u1, v1, u2, v2, u3, v3;
    float x, y, xS, yS;
};

typedef void (*TileDrawFunc)(const TileQuad &q, void *userData);

static void ForEachTile(PlugIn_ViewPort *vp, TileGrid &tg,
                         TileDrawFunc drawFunc, void *userData)
{
    int xs = tg.xs, ys = tg.ys, gridH = tg.gridH;
    double *lva = tg.lva;
    bool repeat = tg.repeat;
    double xS = vp->pix_width / (double)xs;
    double yS = vp->pix_height / (double)ys;

    for (int i = 0; i < xs; i++) {
        for (int j = 0; j < ys; j++) {
            int i00 = (i * gridH + j) * 2;
            int i10 = ((i+1) * gridH + j) * 2;
            int i11 = ((i+1) * gridH + j+1) * 2;
            int i01 = (i * gridH + j+1) * 2;
            TileQuad q;
            q.u0=lva[i00]; q.u1=lva[i10]; q.u2=lva[i11]; q.u3=lva[i01];
            q.v0=lva[i00+1]; q.v1=lva[i10+1]; q.v2=lva[i11+1]; q.v3=lva[i01+1];

            if (repeat) {
                if (q.u1 - q.u0 > .5) q.u1--;
                else if (q.u0 - q.u1 > .5) q.u1++;
                if (q.u2 - q.u0 > .5) q.u2--;
                else if (q.u0 - q.u2 > .5) q.u2++;
                if (q.u3 - q.u0 > .5) q.u3--;
                else if (q.u0 - q.u3 > .5) q.u3++;
            }

            if (!((repeat ||
                   ((q.u0>=0||q.u1>=0||q.u2>=0||q.u3>=0)&&(q.u0<=1||q.u1<=1||q.u2<=1||q.u3<=1))) &&
                  (q.v0>=0||q.v1>=0||q.v2>=0||q.v3>=0)&&(q.v0<=1||q.v1<=1||q.v2<=1||q.v3<=1))) continue;
            if (q.u1 <= q.u0) continue;

            q.x = (float)(xS * i);
            q.y = (float)(yS * j);
            q.xS = (float)xS;
            q.yS = (float)yS;
            drawFunc(q, userData);
        }
    }
}

//===================================================================
// (i) Shader draw path
//===================================================================
static void DrawShaderTile(const TileQuad &q, void *userData) {
    GLfloat verts[8] = {q.x, q.y, q.x+q.xS, q.y,
                        q.x+q.xS, q.y+q.yS, q.x, q.y+q.yS};
    GLfloat uvs[8] = {(float)q.u0,(float)q.v0, (float)q.u1,(float)q.v1,
                      (float)q.u2,(float)q.v2, (float)q.u3,(float)q.v3};
    ncdf_shader_enable_attrib(0);
    ncdf_shader_enable_attrib(1);
    ncdf_shader_attrib_pointer(0, 2, 0, verts);
    ncdf_shader_attrib_pointer(1, 2, 0, uvs);
    glDrawArrays(GL_QUADS, 0, 4);
    ncdf_shader_disable_attrib(0);
    ncdf_shader_disable_attrib(1);
}

static void DrawShaderPath(PlugIn_ViewPort *vp,
                           ncdfOverlayFactory::RenderSettings &settings,
                           GLuint texID, GLuint lutID, bool hasLUT,
                           GLuint *physicalTexID, bool *hasPhysicalTex,
                           TileGrid &tg, int tw, int th)
{
    ncdf_shader_use_program(ncdf_shader_get_grid_program());
    NcdfShaderUniforms u = ncdf_shader_get_uniforms();
    if (u.texSize >= 0) ncdf_shader_uniform_2f(u.texSize, (float)tw, (float)th);
    if (u.vectorMode >= 0) ncdf_shader_uniform_1i(u.vectorMode, settings.vectorMode ? 1 : 0);
    if (u.dataMin >= 0) ncdf_shader_uniform_1f(u.dataMin, settings.dataMin);
    if (u.dataMax >= 0) ncdf_shader_uniform_1f(u.dataMax, settings.dataMax);
    if (u.dataMinV >= 0) ncdf_shader_uniform_1f(u.dataMinV, settings.dataMinV);
    if (u.dataMaxV >= 0) ncdf_shader_uniform_1f(u.dataMaxV, settings.dataMaxV);
    if (u.lutMin >= 0) ncdf_shader_uniform_1f(u.lutMin, settings.lutMin);
    if (u.lutMax >= 0) ncdf_shader_uniform_1f(u.lutMax, settings.lutMax);
    if (u.physMin >= 0) ncdf_shader_uniform_1f(u.physMin, settings.physMin);
    if (u.physMax >= 0) ncdf_shader_uniform_1f(u.physMax, settings.physMax);
    if (u.physMinV >= 0) ncdf_shader_uniform_1f(u.physMinV, settings.physMinV);
    if (u.physMaxV >= 0) ncdf_shader_uniform_1f(u.physMaxV, settings.physMaxV);
    if (u.dataTex >= 0) {
        ncdf_shader_active_texture(0x84C0);
        glBindTexture(GL_TEXTURE_2D, texID);
        ncdf_shader_uniform_1i(u.dataTex, 0);
    }
    if (u.colorLUT >= 0 && hasLUT) {
        ncdf_shader_active_texture(0x84C1);
        glBindTexture(GL_TEXTURE_2D, lutID);
        ncdf_shader_uniform_1i(u.colorLUT, 1);
    }
    // Bind physical texture for vector mode (scalar uses dataTex.g)
    if (u.physicalTex >= 0 && settings.vectorMode && hasPhysicalTex && *hasPhysicalTex) {
        ncdf_shader_active_texture(0x84C2);
        glBindTexture(GL_TEXTURE_2D, *physicalTexID);
        ncdf_shader_uniform_1i(u.physicalTex, 2);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ForEachTile(vp, tg, DrawShaderTile, NULL);

    // Restore fixed-function state
    ncdf_shader_use_program(0);
    ncdf_shader_active_texture(0x84C0);
    glDisable(GL_BLEND);
}

//===================================================================
// Shared grid overlay renderer
//===================================================================
void ncdfOverlayFactory::RenderGridOverlay(PlugIn_ViewPort *vp,
                                           double **grid,
                                           ColorFunc colorFunc,
                                           RenderSettings &settings,
                                           GLuint &texID, bool &hasTex, bool &needsRebuild,
                                           int dataDim[2], int glDim[2],
                                           GLuint &lutID, bool &hasLUT,
                                           unsigned char *&uploadBuf, int &uploadBufSize,
                                           double **slopeGrid,
                                           GLuint slopeTexID,
                                           GLuint *physicalTexID, bool *hasPhysicalTex)
{
    if (!gui || !vp || !grid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    int tw = 0, th = 0;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        // --- Texture creation/update ---
        double lonMin = tlon, lonMax = blon;
        double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
        bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);
        int borderH = repeat ? 0 : 1;
        int extraCol = repeat ? 1 : 0;
        int expectedTw = ni + 2 * borderH + extraCol;
        int expectedTh = nj + 2;

        bool dataChanged = needsRebuild || !hasTex;
        if (needsRebuild) {
            if (hasTex && texID && (glDim[0] != expectedTw || glDim[1] != expectedTh)) {
                glDeleteTextures(1, &texID); texID = 0;
                hasTex = false;
            }
        }

        if (!hasTex || dataChanged) {
            tw = expectedTw; th = expectedTh;
            int _bufSize = tw * th * 4;
            if (!uploadBuf || uploadBufSize < _bufSize) {
                if (uploadBuf) free(uploadBuf);
                uploadBuf = (unsigned char*)malloc(_bufSize);
                uploadBufSize = uploadBuf ? _bufSize : 0;
            }
            unsigned char *texData = uploadBuf;
            if (!texData) return;
            memset(texData, 0, tw * th * 4);

            TexBuildContext ctx = { gui, ni, nj, tw, th, borderH, repeat,
                                   texData, tlon, tlat, blon, blat };

            if (settings.useShader) {
                if (settings.vectorMode && settings.uGrid && settings.vGrid)
                    FillVectorCoeffTex(ctx, settings);
                else
                    FillScalarCoeffTex(ctx, grid, settings);
                BuildLUTTex(colorFunc, settings, lutID, hasLUT);
            }

            ApplyTextureBorders(ctx);
            UploadCoeffTexture(texID, hasTex, tw, th, repeat, dataDim, glDim, texData);
            BuildPhysicalTex(ctx, grid, settings, repeat, physicalTexID, hasPhysicalTex);
            needsRebuild = false;
        }  // end if (!hasTex || dataChanged)

        // --- Draw tiled texture (GRIB pattern) ---
        if (hasTex && texID) {
            // Reuse tlon/tlat/blon/blat from class members for draw context
            TexBuildContext drawCtx = { gui, ni, nj, glDim[0], glDim[1], 0, false,
                                       NULL, tlon, tlat, blon, blat };
            TileGrid tg = ComputeTileUVs(vp, drawCtx, dataDim, glDim);
            if (!tg.lva) return;

            if (ncdf_shader_initialized() && ncdf_shader_get_grid_program() > 0) {
                DrawShaderPath(vp, settings, texID, lutID, hasLUT,
                               physicalTexID, hasPhysicalTex,
                               tg, glDim[0], glDim[1]);
            }
            delete[] tg.lva;
        }
#endif
    }
}

// Generic isoline renderer: CPU Marching Squares + glBegin/glEnd
void ncdfOverlayFactory::DrawIsoLines(PlugIn_ViewPort *vp,
                         double **grid, int ni, int nj,
                         bool &needsRebuild,
                         std::vector<ncdfIsoSeg> &segments,
                         double tlat, double tlon, double blat, double blon,
                         double jDirectionIncr)
{
    if (!vp || !grid || ni < 2 || nj < 2) return;

    // Rebuild segments when data changes
    if (needsRebuild) {
        segments.clear();
        double dMin = 1e10, dMax = -1e10;
        for (int j = 0; j < nj; j++) {
            if (!grid[j]) break;
            for (int i = 0; i < ni; i++) {
                double v = grid[j][i];
                if (v == ncdf_NOTDEF || v != v || !isfinite(v)) continue;
                if (v < dMin) dMin = v;
                if (v > dMax) dMax = v;
            }
        }
        if (dMin < dMax) {
            // Adaptive spacing: ~10 isolines, rounded to "nice" numbers
            double range = dMax - dMin;
            double rawSpacing = range / 10.0;
            double mag = pow(10.0, floor(log10(rawSpacing)));
            double norm = rawSpacing / mag;
            double spacing;
            if (norm < 1.5) spacing = mag;
            else if (norm < 3.5) spacing = 2.0 * mag;
            else if (norm < 7.5) spacing = 5.0 * mag;
            else spacing = 10.0 * mag;
            if (spacing < 1e-10) spacing = 1.0;

            dMin = floor(dMin / spacing) * spacing;
            dMax = ceil(dMax / spacing) * spacing;
            double lat_origin, lon_origin;
            if (jDirectionIncr >= 0) {
                // South-to-north: j=0 at south boundary
                lat_origin = blat;  // min latitude
            } else {
                // North-to-south: j=0 at north boundary
                lat_origin = tlat;  // max latitude
            }
            lon_origin = tlon;  // min longitude
            double incrLat = fabs((tlat - blat) / (nj - 1));
            if (jDirectionIncr < 0) incrLat = -incrLat;
            double incrLon = (blon - lon_origin) / (ni - 1);
            for (double val = dMin; val <= dMax; val += spacing) {
                IsoLine isoLine(val, grid, nj, ni, lat_origin, lon_origin, incrLat, incrLon);
                std::list<Segment*>& trace = isoLine.getTrace();
                for (std::list<Segment*>::iterator it = trace.begin(); it != trace.end(); ++it) {
                    Segment *seg = *it;
                    ncdfIsoSeg s = {seg->py1, seg->px1, seg->py2, seg->px2, val};
                    segments.push_back(s);
                }
            }
        }
        needsRebuild = false;
    }

    if (segments.empty()) return;

#ifdef ocpnUSE_GL
    glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(1.0f);
        glColor4ub(80, 80, 80, 220);

        glBegin(GL_LINES);
        for (size_t k = 0; k < segments.size(); k++) {
            const ncdfIsoSeg& s = segments[k];
            wxPoint ab, cd;
            GetCanvasPixLL(vp, &ab, s.lat1, s.lon1);
            GetCanvasPixLL(vp, &cd, s.lat2, s.lon2);
            if (fabs((double)ab.x - cd.x) > vp->pix_width / 2) continue;
            glVertex2i(ab.x, ab.y);
            glVertex2i(cd.x, cd.y);
        }
        glEnd();

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#endif
}
