#include "ncdf_overlay_salinity.h"
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


void SalinityOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true; dataReady = false;
    gridReady = false;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedBaseGrid.reset(); cachedNj = cachedNi = 0;
    cachedDataMin = 0; cachedDataMax = 1;
    cachedCoefMin = 0; cachedCoefMax = 1;
    cachedPhysMin = 0; cachedPhysMax = 1;
    cachedPhysMinV = 0; cachedPhysMaxV = 1;
    cachedCoeff = NULL; cachedCoefScalarMin = 0; cachedCoefScalarMax = 1;
    cachedPhysScalarMin = 0; cachedPhysScalarMax = 1;
    cachedCoeffNj = cachedCoeffNi = 0;
    needsIsoRebuild = true;
    isoSegments.clear();
}

void SalinityOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedBaseGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedBaseGrid[j];
        cachedBaseGrid.reset();
    }
    if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
    Init();
}

void SalinityOverlay::clearCache() {
    if (cachedBaseGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedBaseGrid[j];
        cachedBaseGrid.reset();
    }
    cachedNj = cachedNi = 0;
    if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
    dataReady = false;
    needsRebuild = true;
    needsIsoRebuild = true;
    // GPU textures preserved: glTexture, lutID, uploadBuf
}

void SalinityOverlay::clearCoefficients() {
    ncdfLog("[clearCoeff:sal] cachedBaseGrid=%p cachedCoeff=%p cachedNj=%d cachedNi=%d\n",
        (void*)cachedBaseGrid.get(), (void*)cachedCoeff, cachedNj, cachedNi);
    if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
    // Delete GPU texture — it contains old coefficient data, must be recreated
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); glTexture = 0; hasTexture = false; }
    // Grid (cachedBaseGrid) preserved for reuse in same-file time step switch
    dataReady = false;
    gridReady = false;
    needsRebuild = true;
    needsIsoRebuild = true;
}

void SalinityOverlay::ensureGridAllocated(int ni, int nj) {
    if (cachedBaseGrid && cachedNj == nj && cachedNi == ni) return;
    if (cachedBaseGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedBaseGrid[j];
        cachedBaseGrid.reset();
    }
    auto src = std::make_unique<float*[]>(nj);
    for (int j = 0; j < nj; j++) src[j] = new float[ni];
    cachedBaseGrid = std::move(src);
    cachedNj = nj; cachedNi = ni;
    ncdfLog("[prealloc:sal] allocated %dx%d\n", ni, nj);
}

void SalinityOverlay::prepareGrid(MainDialog *gui) {
    if (!gui || !gui->myMessage.hasSalData()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    auto t0 = std::chrono::high_resolution_clock::now();
    if (!cachedBaseGrid || cachedNj != nj || cachedNi != ni) {
        ncdfLog("[prepare:sal] grid MISS — allocating %dx%d\n", ni, nj);
        if (cachedBaseGrid) { for (int j = 0; j < cachedNj; j++) delete[] cachedBaseGrid[j]; cachedBaseGrid.reset(); }
        auto src = std::make_unique<float*[]>(nj);
        for (int j = 0; j < nj; j++) {
            src[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double val = gui->myMessage.getSal(i, j);
                src[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
            }
        }
        cachedBaseGrid = std::move(src);
        cachedNj = nj; cachedNi = ni;
    } else {
        ncdfLog("[prepare:sal] grid HIT — reusing %dx%d\n", ni, nj);
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                double val = gui->myMessage.getSal(i, j);
                cachedBaseGrid[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
            }
    }
    double dMin = 1e30, dMax = -1e30;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float v = cachedBaseGrid[j][i];
            if (isfinite(v)) { if (v < dMin) dMin = v; if (v > dMax) dMax = v; }
        }
    cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
    cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    gridReady = true;
    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] Salinity::prepareGrid ni=%d nj=%d %lldms\n", ni, nj,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

void SalinityOverlay::prepareCoeff(ncdfOverlayFactory *factory) {
    if (!cachedBaseGrid || !gridReady) return;
    int ni = cachedNi, nj = cachedNj;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (!cachedCoeff || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        ncdfLog("[prepare:sal] coeff MISS — allocating\n");
        if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
        cachedCoeff = (float*)calloc(ni * nj, sizeof(float));
    } else {
        ncdfLog("[prepare:sal] coeff HIT — overwriting\n");
    }
    if (cachedCoeff) {
        float pMin = 1e30f, pMax = -1e30f;
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float v = cachedBaseGrid[j][i];
                if (isfinite(v)) { if (v < pMin) pMin = v; if (v > pMax) pMax = v; }
            }
        ncdfOverlayFactory::PrefilterScalarCoefficients(cachedCoeff, cachedBaseGrid.get(),
            ni, nj, false, cachedCoefScalarMin, cachedCoefScalarMax, pMin, pMax);
        cachedCoeffNj = nj; cachedCoeffNi = ni;
        cachedPhysScalarMin = pMin; cachedPhysScalarMax = pMax;
        const float bs = 2.5858f;
        cachedCoefMin = cachedCoefScalarMin * bs;
        cachedCoefMax = cachedCoefScalarMax * bs;
        cachedPhysMin = pMin; cachedPhysMax = pMax;
    }
    dataReady = true;
    needsRebuild = true;
    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] Salinity::prepareCoeff ni=%d nj=%d %lldms\n", ni, nj,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

