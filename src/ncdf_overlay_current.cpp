#include "ncdf_overlay_current.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"
#include "shader/ncdf_shader.h"
#include <cmath>
#include <chrono>

extern void ncdfLog(const char* format, ...);
extern void BicubicSplinePrefilterWrapAware(float* dst, const float* src,
                                             const unsigned char* mask,
                                             int w, int h, bool isGlobal);

bool CurrentOverlay::s_smoothColors = false;

void CurrentOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true; dataReady = false;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    physicalTexID = 0; hasPhysicalTex = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedNj = cachedNi = 0;
    cachedU = NULL; cachedV = NULL; cachedUNj = cachedVNi = 0;
    cachedCoeffU = NULL; cachedCoeffV = NULL;
    cachedCoefU_min = cachedCoefU_max = cachedCoefV_min = cachedCoefV_max = 0;
    cachedCoeffNj = cachedCoeffNi = 0;
    cachedDataMin = 0; cachedDataMax = 1;
    cachedCoefMin = 0; cachedCoefMax = 1;
    cachedPhysMin = 0; cachedPhysMax = 1;
    cachedDataMinV = 0; cachedDataMaxV = 1;
    cachedPhysMinV = 0; cachedPhysMaxV = 1;
    cachedVorticity.reset(); vortNj = vortNi = 0;
    vortTexture = 0; hasVortTexture = false;
    slopeSource = 0;
    licTexture = 0; hasLICTexture = false; needsLICRebuild = true;
}

void CurrentOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (hasPhysicalTex && physicalTexID) { glDeleteTextures(1, &physicalTexID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
    if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    if (cachedVorticity) {
        for (int j = 0; j < vortNj; j++) delete[] cachedVorticity[j];
        cachedVorticity.reset();
    }
    if (hasVortTexture && vortTexture) { glDeleteTextures(1, &vortTexture); }
    if (hasLICTexture && licTexture) { glDeleteTextures(1, &licTexture); }
    Init();
}

void CurrentOverlay::clearCache() {
    if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
    if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    if (cachedVorticity) { for (int j = 0; j < vortNj; j++) delete[] cachedVorticity[j]; cachedVorticity.reset(); }
    dataReady = false;
    needsRebuild = true;
    needsLICRebuild = true;
    // GPU textures preserved: glTexture, lutID, physicalTexID, uploadBuf, vortTexture, licTexture
}

void CurrentOverlay::Invalidate() {
    ncdfLog("[invalidate:cur] needsRebuild was=%d hasPhysicalTex=%d hasTex=%d\n", (int)needsRebuild, (int)hasPhysicalTex, (int)hasTexture);
    needsRebuild = true;
    needsLICRebuild = true;
    // Don't delete GL textures here — setData() may run on a non-GL thread.
    // Defer deletion to RenderGridOverlay (render thread has GL context).
    hasPhysicalTex = false;  // Mark stale; render thread will glDeleteTextures
}

void CurrentOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    if (!gui || !gui->myMessage.hasCurrent()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Build U/V grids as float
    if (!cachedU || !cachedV) {
        ncdfLog("[prepare:cur] grid cache MISS — allocating %dx%d\n", ni, nj);
        cachedU = new float*[nj]; cachedV = new float*[nj];
        for (int j = 0; j < nj; j++) {
            cachedU[j] = new float[ni]; cachedV[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                cachedU[j][i] = (u == ncdf_NOTDEF || !isfinite(u)) ? NAN : (float)u;
                cachedV[j][i] = (v == ncdf_NOTDEF || !isfinite(v)) ? NAN : (float)v;
            }
        }
        cachedUNj = nj; cachedVNi = ni;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // 2. B-spline coefficients for U and V
    bool coeffRebuilt = false;
    if (!cachedCoeffU || !cachedCoeffV || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        ncdfLog("[prepare:cur] coeff cache MISS — cachedCoeffU=%p cachedNj=%d cachedNi=%d vs nj=%d ni=%d\n",
            (void*)cachedCoeffU, cachedCoeffNj, cachedCoeffNi, nj, ni);
        if (cachedCoeffU) free(cachedCoeffU);
        if (cachedCoeffV) free(cachedCoeffV);
        cachedCoeffU = (float*)calloc(nj * ni, sizeof(float));
        cachedCoeffV = (float*)calloc(nj * ni, sizeof(float));
        if (cachedCoeffU && cachedCoeffV) {
            double lonMin = factory->tlon, lonMax = factory->blon;
            double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
            bool isGlobal = (lonMax - lonMin + gridSpacingLon >= 360);
            ncdfOverlayFactory::PrefilterCoefficients(
                cachedCoeffU, cachedCoeffV, cachedU, cachedV,
                ni, nj, isGlobal,
                cachedCoefU_min, cachedCoefU_max, cachedCoefV_min, cachedCoefV_max);
            cachedCoeffNj = nj; cachedCoeffNi = ni;
            // Set physical value range for shader denormalization (coefRange * bsplineScale)
            const float bsplineScale = 2.5858f;
            cachedCoefMin = cachedCoefU_min * bsplineScale;
            cachedCoefMax = cachedCoefU_max * bsplineScale;
        }
        coeffRebuilt = true;
    }

    // Compute speed range from U/V (for shader normalization)
    float dMin = 1e30f, dMax = -1e30f;
    for (int jj = 0; jj < nj; jj++)
        for (int ii = 0; ii < ni; ii++) {
            float u = cachedU[jj][ii], v = cachedV[jj][ii];
            if (isfinite(u) && isfinite(v)) {
                float spd = sqrtf(u * u + v * v);
                if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
            }
        }
    cachedDataMin = (dMin < dMax) ? dMin : 0;
    cachedDataMax = (dMin < dMax) ? dMax : 1;

    auto t3 = std::chrono::high_resolution_clock::now();

    dataReady = true;
    needsRebuild = coeffRebuilt;  // Only rebuild texture when data actually changed
    auto uvMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto bsplineMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t1).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t0).count();
    ncdfLog("[perf] Current::prepareData ni=%d nj=%d uv_grid=%lldms bspline(U+V)=%lldms total=%lldms\n",
        ni, nj, (long long)uvMs, (long long)bsplineMs, (long long)totalMs);
}

