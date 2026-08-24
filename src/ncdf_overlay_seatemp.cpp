#include "ncdf_overlay_seatemp.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"
#include "shader/ncdf_shader.h"
#include <cmath>

bool SeaTempOverlay::s_smoothColors = false;

void SeaTempOverlay::Init() {
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

void SeaTempOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        cachedGrid.reset();
    }
    Init();
}

void SeaTempOverlay::Invalidate() {
    needsRebuild = true;
    needsIsoRebuild = true;
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
