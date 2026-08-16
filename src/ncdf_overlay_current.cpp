#include "ncdf_overlay_current.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "ncdfoverlayfactory.h"
#include "shader/ncdf_shader.h"
#include <cmath>

bool CurrentOverlay::s_smoothColors = false;

void CurrentOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedGrid = NULL; cachedNj = cachedNi = 0; cachedOwns = false;
    cachedVorticity = NULL; vortNj = vortNi = 0;
    vortTexture = 0; hasVortTexture = false;
    slopeSource = 0;
    licTexture = 0; hasLICTexture = false; needsLICRebuild = true;
}

void CurrentOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedOwns && cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        delete[] cachedGrid;
    }
    cachedGrid = NULL;
    if (cachedVorticity) {
        for (int j = 0; j < vortNj; j++) delete[] cachedVorticity[j];
        delete[] cachedVorticity;
        cachedVorticity = NULL;
    }
    if (hasVortTexture && vortTexture) { glDeleteTextures(1, &vortTexture); }
    if (hasLICTexture && licTexture) { glDeleteTextures(1, &licTexture); }
    Init();
}

void CurrentOverlay::Invalidate() {
    needsRebuild = true;
    needsLICRebuild = true;
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
                                     ncdfOverlayFactory *factory, bool useShader, int interpMode) {
    if (!gui || !gui->gridu || !gui->gridv || !gui->gridMag) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Build cached grid if needed
    if (needsRebuild || !cachedGrid) {
        if (cachedOwns) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; delete[] cachedGrid; }
        cachedGrid = NULL; cachedOwns = false;
        double** src = gui->gridMag;
        if (plugin->m_settingsCurrent.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(src, nj, ni);
            if (ad) { src = ad; cachedGrid = ad; cachedOwns = true; }
        }
        if (plugin->m_settingsCurrent.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(src, nj, ni);
            if (sh) { if (cachedOwns) { for (int j=0;j<cachedNj;j++) delete[] cachedGrid[j]; delete[] cachedGrid; } cachedGrid = sh; cachedOwns = true; src = sh; }
        }
        if (!cachedGrid) { cachedGrid = gui->gridMag; cachedOwns = false; }
        cachedNj = nj; cachedNi = ni;
    }

    // Slope shading (vorticity)
    double** slopeGrid = NULL;
    slopeSource = glTexture;
    if (plugin->m_settingsCurrent.slopeShading && gui->gridu && gui->gridv) {
        if (needsRebuild || !cachedVorticity) {
            if (cachedVorticity) { for (int j=0;j<vortNj;j++) delete[] cachedVorticity[j]; delete[] cachedVorticity; }
            cachedVorticity = ncdfOverlayFactory::BuildVorticityGrid(gui->gridu, gui->gridv, nj, ni);
            vortNj = nj; vortNi = ni;
        }
        slopeGrid = cachedVorticity;
        // TODO: build vorticity texture for shader slope path
    }

    // Build shader settings
    ncdfOverlayFactory::RenderSettings settings;
    settings.useShader = useShader;
    settings.interpMode = interpMode;
    settings.smoothColors = plugin->m_settingsCurrent.smoothColors;
    settings.sCurve = plugin->m_settingsCurrent.sCurve;
    settings.slopeShading = plugin->m_settingsCurrent.slopeShading;
    settings.slopeMode = 0;  // continuous gradient for current

    // Compute data range for slope shading
    if (settings.slopeShading && gui->gridMag) {
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double v = gui->gridMag[jj][ii];
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
        CurrentOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
        slopeGrid, hasVortTexture ? vortTexture : 0);

    return true;
}
