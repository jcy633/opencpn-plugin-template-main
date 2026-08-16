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
    cachedGrid = NULL; cachedNj = cachedNi = 0; cachedOwns = false;
    needsIsoRebuild = true;
    isoSegments.clear();
}

void SeaTempOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedOwns && cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        delete[] cachedGrid;
    }
    cachedGrid = NULL;
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

bool SeaTempOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                     ncdfOverlayFactory *factory, bool useShader, int interpMode) {
    if (!gui || !gui->gridSST || !gui->hasSeaTemp) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Build cached grid if needed
    if (needsRebuild || !cachedGrid) {
        if (cachedOwns) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; delete[] cachedGrid; }
        cachedGrid = NULL; cachedOwns = false;
        double** src = gui->gridSST;
        if (plugin->m_settingsSeaTemp.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(src, nj, ni);
            if (ad) { src = ad; cachedGrid = ad; cachedOwns = true; }
        }
        if (plugin->m_settingsSeaTemp.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(src, nj, ni);
            if (sh) { if (cachedOwns) { for (int j=0;j<cachedNj;j++) delete[] cachedGrid[j]; delete[] cachedGrid; } cachedGrid = sh; cachedOwns = true; src = sh; }
        }
        if (!cachedGrid) { cachedGrid = gui->gridSST; cachedOwns = false; }
        cachedNj = nj; cachedNi = ni;
    }

    ncdfOverlayFactory::RenderSettings settings;
    settings.useShader = useShader;
    settings.interpMode = interpMode;
    settings.smoothColors = plugin->m_settingsSeaTemp.smoothColors;
    settings.sCurve = plugin->m_settingsSeaTemp.sCurve;
    settings.slopeShading = plugin->m_settingsSeaTemp.slopeShading;
    settings.slopeMode = 1;  // SST temperature segments

    if (settings.slopeShading && gui->gridSST) {
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = gui->gridSST[jj][ii];
                if (v != ncdf_NOTDEF && v == v && isfinite(v)) {
                    if (v < dMin) dMin = v; if (v > dMax) dMax = v;
                }
            }
        settings.dataMin = (float)dMin;
        settings.dataMax = (float)dMax;
    } else {
        settings.dataMin = 0; settings.dataMax = 1;
    }

    s_smoothColors = settings.smoothColors;
    factory->RenderGridOverlay(vp, cachedGrid,
        SeaTempOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize);

    return true;
}

void SeaTempOverlay::RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui) {
    if (!gui || !vp || !gui->gridSST) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    double tlat = wxMax(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double blat = wxMin(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double tlon = wxMin(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    double blon = wxMax(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    ncdfOverlayFactory::DrawIsoLines(vp, gui->gridSST, ni, nj,
                 needsIsoRebuild, isoSegments,
                 tlat, tlon, blat, blon,
                 gui->myMessage.jDirectionIncr);
}
