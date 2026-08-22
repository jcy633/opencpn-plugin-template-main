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

    ncdfLog("[pf] w=%d h=%d z=%d inv=%d\n", w, h, (int)(z*10000), (int)(inv*10000));

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
// Shared grid overlay renderer
//===================================================================
void ncdfOverlayFactory::RenderGridOverlay(PlugIn_ViewPort *vp,
                                           double **grid,
                                           ColorFunc colorFunc,
                                           const RenderSettings &settings,
                                           GLuint &texID, bool &hasTex, bool &needsRebuild,
                                           int dataDim[2], int glDim[2],
                                           GLuint &lutID, bool &hasLUT,
                                           unsigned char *&uploadBuf, int &uploadBufSize,
                                           double **slopeGrid,
                                           GLuint slopeTexID)
{
    if (!gui || !vp || !grid) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    ncdfLog("[ncdf] RenderGridOverlay: ni=%d nj=%d needsRebuild=%d hasTex=%d\n",
        ni, nj, (int)needsRebuild, (int)hasTex);

    int tw = 0, th = 0;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        // Compute expected texture dimensions
        double lonMin = tlon, lonMax = blon;
        double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
        bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);
        int borderH = repeat ? 0 : 1;
        int extraCol = repeat ? 1 : 0;
        int expectedTw = ni + 2 * borderH + extraCol;
        int expectedTh = nj + 2;

        // Fill texture data only when data changed or texture doesn't exist
        bool dataChanged = needsRebuild || !hasTex;
        if (needsRebuild) {
            if (hasTex && texID && (glDim[0] != expectedTw || glDim[1] != expectedTh)) {
                glDeleteTextures(1, &texID); texID = 0;
                hasTex = false;
            }
            needsRebuild = false;
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

            if (settings.useShader) {
                if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                    // --- Vector mode: R8 normalized u,v with B-spline prefilter ---
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
                    const_cast<RenderSettings&>(settings).dataMin = (float)dMin;
                    const_cast<RenderSettings&>(settings).dataMax = (float)dMax;

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
                        // Mask-aware B-spline prefilter (no FillInvalidPixels)
                        BicubicSplinePrefilterMasked(coeffU, rawU, validMask, ni, nj);
                        BicubicSplinePrefilterMasked(coeffV, rawV, validMask, ni, nj);

                        // Compute coefMin/coefMax from both u and v valid coefficients
                        float coefMin = 1e30f, coefMax = -1e30f;
                        for (int k = 0; k < nj * ni; k++) {
                            if (!validMask[k]) continue;
                            if (coeffU[k] < coefMin) coefMin = coeffU[k];
                            if (coeffU[k] > coefMax) coefMax = coeffU[k];
                            if (coeffV[k] < coefMin) coefMin = coeffV[k];
                            if (coeffV[k] > coefMax) coefMax = coeffV[k];
                        }
                        if (coefMax <= coefMin) { coefMin = 0; coefMax = 1; }
                        float coefRange = coefMax - coefMin;
                        if (coefRange < 1e-10f) coefRange = 1.0f;

                        const_cast<RenderSettings&>(settings).dataMin = coefMin;
                        const_cast<RenderSettings&>(settings).dataMax = coefMax;

                        ncdfLog("[vector] coefMin=%.4f coefMax=%.4f dMin=%.4f dMax=%.4f\n",
                            coefMin, coefMax, dMin, dMax);

                        for (int j = 0; j < nj; j++) {
                            int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                            for (int i = 0; i < ni; i++) {
                                int x = i + borderH, y = texRow + 1;
                                if (x >= tw - 1 || y >= th - 1) continue;
                                int off = 4 * (y * tw + x);
                                if (!validMask[j*ni+i]) {
                                    texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 0;
                                } else {
                                    texData[off]   = (unsigned char)(fmin(fmax((coeffU[j*ni+i] - coefMin) / coefRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                                    texData[off+1] = (unsigned char)(fmin(fmax((coeffV[j*ni+i] - coefMin) / coefRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                                    texData[off+2] = 0; texData[off+3] = 255;
                                }
                            }
                        }
                    }
                    if (rawU) free(rawU); if (rawV) free(rawV);
                    if (coeffU) free(coeffU); if (coeffV) free(coeffV);
                    if (validMask) free(validMask);
                    ncdfLog("[vector] B-spline u/v mask-aware prefiltred, R8 coefNorm\n");
                } else {
                // --- Scalar mode: mask-aware B-spline prefiltred coefficients ---

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

                    // DEBUG: Test prefilter with 100-element uniform data
                    {
                        float testSrc[100], testDst[100];
                        unsigned char testMask[100];
                        for (int k = 0; k < 100; k++) { testSrc[k] = 35.0f; testMask[k] = 1; }
                        BicubicSplinePrefilterMasked(testDst, testSrc, testMask, 100, 1);
                        // Check center (position 50) - far from boundary
                        ncdfLog("[pf100] c50=%d c25=%d c75=%d\n",
                            (int)(testDst[50]*100), (int)(testDst[25]*100), (int)(testDst[75]*100));
                        // Reconstruction at center: f(50) = (1/6)*c[48] + (4/6)*c[49] + (1/6)*c[50]
                        float recon = testDst[48]/6.0f + testDst[49]*4.0f/6.0f + testDst[50]/6.0f;
                        ncdfLog("[pf100] recon50=%d expect=3500\n", (int)(recon*100));
                    }

                    // Stage 1: Count valid/invalid pixels and physical data range
                    float physMin = 1e30f, physMax = -1e30f;
                    int validCount = 0, invalidCount = 0;
                    for (int k = 0; k < nj * ni; k++) {
                        if (!validMask[k]) { invalidCount++; continue; }
                        validCount++;
                        if (rawBuf[k] < physMin) physMin = rawBuf[k];
                        if (rawBuf[k] > physMax) physMax = rawBuf[k];
                    }
                    ncdfLog("[stage1-data] valid=%d invalid=%d physMin=%.4f physMax=%.4f\n",
                        validCount, invalidCount, physMin, physMax);

                    // Stage 2: Mask-aware B-spline prefilter
                    BicubicSplinePrefilterMasked(coeffBuf, rawBuf, validMask, ni, nj);

                    // Stage 3: Coefficient range and distribution
                    float coefMin = 1e30f, coefMax = -1e30f;
                    for (int k = 0; k < nj * ni; k++) {
                        if (!validMask[k]) continue;
                        if (coeffBuf[k] < coefMin) coefMin = coeffBuf[k];
                        if (coeffBuf[k] > coefMax) coefMax = coeffBuf[k];
                    }
                    if (coefMax <= coefMin) { coefMin = 0; coefMax = 1; }
                    float coefRange = coefMax - coefMin;
                    if (coefRange < 1e-10f) coefRange = 1.0f;

                    // Find the longest ocean segment in a middle row
                    int cj = nj / 2;  // middle row
                    int bestStart = 0, bestLen = 0, curStart = -1;
                    for (int i = 0; i <= ni; i++) {
                        bool valid = (i < ni) && validMask[cj*ni+i];
                        if (valid && curStart < 0) curStart = i;
                        if ((!valid || i == ni) && curStart >= 0) {
                            int len = i - curStart;
                            if (len > bestLen) { bestLen = len; bestStart = curStart; }
                            curStart = -1;
                        }
                    }
                    int bi = bestStart + bestLen / 2;  // center of longest segment
                    float pBest = rawBuf[cj*ni+bi];
                    float cBest = coeffBuf[cj*ni+bi];
                    float cBestL = (bi > 0) ? coeffBuf[cj*ni+bi-1] : cBest;
                    float cBestR = (bi < ni-1) ? coeffBuf[cj*ni+bi+1] : cBest;
                    float reconBest = cBestL/6.0f + cBest*4.0f/6.0f + cBestR/6.0f;
                    ncdfLog("[longseg] bestRow start=%d len=%d center=%d\n", bestStart, bestLen, bi);
                    ncdfLog("[longseg] phys=%.2f coef=%.2f recon=%.2f\n", pBest, cBest, reconBest);

                    ncdfLog("[stage3-coef] coefMin=%.4f coefMax=%.4f coefRange=%.4f\n", coefMin, coefMax, coefRange);

                    // Stage 4: R8 normalization
                    // Scan a row through center to find valid/invalid pattern
                    int rowStart = -1, rowEnd = -1, rowGaps = 0;
                    for (int i = 0; i < ni; i++) {
                        if (validMask[cj*ni+i]) {
                            if (rowStart < 0) rowStart = i;
                            rowEnd = i;
                        } else if (rowStart >= 0) {
                            rowGaps++;
                        }
                    }
                    ncdfLog("[stage3-row] centerRow: start=%d end=%d gaps=%d len=%d\n",
                        rowStart, rowEnd, rowGaps, rowEnd - rowStart + 1);

                    const float bsplineScale = 2.5858f;  // (1-z)^4, correction for 2D B-spline
                    const_cast<RenderSettings&>(settings).dataMin = coefMin * bsplineScale;
                    const_cast<RenderSettings&>(settings).dataMax = coefMax * bsplineScale;

                    // Stage 4: R8 normalization and sampling
                    int r8Min = 255, r8Max = 0;
                    for (int j = 0; j < nj; j++) {
                        int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                        for (int i = 0; i < ni; i++) {
                            int x = i + borderH, y = texRow + 1;
                            if (x >= tw - 1 || y >= th - 1) continue;
                            int off = 4 * (y * tw + x);
                            if (!validMask[j*ni+i]) {
                                texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 0;
                            } else {
                                float normalized = (coeffBuf[j*ni+i] - coefMin) / coefRange;
                                unsigned char q = (unsigned char)(fmin(fmax(normalized, 0.0f), 1.0f) * 255.0f + 0.5f);
                                texData[off] = q; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 255;
                                if (q < r8Min) r8Min = q; if (q > r8Max) r8Max = q;
                            }
                        }
                    }
                    ncdfLog("[stage4-r8] r8Min=%d r8Max=%d\n", r8Min, r8Max);

                    free(rawBuf); free(coeffBuf); free(validMask);
                }

                }  // end scalar mode else

                // Color LUT (shared between vector and scalar modes)
                // Only rebuild when lutMin/lutMax or colorFunc changes
                {
                    static float s_cachedLutMin = 0, s_cachedLutMax = 0;
                    static ColorFunc s_cachedColorFunc = NULL;
                    float lutMin = settings.lutMin;
                    float lutMax = settings.lutMax;
                    bool lutChanged = (lutMin != s_cachedLutMin || lutMax != s_cachedLutMax
                                       || colorFunc != s_cachedColorFunc);
                    if (lutChanged || !hasLUT || !lutID) {
                        float lutRange = lutMax - lutMin;
                        if (lutRange < 1e-10f) lutRange = 1.0f;
                        unsigned char lutData[256 * 4];
                        for (int k = 0; k < 256; k++) {
                            double val = lutMin + (k / 255.0) * lutRange;
                            wxColour c = colorFunc(val);
                            lutData[k*4] = c.Red(); lutData[k*4+1] = c.Green();
                            lutData[k*4+2] = c.Blue(); lutData[k*4+3] = 255;
                        }
                        ncdfLog("[stage5-lut] lutMin=%.4f lutMax=%.4f lutRange=%.4f changed=%d\n",
                            lutMin, lutMax, lutRange, (int)lutChanged);
                        ncdfLog("[stage5-lut] LUT[0]=(%d,%d,%d) val=%.2f | LUT[64]=(%d,%d,%d) val=%.2f | LUT[128]=(%d,%d,%d) val=%.2f | LUT[192]=(%d,%d,%d) val=%.2f | LUT[255]=(%d,%d,%d) val=%.2f\n",
                            lutData[0], lutData[1], lutData[2], lutMin + 0.0/255.0*lutRange,
                            lutData[256], lutData[257], lutData[258], lutMin + 64.0/255.0*lutRange,
                            lutData[512], lutData[513], lutData[514], lutMin + 128.0/255.0*lutRange,
                            lutData[768], lutData[769], lutData[770], lutMin + 192.0/255.0*lutRange,
                            lutData[1020], lutData[1021], lutData[1022], lutMin + 255.0/255.0*lutRange);
                        if (!hasLUT || !lutID) {
                            glGenTextures(1, &lutID);
                            glBindTexture(GL_TEXTURE_2D, lutID);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
                            hasLUT = true;
                        } else {
                            glBindTexture(GL_TEXTURE_2D, lutID);
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
                        }
                        s_cachedLutMin = lutMin; s_cachedLutMax = lutMax;
                        s_cachedColorFunc = colorFunc;
                    }
                }

            } else {
                // --- Fixed-function path: colored RGBA texture ---
                unsigned char alpha = 255;
                for (int j = 0; j < nj; j++) {
                    if (!grid[j]) break;
                    int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                    for (int i = 0; i < ni; i++) {
                        double val = grid[j][i];
                        if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) continue;
                        int x = i + borderH, y = texRow + 1;
                        if (x >= tw - 1 || y >= th - 1) continue;
                        wxColour c = colorFunc(val);
                        int off = 4 * (y * tw + x);
                        unsigned char r = c.Red(), g = c.Green(), b = c.Blue();

                        // (slope shading removed - always use shader)
                        texData[off]     = r;
                        texData[off + 1] = g;
                        texData[off + 2] = b;
                        texData[off + 3] = alpha;
                    }
                }
            }

            // Repeat mode: duplicate first column at end for seamless wrapping
            if (repeat) {
                for (int j = 0; j < nj; j++) {
                    int texRow = (gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
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
        }  // end if (!hasTex || dataChanged)

        // Draw tiled texture (GRIB pattern)
        if (hasTex && texID) {
            tw = glDim[0]; th = glDim[1];
            double potNormX = (double)dataDim[0] / tw;
            double potNormY = (double)dataDim[1] / th;
            double lat_min = blat, lon_min = tlon;
            double lat_max = tlat, lon_max = blon;
            double latstep = fabs(lat_max - lat_min) / (nj - 1);
            double lonstep = (lon_max - lon_min) / (ni - 1);
            if (latstep < 1e-10 || lonstep < 1e-10) return;
            double clon = (lon_min + lon_max) / 2;

            bool repeat = (lon_max - lon_min + lonstep >= 360);
            double lon_range = lon_max - lon_min;

            // Tile grid for non-linear projection handling
            double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
            if (pw < 20) pw = 20;
            int xs = (int)ceil(vp->pix_width / pw);
            int ys = (int)ceil(vp->pix_height / pw);
            if (vp->rotation == 0) xs = 1;
            if (xs < 2) xs = 2; if (ys < 2) ys = 2;
            // Global data needs more tiles to avoid degenerate texture coords
            if (repeat && xs < 4) xs = 4;
            if (xs > 16) xs = 16; if (ys > 16) ys = 16;
            int gridW = xs + 1, gridH = ys + 1;

            double *lva = new(std::nothrow) double[gridW * gridH * 2];
            if (!lva) return;

            for (int i = 0; i < gridW; i++) {
                double px = vp->pix_width / (double)xs * i;
                for (int j = 0; j < gridH; j++) {
                    double py = vp->pix_height / (double)ys * j;
                    double lat, lon;
                    wxPoint pt((int)px, (int)py);
                    GetCanvasLLPix(vp, pt, &lat, &lon);
                    if (repeat) {
                        // Wrap longitude into data range for GL_REPEAT
                        double d = fmod(lon - lon_min, lon_range);
                        if (d < 0) d += lon_range;
                        lon = lon_min + d;
                    } else {
                        if (clon - lon > 180) lon += 360;
                        else if (lon - clon > 180) lon -= 360;
                    }
                    int idx = (i * gridH + j) * 2;
                    lva[idx]     = ((lon - lon_min) / lonstep - repeat + 1.5) / tw * potNormX;
                    lva[idx + 1] = ((lat - lat_min) / latstep + 1.5) / th * potNormY;
                }
            }

            // === Shader bicubic path ===
            if (settings.useShader && ncdf_shader_initialized() &&
                ncdf_shader_get_grid_program() > 0) {
                wxLogMessage(_T("[shader] drawing with Catmull-Rom bicubic, texID=%u LUT=%u"),
                             texID, lutID);
                ncdf_shader_use_program(ncdf_shader_get_grid_program());
                NcdfShaderUniforms u = ncdf_shader_get_uniforms();
                if (u.texSize >= 0) ncdf_shader_uniform_2f(u.texSize, (float)tw, (float)th);
                if (u.vectorMode >= 0) ncdf_shader_uniform_1i(u.vectorMode, settings.vectorMode ? 1 : 0);
                if (u.dataMin >= 0) ncdf_shader_uniform_1f(u.dataMin, settings.dataMin);
                if (u.dataMax >= 0) ncdf_shader_uniform_1f(u.dataMax, settings.dataMax);
                if (u.lutMin >= 0) ncdf_shader_uniform_1f(u.lutMin, settings.lutMin);
                if (u.lutMax >= 0) ncdf_shader_uniform_1f(u.lutMax, settings.lutMax);
                ncdfLog("[draw] vec=%d dMin=%.3f dMax=%.3f lutMin=%.3f lutMax=%.3f texID=%u lutID=%u hasLUT=%d\n",
                    (int)settings.vectorMode, settings.dataMin, settings.dataMax,
                    settings.lutMin, settings.lutMax, texID, lutID, (int)hasLUT);
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

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                double xS = vp->pix_width / (double)xs;
                double yS = vp->pix_height / (double)ys;
                for (int i = 0; i < xs; i++) {
                    for (int j = 0; j < ys; j++) {
                        int i00 = (i * gridH + j) * 2;
                        int i10 = ((i+1) * gridH + j) * 2;
                        int i11 = ((i+1) * gridH + j+1) * 2;
                        int i01 = (i * gridH + j+1) * 2;
                        double u0=lva[i00], u1=lva[i10], u2=lva[i11], u3=lva[i01];
                        double v0=lva[i00+1], v1=lva[i10+1], v2=lva[i11+1], v3=lva[i01+1];

                        if (repeat) {
                            if (u1 - u0 > .5) u1--;
                            else if (u0 - u1 > .5) u1++;
                            if (u2 - u0 > .5) u2--;
                            else if (u0 - u2 > .5) u2++;
                            if (u3 - u0 > .5) u3--;
                            else if (u0 - u3 > .5) u3++;
                        }

                        if (!((repeat ||
                               ((u0>=0||u1>=0||u2>=0||u3>=0)&&(u0<=1||u1<=1||u2<=1||u3<=1))) &&
                              (v0>=0||v1>=0||v2>=0||v3>=0)&&(v0<=1||v1<=1||v2<=1||v3<=1))) continue;
                        if (u1 <= u0) continue;
                        double x = xS * i, y = yS * j;
                        GLfloat verts[8] = {(float)x,(float)y, (float)(x+xS),(float)y,
                                            (float)(x+xS),(float)(y+yS), (float)x,(float)(y+yS)};
                        GLfloat uvs[8] = {(float)u0,(float)v0, (float)u1,(float)v1,
                                          (float)u2,(float)v2, (float)u3,(float)v3};
                        ncdf_shader_enable_attrib(0);
                        ncdf_shader_enable_attrib(1);
                        ncdf_shader_attrib_pointer(0, 2, 0, verts);
                        ncdf_shader_attrib_pointer(1, 2, 0, uvs);
                        glDrawArrays(GL_QUADS, 0, 4);
                        ncdf_shader_disable_attrib(0);
                        ncdf_shader_disable_attrib(1);
                    }
                }

                // Restore fixed-function state
                ncdf_shader_use_program(0);
                ncdf_shader_active_texture(0x84C0);
                glDisable(GL_BLEND);
                delete[] lva;
                return;
            }

            // === Fixed-function fallback path ===
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texID);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glColor4f(1, 1, 1, 1);

            double xS = vp->pix_width / (double)xs;
            double yS = vp->pix_height / (double)ys;
            for (int i = 0; i < xs; i++) {
                for (int j = 0; j < ys; j++) {
                    int i00 = (i * gridH + j) * 2;
                    int i10 = ((i+1) * gridH + j) * 2;
                    int i11 = ((i+1) * gridH + j+1) * 2;
                    int i01 = (i * gridH + j+1) * 2;
                    double u0=lva[i00], u1=lva[i10], u2=lva[i11], u3=lva[i01];
                    double v0=lva[i00+1], v1=lva[i10+1], v2=lva[i11+1], v3=lva[i01+1];

                    if (repeat) {
                        if (u1 - u0 > .5) u1--;
                        else if (u0 - u1 > .5) u1++;
                        if (u2 - u0 > .5) u2--;
                        else if (u0 - u2 > .5) u2++;
                        if (u3 - u0 > .5) u3--;
                        else if (u0 - u3 > .5) u3++;
                    }

                    if (!((repeat ||
                           ((u0>=0||u1>=0||u2>=0||u3>=0)&&(u0<=1||u1<=1||u2<=1||u3<=1))) &&
                          (v0>=0||v1>=0||v2>=0||v3>=0)&&(v0<=1||v1<=1||v2<=1||v3<=1))) continue;
                    if (u1 <= u0) continue;
                    double x = xS * i, y = yS * j;
                    glBegin(GL_QUADS);
                    glTexCoord2d(u0,v0); glVertex2f((float)x,(float)y);
                    glTexCoord2d(u1,v1); glVertex2f((float)(x+xS),(float)y);
                    glTexCoord2d(u2,v2); glVertex2f((float)(x+xS),(float)(y+yS));
                    glTexCoord2d(u3,v3); glVertex2f((float)x,(float)(y+yS));
                    glEnd();
                }
            }
            delete[] lva;

            glDisable(GL_BLEND);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
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
