#include "ncdf_overlay_current.h"
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

bool CurrentOverlay::s_smoothColors = false;

void CurrentOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true; dataReady = false;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    physicalTexID = 0; hasPhysicalTex = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedGrid.reset(); cachedNj = cachedNi = 0;
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
    if (cachedGrid) {
        for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
        cachedGrid.reset();
    }
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

void CurrentOverlay::Invalidate() {
    needsRebuild = true;
    needsLICRebuild = true;
    if (hasPhysicalTex && physicalTexID) {
        glDeleteTextures(1, &physicalTexID);
        physicalTexID = 0;
    }
    hasPhysicalTex = false;
}

void CurrentOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    if (!gui || !gui->myMessage.hasCurrent()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    // 1. Build speed grid
    if (!cachedGrid) {
        auto src = std::make_unique<double*[]>(nj);
        for (int j = 0; j < nj; j++) {
            src[j] = new double[ni];
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || u != u || v != v)
                    src[j][i] = ncdf_NOTDEF;
                else
                    src[j][i] = sqrt(u * u + v * v);
            }
        }
        cachedGrid = std::move(src);
        cachedNj = nj; cachedNi = ni;

        if (plugin->m_settingsCurrent.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(cachedGrid.get(), nj, ni);
            if (ad) { for (int j = 0; j < nj; j++) delete[] cachedGrid[j]; cachedGrid.reset(ad); }
        }
        if (plugin->m_settingsCurrent.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(cachedGrid.get(), nj, ni);
            if (sh) { for (int j = 0; j < nj; j++) delete[] cachedGrid[j]; cachedGrid.reset(sh); }
        }

        // Compute data range
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

    // 2. Build U/V grids + B-spline coefficients
    if (!cachedU || !cachedV) {
        cachedU = new double*[nj]; cachedV = new double*[nj];
        for (int j = 0; j < nj; j++) {
            cachedU[j] = new double[ni]; cachedV[j] = new double[ni];
            for (int i = 0; i < ni; i++) {
                cachedU[j][i] = gui->myMessage.getU(i, j);
                cachedV[j][i] = gui->myMessage.getV(i, j);
            }
        }
        cachedUNj = nj; cachedVNi = ni;
    }

    if (!cachedCoeffU || !cachedCoeffV || cachedCoeffNj != nj || cachedCoeffNi != ni) {
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
        }
    }

    dataReady = true;
    needsRebuild = false;  // Data is ready — RenderColorMap will just create textures
    ncdfLog("[render] CurrentOverlay::prepareData done, ni=%d nj=%d\n", ni, nj);
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

    // If animated grid provided, use it directly (animation replaces static color map)
    if (animatedGrid) {
        // Release old owned grid (animatedGrid is not owned by us)
        if (cachedGrid) {
            for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j];
            cachedGrid.reset();
        }
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
    } else if (!cachedGrid) {
        return false;
    } else {
    // Compute speed grid on-the-fly from u/v (NaN-safe)
    // Also serves as cached grid for aniso diffusion / sharpening
    if (needsRebuild || !cachedGrid) {
        // Only rebuild from myMessage if cachedGrid doesn't exist yet
        if (!cachedGrid) {
            cachedNj = nj; cachedNi = ni;
            auto src = std::make_unique<double*[]>(nj);
            for (int j = 0; j < nj; j++) {
                src[j] = new double[ni];
                for (int i = 0; i < ni; i++) {
                    double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                    if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || u != u || v != v)
                        src[j][i] = ncdf_NOTDEF;
                    else
                        src[j][i] = sqrt(u * u + v * v);
                }
            }
            cachedGrid = std::move(src);
        }
        if (plugin->m_settingsCurrent.anisoDiffusion) {
            double** ad = ncdfOverlayFactory::BuildAnisoDiffusedGrid(cachedGrid.get(), nj, ni);
            if (ad) { for (int j = 0; j < nj; j++) delete[] cachedGrid[j]; cachedGrid.reset(ad); }
        }
        if (plugin->m_settingsCurrent.sharpen) {
            double** sh = ncdfOverlayFactory::BuildSharpenedGrid(cachedGrid.get(), nj, ni);
            if (sh) { for (int j=0;j<nj;j++) delete[] cachedGrid[j]; cachedGrid.reset(sh); }
        }

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
    if (plugin->m_settingsCurrent.slopeShading && cachedU && cachedV) {
        if (needsRebuild || !cachedVorticity) {
            if (cachedVorticity) { for (int j=0;j<vortNj;j++) delete[] cachedVorticity[j]; }
            cachedVorticity.reset();
            // Build temporary 2D grids for vorticity computation
            double** tmpU = new double*[nj], **tmpV = new double*[nj];
            for (int j = 0; j < nj; j++) {
                tmpU[j] = new double[ni]; tmpV[j] = new double[ni];
                for (int i = 0; i < ni; i++) { tmpU[j][i] = gui->myMessage.getU(i, j); tmpV[j][i] = gui->myMessage.getV(i, j); }
            }
            cachedVorticity.reset(ncdfOverlayFactory::BuildVorticityGrid(tmpU, tmpV, nj, ni));
            for (int j = 0; j < nj; j++) { delete[] tmpU[j]; delete[] tmpV[j]; }
            delete[] tmpU; delete[] tmpV;
            vortNj = nj; vortNi = ni;
        }
        slopeGrid = cachedVorticity.get();
        // TODO: build vorticity texture for shader slope path
    }

    // Build shader settings
    ncdfOverlayFactory::RenderSettings settings;
    settings.smoothColors = plugin->m_settingsCurrent.smoothColors;

    // Vector mode: pass u/v grids for shader-side speed computation
    if (animUGrid && animVGrid) {
        // Animation mode: use advected u/v grids for vector interpolation
        settings.vectorMode = true;
        settings.uGrid = animUGrid;
        settings.vGrid = animVGrid;
    } else if (!animatedGrid && cachedU && cachedV) {
        // Static mode: use cached u/v grids (only rebuild when data changes)
        settings.vectorMode = true;
        if (needsRebuild || !cachedU || !cachedV) {
            // Only rebuild from myMessage if cachedU/V don't exist yet
            if (!cachedU || !cachedV) {
                if (cachedU) { for (int j = 0; j < cachedNj; j++) delete[] cachedU[j]; delete[] cachedU; }
                if (cachedV) { for (int j = 0; j < cachedNj; j++) delete[] cachedV[j]; delete[] cachedV; }
                cachedU = new double*[nj]; cachedV = new double*[nj];
                for (int j = 0; j < nj; j++) {
                    cachedU[j] = new double[ni]; cachedV[j] = new double[ni];
                    for (int i = 0; i < ni; i++) {
                        cachedU[j][i] = gui->myMessage.getU(i, j);
                        cachedV[j][i] = gui->myMessage.getV(i, j);
                    }
                }
                cachedUNj = nj; cachedVNi = ni;
            }
            // Only invalidate coefficients if dimensions changed
            if (cachedCoeffNj != nj || cachedCoeffNi != ni) {
                if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
                if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
                cachedCoeffNj = cachedCoeffNi = 0;
            }
        }

        // B-spline prefilter: only when coefficients don't exist yet
        if (!cachedCoeffU || !cachedCoeffV || cachedCoeffNj != nj || cachedCoeffNi != ni) {
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
            }
        }
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
        double dMin = 1e30, dMax = -1e30;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                double u = settings.uGrid[jj][ii], v = settings.vGrid[jj][ii];
                if (u != ncdf_NOTDEF && v != ncdf_NOTDEF && isfinite(u) && isfinite(v)) {
                    double spd = sqrt(u*u + v*v);
                    if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    }

    // Use cached coefMin/coefMax if texture exists (set by RenderGridOverlay on first frame)
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
        settings.physMin = 0.0f;
        settings.physMax = 1.0f;
        settings.physMinV = 0.0f;
        settings.physMaxV = 1.0f;
    }
    settings.lutMin = 0.0f;    // Current speed color stops range
    settings.lutMax = 1.5f;

    s_smoothColors = settings.smoothColors;
    double** renderGrid = animatedGrid ? animatedGrid : cachedGrid.get();
    factory->RenderGridOverlay(vp, renderGrid,
        CurrentOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
        slopeGrid, hasVortTexture ? vortTexture : 0,
        &physicalTexID, &hasPhysicalTex);

    // Texture is now on GPU — release CPU-side data
    if (hasTexture) {
        cachedCoefMin = settings.dataMin;
        cachedCoefMax = settings.dataMax;
        cachedDataMinV = settings.dataMinV;
        cachedDataMaxV = settings.dataMaxV;
        cachedPhysMin = settings.physMin;
        cachedPhysMax = settings.physMax;
        cachedPhysMinV = settings.physMinV;
        cachedPhysMaxV = settings.physMaxV;
        // Free CPU caches — data is now on GPU
        if (cachedGrid) { for (int j = 0; j < cachedNj; j++) delete[] cachedGrid[j]; cachedGrid.reset(); }
        if (cachedU) { for (int j = 0; j < cachedNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
        if (cachedV) { for (int j = 0; j < cachedNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
        if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
        if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    }

    return true;
}
