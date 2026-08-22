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
    cachedDataMin = 0; cachedDataMax = 1;
    cachedCoefMin = 0; cachedCoefMax = 1;
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
                                     ncdfOverlayFactory *factory, bool useShader,
                                     double** animatedGrid, double** animUGrid, double** animVGrid) {
    if (!gui) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // If animated grid provided, use it directly (animation replaces static color map)
    if (animatedGrid) {
        if (cachedOwns && cachedGrid) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; delete[] cachedGrid; }
        cachedGrid = animatedGrid;
        cachedOwns = false;  // owned by ncdfAnimate
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

        // Store animated u/v grids for vector mode texture upload
        // (will be used in settings setup below)
    } else if (!gui->myMessage.hasCurrent()) {
        return false;
    } else {
    // Compute speed grid on-the-fly from u/v (NaN-safe)
    // Also serves as cached grid for aniso diffusion / sharpening
    if (needsRebuild || !cachedGrid) {
        if (cachedOwns) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; delete[] cachedGrid; }
        cachedGrid = NULL; cachedOwns = false;

        // Build speed grid: sqrt(u² + v²), NaN if either component is invalid
        double** speedGrid = new double*[nj];
        for (int j = 0; j < nj; j++) {
            speedGrid[j] = new double[ni];
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || u != u || v != v)
                    speedGrid[j][i] = ncdf_NOTDEF;
                else
                    speedGrid[j][i] = sqrt(u * u + v * v);
            }
        }
        double** src = speedGrid;
        if (plugin->m_settingsCurrent.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(src, nj, ni);
            if (ad) { src = ad; cachedGrid = ad; cachedOwns = true; }
        }
        if (plugin->m_settingsCurrent.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(src, nj, ni);
            if (sh) { if (cachedOwns) { for (int j=0;j<cachedNj;j++) delete[] cachedGrid[j]; delete[] cachedGrid; } cachedGrid = sh; cachedOwns = true; src = sh; }
        }
        if (!cachedGrid) { cachedGrid = speedGrid; cachedOwns = true; }
        cachedNj = nj; cachedNi = ni;

        // Cache data range during rebuild (not every frame)
        if (cachedGrid) {
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
    }
    } // end else (not animated)

    // Slope shading (vorticity)
    double** slopeGrid = NULL;
    slopeSource = glTexture;
    if (plugin->m_settingsCurrent.slopeShading && gui->myMessage.hasCurrent()) {
        if (needsRebuild || !cachedVorticity) {
            if (cachedVorticity) { for (int j=0;j<vortNj;j++) delete[] cachedVorticity[j]; delete[] cachedVorticity; }
            // Build temporary 2D grids for vorticity computation
            double** tmpU = new double*[nj], **tmpV = new double*[nj];
            for (int j = 0; j < nj; j++) {
                tmpU[j] = new double[ni]; tmpV[j] = new double[ni];
                for (int i = 0; i < ni; i++) { tmpU[j][i] = gui->myMessage.getU(i, j); tmpV[j][i] = gui->myMessage.getV(i, j); }
            }
            cachedVorticity = ncdfOverlayFactory::BuildVorticityGrid(tmpU, tmpV, nj, ni);
            for (int j = 0; j < nj; j++) { delete[] tmpU[j]; delete[] tmpV[j]; }
            delete[] tmpU; delete[] tmpV;
            vortNj = nj; vortNi = ni;
        }
        slopeGrid = cachedVorticity;
        // TODO: build vorticity texture for shader slope path
    }

    // Build shader settings
    ncdfOverlayFactory::RenderSettings settings;
    settings.useShader = useShader;
    settings.smoothColors = plugin->m_settingsCurrent.smoothColors;

    // Vector mode: pass u/v grids for shader-side speed computation
    double** tmpU = NULL, **tmpV = NULL;
    if (animUGrid && animVGrid) {
        // Animation mode: use advected u/v grids for vector interpolation
        settings.vectorMode = true;
        settings.uGrid = animUGrid;
        settings.vGrid = animVGrid;
    } else if (!animatedGrid && gui->myMessage.hasCurrent()) {
        // Static mode: build u/v grids from source data
        settings.vectorMode = true;
        tmpU = new double*[nj]; tmpV = new double*[nj];
        for (int j = 0; j < nj; j++) {
            tmpU[j] = new double[ni]; tmpV[j] = new double[ni];
            for (int i = 0; i < ni; i++) {
                tmpU[j][i] = gui->myMessage.getU(i, j);
                tmpV[j][i] = gui->myMessage.getV(i, j);
            }
        }
        settings.uGrid = tmpU;
        settings.vGrid = tmpV;
        // Compute speed range for shader normalization
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double u = tmpU[jj][ii], v = tmpV[jj][ii];
                if (u != ncdf_NOTDEF && v != ncdf_NOTDEF && isfinite(u) && isfinite(v)) {
                    double spd = sqrt(u*u + v*v);
                    if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    } else {
        settings.vectorMode = false;
        settings.uGrid = NULL;
        settings.vGrid = NULL;
    }

    // Use cached coefMin/coefMax if texture exists (set by RenderGridOverlay on first frame)
    if (hasTexture) {
        settings.dataMin = cachedCoefMin;
        settings.dataMax = cachedCoefMax;
    } else {
        settings.dataMin = cachedDataMin;
        settings.dataMax = cachedDataMax;
    }
    settings.lutMin = 0.0f;    // Current speed color stops range
    settings.lutMax = 1.5f;

    s_smoothColors = settings.smoothColors;
    factory->RenderGridOverlay(vp, cachedGrid,
        CurrentOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
        slopeGrid, hasVortTexture ? vortTexture : 0);

    // Cache coefMin/coefMax from RenderGridOverlay for subsequent frames
    if (hasTexture) {
        cachedCoefMin = settings.dataMin;
        cachedCoefMax = settings.dataMax;
    }

    // Free temporary u/v grids
    if (tmpU) { for (int j = 0; j < nj; j++) delete[] tmpU[j]; delete[] tmpU; }
    if (tmpV) { for (int j = 0; j < nj; j++) delete[] tmpV[j]; delete[] tmpV; }

    return true;
}
