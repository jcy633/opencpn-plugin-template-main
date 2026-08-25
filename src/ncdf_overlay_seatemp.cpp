#include "ncdf_overlay_seatemp.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"
#include "shader/ncdf_shader.h"
#include <cmath>

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
    cachedGrid.reset(); cachedNj = cachedNi = 0;
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
    if (cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        cachedGrid.reset();
    }
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
    // Clear processed grid — will be rebuilt from cachedBaseGrid in RenderColorMap
    if (cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        cachedGrid.reset();
    }
}

void SeaTempOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    if (!gui || !gui->myMessage.hasSSTData()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // 1. Build SST grid
    if (!cachedBaseGrid) {
        auto src = std::make_unique<double*[]>(nj);
        for (int j = 0; j < nj; j++) {
            src[j] = new double[ni];
            for (int i = 0; i < ni; i++) src[j][i] = gui->myMessage.getSST(i, j);
        }
        cachedBaseGrid = std::move(src);
        cachedNj = nj; cachedNi = ni;
    }
    // Always rebuild filtered grid from base grid (new data or settings change)
    if (cachedGrid) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; cachedGrid.reset(); }
    {
        cachedGrid.reset(new double*[nj]);
        for (int j = 0; j < nj; j++) {
            cachedGrid[j] = new double[ni];
            memcpy(cachedGrid[j], cachedBaseGrid[j], ni * sizeof(double));
        }
        if (plugin->m_settingsSeaTemp.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(cachedGrid.get(), nj, ni);
            if (ad) { for (int j = 0; j < nj; j++) delete[] cachedGrid[j]; cachedGrid.reset(ad); }
        }
        if (plugin->m_settingsSeaTemp.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(cachedGrid.get(), nj, ni);
            if (sh) { for (int j = 0; j < nj; j++) delete[] cachedGrid[j]; cachedGrid.reset(sh); }
        }

        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = cachedGrid[jj][ii];
                if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                    if (v < dMin) dMin = v; if (v > dMax) dMax = v;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    }

    // 2. Compute B-spline coefficients
    if (!cachedCoeff || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        if (cachedCoeff) { free(cachedCoeff); cachedCoeff = NULL; }
        float *coeffBuf = (float*)calloc(ni * nj, sizeof(float));
        unsigned char *byteMask = (unsigned char*)malloc(ni * nj);
        if (coeffBuf && byteMask && cachedGrid) {
            float *rawBuf = (float*)calloc(ni * nj, sizeof(float));
            if (rawBuf) {
                float pMin = 1e30f, pMax = -1e30f;
                for (int j = 0; j < nj; j++)
                    for (int i = 0; i < ni; i++) {
                        int k = j * ni + i;
                        double val = cachedGrid[j][i];
                        if (val == ncdf_NOTDEF || isnan(val) || !isfinite(val)) {
                            rawBuf[k] = 0; byteMask[k] = 0;
                        } else {
                            rawBuf[k] = (float)val; byteMask[k] = 1;
                            if (rawBuf[k] < pMin) pMin = rawBuf[k];
                            if (rawBuf[k] > pMax) pMax = rawBuf[k];
                        }
                    }
                cachedPhysScalarMin = pMin;
                cachedPhysScalarMax = pMax;

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
    }

    dataReady = true;
    needsRebuild = false;  // Data is ready — RenderColorMap will just create textures
    ncdfLog("[render] SeaTempOverlay::prepareData done, ni=%d nj=%d\n", ni, nj);
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
        &cachedGrid, &cachedBaseGrid, &cachedNj, &cachedNi,
        &cachedDataMin, &cachedDataMax, &cachedCoefMin, &cachedCoefMax,
        &cachedPhysMin, &cachedPhysMax, &cachedPhysMinV, &cachedPhysMaxV,
        &cachedCoeff, &cachedCoefScalarMin, &cachedCoefScalarMax,
        &cachedPhysScalarMin, &cachedPhysScalarMax, &cachedCoeffNj, &cachedCoeffNi
    };
    bool ok = factory->RenderScalarColorMap(vp, gui, plugin, st,
        SeaTempOverlay::GetColor, -2.0f, 32.0f,
        HasSSTData, FillSSTGrid,
        plugin->m_settingsSeaTemp.smoothColors,
        plugin->m_settingsSeaTemp.sharpen,
        plugin->m_settingsSeaTemp.anisoDiffusion,
        animatedGrid);
    s_smoothColors = plugin->m_settingsSeaTemp.smoothColors;
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