wxColour CurrentOverlay::GetColor(double val_in) {
    static const double stops[][4] = {
        {0.00,  20,  20, 180}, {0.10,  30,  80, 220}, {0.25,   0, 180, 220},
        {0.50,   0, 200,  80}, {0.75, 220, 220,  20}, {1.00, 240, 100,  20},
        {1.50, 220,  20,  20},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 7, wxMax(val_in, 0.0), s_smoothColors);
}

bool CurrentOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                     ncdfOverlayFactory *factory,
                                     double** animatedGrid, double** animUGrid, double** animVGrid) {
    if (!gui) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Texture reuse: GPU texture already exists, just draw it (view pan / refresh)
    if (hasTexture && !needsRebuild && !animatedGrid) {
        ncdfLog("[render:current] REUSE path: hasTex=%d needsRebuild=%d cachedCoefMin=%f cachedCoefMax=%f\n",
            (int)hasTexture, (int)needsRebuild, cachedCoefMin, cachedCoefMax);
        ncdfOverlayFactory::RenderSettings settings;
        settings.smoothColors = false;
        settings.vectorMode = true;  // Texture was created in vector mode (R=U, G=V)
        settings.uGrid = cachedU;
        settings.vGrid = cachedV;
        settings.precompCoeffU = NULL;
        settings.precompCoeffV = NULL;
        settings.dataMin = cachedCoefMin;
        settings.dataMax = cachedCoefMax;
        settings.dataMinV = cachedDataMinV;
        settings.dataMaxV = cachedDataMaxV;
        settings.physMin = cachedPhysMin;
        settings.physMax = cachedPhysMax;
        settings.physMinV = cachedPhysMinV;
        settings.physMaxV = cachedPhysMaxV;
        settings.lutMin = 0.0f;
        settings.lutMax = 1.5f;
        factory->RenderGridOverlay(vp, (float**)(size_t)1,
            CurrentOverlay::GetColor, settings,
            glTexture, hasTexture, needsRebuild,
            dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
            NULL, 0,
            &physicalTexID, &hasPhysicalTex);
        return true;
    }

    // If animated grid provided, use it directly (animation replaces static color map)
    if (animatedGrid) {
        cachedNj = nj; cachedNi = ni;
        // Compute data range from animated grid (speed)
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = animatedGrid[jj][ii];
                if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                    if (v < dMin) dMin = v; if (v > dMax) dMax = v;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
        needsRebuild = true;  // force texture rebuild every animation frame
    } else {
        // Static mode: no speed grid needed — U/V grids and coefficients are cached
        if (!cachedU || !cachedV || !cachedCoeffU || !cachedCoeffV) return false;
    }

    // Build shader settings
    ncdfLog("[render:current] REBUILD path: cachedU=%p cachedCoeffU=%p hasTex=%d needsRebuild=%d\n",
        (void*)cachedU, (void*)cachedCoeffU, (int)hasTexture, (int)needsRebuild);
    ncdfOverlayFactory::RenderSettings settings;
    settings.smoothColors = false;

    // Vector mode: pass u/v grids for shader-side speed computation
    if (animUGrid && animVGrid) {
        // Animation mode: convert advected double** grids to float**
        settings.vectorMode = true;
        // Allocate temporary float grids for animation
        float** animUF = new float*[nj]; float** animVF = new float*[nj];
        for (int j = 0; j < nj; j++) {
            animUF[j] = new float[ni]; animVF[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                animUF[j][i] = (float)animUGrid[j][i];
                animVF[j][i] = (float)animVGrid[j][i];
            }
        }
        settings.uGrid = animUF;
        settings.vGrid = animVF;
    } else if (!animatedGrid && cachedU && cachedV) {
        // Static mode: use cached u/v grids (only rebuild when data changes)
        settings.vectorMode = true;
        settings.uGrid = cachedU;
        settings.vGrid = cachedV;
        settings.precompCoeffU = cachedCoeffU;
        settings.precompCoeffV = cachedCoeffV;
        settings.precompCoefU_min = cachedCoefU_min;
        settings.precompCoefU_max = cachedCoefU_max;
        settings.precompCoefV_min = cachedCoefV_min;
        settings.precompCoefV_max = cachedCoefV_max;
    } else {
        settings.vectorMode = false;
        settings.uGrid = NULL;
        settings.vGrid = NULL;
        settings.precompCoeffU = NULL;
        settings.precompCoeffV = NULL;
    }

    // Compute speed range for shader normalization (only when data changes)
    if (settings.vectorMode && settings.uGrid && settings.vGrid && (needsRebuild || !hasTexture)) {
        float dMin = 1e30f, dMax = -1e30f;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                float u = settings.uGrid[jj][ii], v = settings.vGrid[jj][ii];
                if (isfinite(u) && isfinite(v)) {
                    float spd = sqrtf(u*u + v*v);
                    if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    }

    // Use cached coefMin/coefMax if texture exists (set by RenderGridOverlay on first frame)
    ncdfLog("[perf] Current RENDER: hasTex=%d coefMin=%f coefMax=%f dataMin=%f dataMax=%f\n",
        (int)hasTexture, cachedCoefMin, cachedCoefMax, cachedDataMin, cachedDataMax);
    if (hasTexture) {
        settings.dataMin = cachedCoefMin;
        settings.dataMax = cachedCoefMax;
        settings.dataMinV = cachedDataMinV;
        settings.dataMaxV = cachedDataMaxV;
        settings.physMin = cachedPhysMin;
        settings.physMax = cachedPhysMax;
        settings.physMinV = cachedPhysMinV;
        settings.physMaxV = cachedPhysMaxV;
    } else {
        settings.dataMin = cachedDataMin;
        settings.dataMax = cachedDataMax;
        settings.dataMinV = 0.0f;
        settings.dataMaxV = 1.0f;
        settings.physMin = cachedDataMin;   // Speed range for physical texture denormalization
        settings.physMax = cachedDataMax;
        settings.physMinV = 0.0f;
        settings.physMaxV = 1.0f;
    }
    settings.lutMin = 0.0f;    // Current speed color stops range
    settings.lutMax = 1.5f;

    s_smoothColors = settings.smoothColors;
    float** renderGrid = (float**)(size_t)1;
    factory->RenderGridOverlay(vp, renderGrid,
        CurrentOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
        NULL, 0,
        &physicalTexID, &hasPhysicalTex);

    // Free temporary animation float grids
    if (animUGrid && animVGrid && settings.uGrid != cachedU) {
        for (int j = 0; j < nj; j++) { delete[] settings.uGrid[j]; delete[] settings.vGrid[j]; }
        delete[] settings.uGrid; delete[] settings.vGrid;
    }

    // Texture is now on GPU — release CPU-side data
    if (hasTexture) {
        // Save physical value range (set by FillVectorCoeffMin: coefMin/Max * bsplineScale)
        ncdfLog("[perf] Current SAVE: settings.dataMin=%f settings.dataMax=%f\n",
            settings.dataMin, settings.dataMax);
        cachedCoefMin = settings.dataMin;
        cachedCoefMax = settings.dataMax;
        cachedDataMinV = settings.dataMinV;
        cachedDataMaxV = settings.dataMaxV;
        cachedPhysMin = settings.physMin;
        cachedPhysMax = settings.physMax;
        cachedPhysMinV = settings.physMinV;
        cachedPhysMaxV = settings.physMaxV;
        // CPU caches (cachedU/V, cachedCoeffU/V) stay alive for reuse on next time step.
        // setData() will clear them when new data arrives.
    }

    return true;
}