void SalinityOverlay::releaseCoeffData() {
    if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
    ncdfLog("[release:sal] coefficients released, grid preserved\n");
}

void SalinityOverlay::Invalidate() {
    ncdfLog("[invalidate:sal] needsRebuild was=%d hasLUT=%d hasTex=%d\n", (int)needsRebuild, (int)hasLUT, (int)hasTexture);
    needsRebuild = true;
    needsIsoRebuild = true;
    // Don't delete GL textures here — setData() may run on a non-GL thread.
    // LUT will be recreated in BuildLUTTex on next render (render thread has GL context).
    hasLUT = false;  // Mark stale; render thread will glDeleteTextures + recreate
}

void SalinityOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    ncdfLog("[perf] Salinity::prepareData ENTER, gui=%p hasSal=%d sal.size=%zu\n",
        (void*)gui, gui ? (int)gui->myMessage.hasSalData() : -1,
        gui ? gui->myMessage.salinity.size() : 0);
    if (!gui || !gui->myMessage.hasSalData()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    ncdfLog("[perf] Salinity::prepareData ni=%d nj=%d\n", ni, nj);
    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Build base grid as float (reuse existing allocation for same-file switch)
    if (!cachedBaseGrid || cachedNj != nj || cachedNi != ni) {
        ncdfLog("[prepare:sal] grid cache MISS — allocating %dx%d\n", ni, nj);
        if (cachedBaseGrid) {
            for (int j = 0; j < cachedNj; j++) delete[] cachedBaseGrid[j];
            cachedBaseGrid.reset();
        }
        auto src = std::make_unique<float*[]>(nj);
        for (int j = 0; j < nj; j++) {
            src[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double val = gui->myMessage.getSal(i, j);
                src[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
            }
        }
        cachedBaseGrid = std::move(src);
        cachedNj = nj; cachedNi = ni;
    } else {
        // Grid cache HIT — reuse existing allocation, just overwrite values
        ncdfLog("[prepare:sal] grid cache HIT — reusing %dx%d\n", ni, nj);
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                double val = gui->myMessage.getSal(i, j);
                cachedBaseGrid[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
            }
        }
    }

    // Compute data range from base grid
    double dMin = 1e30, dMax = -1e30;
    for (int jj = 0; jj < nj; jj++)
        for (int ii = 0; ii < ni; ii++) {
            float v = cachedBaseGrid[jj][ii];
            if (isfinite(v)) {
                if (v < dMin) dMin = v; if (v > dMax) dMax = v;
            }
        }
    cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
    cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    auto t1 = std::chrono::high_resolution_clock::now();

    // 2. Compute B-spline coefficients directly from float base grid
    bool coeffRebuilt = false;
    if (!cachedCoeff || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        ncdfLog("[prepare:sal] coeff cache MISS — cachedCoeff=%p cachedNj=%d cachedNi=%d vs nj=%d ni=%d\n",
            (void*)cachedCoeff, cachedCoeffNj, cachedCoeffNi, nj, ni);
        if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
        float *coeffBuf = (float*)calloc(ni * nj, sizeof(float));
        unsigned char *byteMask = (unsigned char*)malloc(ni * nj);
        if (coeffBuf && byteMask && cachedBaseGrid) {
            float pMin = 1e30f, pMax = -1e30f;
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    int k = j * ni + i;
                    float val = cachedBaseGrid[j][i];
                    if (!isfinite(val)) {
                        coeffBuf[k] = 0; byteMask[k] = 0;
                    } else {
                        coeffBuf[k] = val; byteMask[k] = 1;
                        if (val < pMin) pMin = val;
                        if (val > pMax) pMax = val;
                    }
                }
            cachedPhysScalarMin = pMin;
            cachedPhysScalarMax = pMax;

            float *rawBuf = (float*)malloc(ni * nj * sizeof(float));
            if (rawBuf) {
                memcpy(rawBuf, coeffBuf, ni * nj * sizeof(float));
                double lonMin = wxMin(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
                double lonMax = wxMax(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
                double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
                bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);
                BicubicSplinePrefilterWrapAware(coeffBuf, rawBuf, byteMask, ni, nj, repeat);
                free(rawBuf);

                float cMin = 1e30f, cMax = -1e30f;
                for (int k = 0; k < ni * nj; k++) {
                    if (!byteMask[k]) continue;
                    if (coeffBuf[k] < cMin) cMin = coeffBuf[k];
                    if (coeffBuf[k] > cMax) cMax = coeffBuf[k];
                }
                if (cMax <= cMin) { cMin = 0; cMax = 1; }
                cachedCoefScalarMin = cMin;
                cachedCoefScalarMax = cMax;
                const float bsplineScale = 2.5858f;
                cachedCoefMin = cMin * bsplineScale;
                cachedCoefMax = cMax * bsplineScale;
                cachedPhysMin = cachedPhysScalarMin;
                cachedPhysMax = cachedPhysScalarMax;
                cachedCoeff = coeffBuf;
                coeffBuf = NULL;
                cachedCoeffNj = nj;
                cachedCoeffNi = ni;
            }
        }
        if (coeffBuf) free(coeffBuf);
        if (byteMask) free(byteMask);
        coeffRebuilt = true;
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    dataReady = true;
    needsRebuild = coeffRebuilt;  // Only rebuild texture when data actually changed
    auto gridMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto bsplineMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t0).count();
    ncdfLog("[perf] Salinity::prepareData ni=%d nj=%d grid=%lldms bspline=%lldms total=%lldms\n",
        ni, nj, (long long)gridMs, (long long)bsplineMs, (long long)totalMs);
}

