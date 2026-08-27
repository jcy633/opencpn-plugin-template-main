#include "ncdf_overlay_seatemp.h"
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

bool SeaTempOverlay::s_smoothColors = false;

void SeaTempOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true; dataReady = false;
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

void SeaTempOverlay::Cleanup() {
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

void SeaTempOverlay::Invalidate() {
    needsRebuild = true;
    needsIsoRebuild = true;
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); lutID = 0; }
    hasLUT = false;
}

void SeaTempOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    ncdfLog("[perf] SeaTemp::prepareData ENTER, gui=%p hasSST=%d sst.size=%zu\n",
        (void*)gui, gui ? (int)gui->myMessage.hasSSTData() : -1,
        gui ? gui->myMessage.sst.size() : 0);
    if (!gui || !gui->myMessage.hasSSTData()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    ncdfLog("[perf] SeaTemp::prepareData ni=%d nj=%d\n", ni, nj);
    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Build base grid as float (kept for first-frame texture build in RenderScalarColorMap)
    if (!cachedBaseGrid) {
        auto src = std::make_unique<float*[]>(nj);
        for (int j = 0; j < nj; j++) {
            src[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double val = gui->myMessage.getSST(i, j);
                src[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
            }
        }
        cachedBaseGrid = std::move(src);
        cachedNj = nj; cachedNi = ni;
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
    if (!cachedCoeff || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
        float *coeffBuf = (float*)calloc(ni * nj, sizeof(float));
        unsigned char *byteMask = (unsigned char*)malloc(ni * nj);
        if (coeffBuf && byteMask && cachedBaseGrid) {
            // Build mask directly from float grid (no double→float conversion needed)
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

            // B-spline prefilter: use coeffBuf as both input and output
            // First copy to rawBuf, prefilter into coeffBuf
            float *rawBuf = (float*)malloc(ni * nj * sizeof(float));
            if (rawBuf) {
                memcpy(rawBuf, coeffBuf, ni * nj * sizeof(float));
                double lonMin = wxMin(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
                double lonMax = wxMax(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
                double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
                bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);
                ncdfLog("[perf] SeaTemp: calling B-spline, ni=%d nj=%d repeat=%d total_cells=%d\n",
                    ni, nj, (int)repeat, ni*nj);
                auto bs0 = std::chrono::high_resolution_clock::now();
                BicubicSplinePrefilterWrapAware(coeffBuf, rawBuf, byteMask, ni, nj, repeat);
                auto bs1 = std::chrono::high_resolution_clock::now();
                ncdfLog("[perf] SeaTemp: B-spline done, %lldms\n",
                    (long long)std::chrono::duration_cast<std::chrono::milliseconds>(bs1 - bs0).count());
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
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    dataReady = true;
    needsRebuild = false;
    auto gridMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto bsplineMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t0).count();
    ncdfLog("[perf] SeaTemp::prepareData ni=%d nj=%d grid=%lldms bspline=%lldms total=%lldms\n",
        ni, nj, (long long)gridMs, (long long)bsplineMs, (long long)totalMs);
}

wxColour SeaTempOverlay::GetColor(double temp_c) {
    static const double stops[][4] = {
        {-2.0, 170, 170, 255}, {0.0,  120, 120, 240}, {2.0,  60,  60, 220},
        { 5.0,  20,  20, 200}, {8.0,   0, 100, 200}, {10.0,  0, 160, 180},
        {13.0,  0, 200, 120}, {16.0,  40, 220,  60}, {19.0, 120, 230,  20},
        {22.0, 200, 220,  20}, {25.0, 240, 180,  20}, {28.0, 240, 120,  20},
        {30.0, 220,  60,  20}, {32.0, 200,  20,  20},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 14, temp_c, s_smoothColors);
}

// Helper: fill grid from SST data
static void FillSSTGrid(MainDialog &gui, std::unique_ptr<double*[]> &grid, int nj, int ni) {
    auto src = std::make_unique<double*[]>(nj);
    for (int j = 0; j < nj; j++) {
        src[j] = new double[ni];
        for (int i = 0; i < ni; i++) src[j][i] = gui.myMessage.getSST(i, j);
    }
    grid = std::move(src);
}
static bool HasSSTData(MainDialog &gui) { return gui.myMessage.hasSSTData(); }

bool SeaTempOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                     ncdfOverlayFactory *factory,
                                     double** animatedGrid) {
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
        SeaTempOverlay::GetColor, -2.0f, 32.0f,
        HasSSTData, FillSSTGrid,
        false, false, false,
        animatedGrid);
    s_smoothColors = false;
    return ok;
}

void SeaTempOverlay::RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui) {
    if (!gui || !vp || !gui->myMessage.hasSSTData()) return;
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
        for (int i = 0; i < ni; i++) tmpGrid[j][i] = gui->myMessage.getSST(i, j);
    }
    ncdfOverlayFactory::DrawIsoLines(vp, tmpGrid, ni, nj,
                 needsIsoRebuild, isoSegments,
                 tlat, tlon, blat, blon,
                 gui->myMessage.jDirectionIncr);
    for (int j = 0; j < nj; j++) delete[] tmpGrid[j];
    delete[] tmpGrid;
}
