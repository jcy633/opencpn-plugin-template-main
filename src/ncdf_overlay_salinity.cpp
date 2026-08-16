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
    cachedGrid = NULL; cachedNj = cachedNi = 0; cachedOwns = false;
    needsIsoRebuild = true;
    isoSegments.clear();
}

void SalinityOverlay::Cleanup() {
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

bool SalinityOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                      ncdfOverlayFactory *factory, bool useShader, int interpMode) {
    if (!gui || !gui->gridSalinity || !gui->hasSalinity) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Build cached grid if needed
    if (needsRebuild || !cachedGrid) {
        if (cachedOwns) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; delete[] cachedGrid; }
        cachedGrid = NULL; cachedOwns = false;
        double** src = gui->gridSalinity;
        if (plugin->m_settingsSalinity.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(src, nj, ni);
            if (ad) { src = ad; cachedGrid = ad; cachedOwns = true; }
        }
        if (plugin->m_settingsSalinity.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(src, nj, ni);
            if (sh) { if (cachedOwns) { for (int j=0;j<cachedNj;j++) delete[] cachedGrid[j]; delete[] cachedGrid; } cachedGrid = sh; cachedOwns = true; src = sh; }
        }
        if (!cachedGrid) { cachedGrid = gui->gridSalinity; cachedOwns = false; }
        cachedNj = nj; cachedNi = ni;
    }

    ncdfOverlayFactory::RenderSettings settings;
    settings.useShader = useShader;
    settings.interpMode = interpMode;
    settings.smoothColors = plugin->m_settingsSalinity.smoothColors;
    settings.sCurve = plugin->m_settingsSalinity.sCurve;
    settings.slopeShading = plugin->m_settingsSalinity.slopeShading;
    settings.slopeMode = 0;  // continuous Sobel for salinity

    if (settings.slopeShading && gui->gridSalinity) {
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = gui->gridSalinity[jj][ii];
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
        SalinityOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize);

    return true;
}

void SalinityOverlay::RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui) {
    if (!gui || !vp || !gui->gridSalinity) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    double tlat = wxMax(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double blat = wxMin(gui->myMessage.firstGridPointLat, gui->myMessage.lastGridPointLat);
    double tlon = wxMin(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    double blon = wxMax(gui->myMessage.firstGridPointLong, gui->myMessage.lastGridPointLong);
    ncdfOverlayFactory::DrawIsoLines(vp, gui->gridSalinity, ni, nj,
                 needsIsoRebuild, isoSegments,
                 tlat, tlon, blat, blon,
                 gui->myMessage.jDirectionIncr);
}