wxColour SalinityOverlay::GetColor(double sal_psu) {
    static const double stops[][4] = {
        {30.0, 120, 120, 255}, {31.0,  80,  80, 240}, {32.0,  40,  40, 220},
        {33.0,  20,  20, 200}, {33.5,   0,  60, 200}, {34.0,   0, 120, 180},
        {34.5,   0, 180, 140}, {35.0,   0, 200,  80}, {35.5,  40, 220,  40},
        {36.0, 120, 230,  20}, {36.5, 200, 220,  20}, {37.0, 240, 180,  20},
        {37.5, 240, 120,  20}, {38.0, 220,  60,  20}, {39.0, 200,  20,  20},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 15, sal_psu, false);
}

// Helper: fill grid from salinity data
static void FillSalGrid(MainDialog &gui, std::unique_ptr<double*[]> &grid, int nj, int ni) {
    auto src = std::make_unique<double*[]>(nj);
    for (int j = 0; j < nj; j++) {
        src[j] = new double[ni];
        for (int i = 0; i < ni; i++) src[j][i] = gui.myMessage.getSal(i, j);
    }
    grid = std::move(src);
}
static bool HasSalData(MainDialog &gui) { return gui.myMessage.hasSalData(); }

bool SalinityOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                      ncdfOverlayFactory *factory,
                                      double** animatedGrid) {
    extern void ncdfLog(const char* format, ...);
    ncdfLog("[SAL-ANIM] RenderColorMap: animatedGrid=%p hasTexture=%d needsRebuild=%d "
            "cachedCoeff=%p cachedCoefMin=%.3f cachedCoefMax=%.3f "
            "cachedPhysScalarMin=%.3f cachedPhysScalarMax=%.3f\n",
            (void*)animatedGrid, (int)hasTexture, (int)needsRebuild,
            (void*)cachedCoeff, cachedCoefMin, cachedCoefMax,
            cachedPhysScalarMin, cachedPhysScalarMax);
    ncdfOverlayFactory::ScalarOverlayState st = {
        &glTexture, &hasTexture, &needsRebuild,
        dataDim, glDim, &lutID, &hasLUT, &uploadBuf, &uploadBufSize,
        &cachedBaseGrid, &cachedNj, &cachedNi,
        &cachedDataMin, &cachedDataMax, &cachedCoefMin, &cachedCoefMax,
        &cachedPhysMin, &cachedPhysMax, &cachedPhysMinV, &cachedPhysMaxV,
        &cachedCoeff, &cachedCoefScalarMin, &cachedCoefScalarMax,
        &cachedPhysScalarMin, &cachedPhysScalarMax, &cachedCoeffNj, &cachedCoeffNi
    };
    bool ok = factory->RenderScalarColorMap(vp, gui, plugin, st,
        SalinityOverlay::GetColor, 30.0f, 39.0f,
        HasSalData, FillSalGrid,
        animatedGrid);
    // Release coefficient data after texture upload (skip during animation —
    // animation may produce NULL animatedGrid on early frames while still needing coefficients)
    bool animActive = plugin->m_settingsSalinity.animate ||
                      plugin->m_settingsCurrent.animate ||
                      plugin->m_settingsSeaTemp.animate;
    if (!animActive)
        releaseCoeffData();
    return ok;
}

void SalinityOverlay::RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui) {
    if (!gui || !vp || !gui->myMessage.hasSalData()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    double tlat = wxMax(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double blat = wxMin(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double tlon = wxMin(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    double blon = wxMax(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    // Build temporary 2D grid for isoline renderer
    double** tmpGrid = new double*[nj];
    for (int j = 0; j < nj; j++) {
        tmpGrid[j] = new double[ni];
        for (int i = 0; i < ni; i++) tmpGrid[j][i] = gui->myMessage.getSal(i, j);
    }
    ncdfOverlayFactory::DrawIsoLines(vp, tmpGrid, ni, nj,
                 needsIsoRebuild, isoSegments,
                 tlat, tlon, blat, blon,
                 gui->myMessage.jDirectionIncr);
    for (int j = 0; j < nj; j++) delete[] tmpGrid[j];
    delete[] tmpGrid;
}
