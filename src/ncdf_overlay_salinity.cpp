#include "ncdf_overlay_salinity.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"
#include "shader/ncdf_shader.h"
#include <cmath>

bool SalinityOverlay::s_smoothColors = false;

void SalinityOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedGrid.reset(); cachedNj = cachedNi = 0;
    cachedDataMin = 0; cachedDataMax = 1;
    cachedCoefMin = 0; cachedCoefMax = 1;
    cachedPhysMin = 0; cachedPhysMax = 1;
    cachedPhysMinV = 0; cachedPhysMaxV = 1;
    needsIsoRebuild = true;
    isoSegments.clear();
}

void SalinityOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        cachedGrid.reset();
    }
    Init();
}

void SalinityOverlay::Invalidate() {
    needsRebuild = true;
    needsIsoRebuild = true;
}

wxColour SalinityOverlay::GetColor(double sal_psu) {
    static const double stops[][4] = {
        {30.0, 120, 120, 255}, {31.0,  80,  80, 240}, {32.0,  40,  40, 220},
        {33.0,  20,  20, 200}, {33.5,   0,  60, 200}, {34.0,   0, 120, 180},
        {34.5,   0, 180, 140}, {35.0,   0, 200,  80}, {35.5,  40, 220,  40},
        {36.0, 120, 230,  20}, {36.5, 200, 220,  20}, {37.0, 240, 180,  20},
        {37.5, 240, 120,  20}, {38.0, 220,  60,  20}, {39.0, 200,  20,  20},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 15, sal_psu, s_smoothColors);
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
                                      ncdfOverlayFactory *factory, bool useShader,
                                      double** animatedGrid) {
    ncdfOverlayFactory::ScalarOverlayState st = {
        &glTexture, &hasTexture, &needsRebuild,
        dataDim, glDim, &lutID, &hasLUT, &uploadBuf, &uploadBufSize,
        &cachedGrid, &cachedNj, &cachedNi,
        &cachedDataMin, &cachedDataMax, &cachedCoefMin, &cachedCoefMax,
        &cachedPhysMin, &cachedPhysMax, &cachedPhysMinV, &cachedPhysMaxV
    };
    bool ok = factory->RenderScalarColorMap(vp, gui, plugin, useShader, st,
        SalinityOverlay::GetColor, 30.0f, 39.0f,
        HasSalData, FillSalGrid,
        plugin->m_settingsSalinity.smoothColors,
        plugin->m_settingsSalinity.sharpen,
        plugin->m_settingsSalinity.anisoDiffusion,
        animatedGrid);
    s_smoothColors = plugin->m_settingsSalinity.smoothColors;
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
