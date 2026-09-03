#include "ncdf_overlay_current.h"
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


void CurrentOverlay::Init() {
    glTexture = 0; hasTexture = false; needsRebuild = true; dataReady = false;
    gridReady = false;
    dataDim[0] = dataDim[1] = 0;
    glDim[0] = glDim[1] = 0;
    lutID = 0; hasLUT = false;
    physicalTexID = 0; hasPhysicalTex = false;
    uploadBuf = NULL; uploadBufSize = 0;
    cachedNj = cachedNi = 0;
    cachedU = NULL; cachedV = NULL; cachedUNj = cachedVNi = 0;
    cachedCoeffU = NULL; cachedCoeffV = NULL;
    cachedCoefU_min = cachedCoefU_max = cachedCoefV_min = cachedCoefV_max = 0;
    cachedCoeffNj = cachedCoeffNi = 0;
    cachedDataMin = 0; cachedDataMax = 1;
    cachedCoefMin = 0; cachedCoefMax = 1;
    cachedPhysMin = 0; cachedPhysMax = 1;
    cachedDataMinV = 0; cachedDataMaxV = 1;
    cachedPhysMinV = 0; cachedPhysMaxV = 1;
    m_animUF = NULL; m_animVF = NULL; m_animNi = 0; m_animNj = 0;
    m_animCoeffU = NULL; m_animCoeffV = NULL;
    m_animCoefU_min = m_animCoefU_max = m_animCoefV_min = m_animCoefV_max = 0;
    m_animCoeffReady = false;
}

void CurrentOverlay::Cleanup() {
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); }
    if (hasLUT && lutID) { glDeleteTextures(1, &lutID); }
    if (hasPhysicalTex && physicalTexID) { glDeleteTextures(1, &physicalTexID); }
    if (uploadBuf) { free(uploadBuf); uploadBuf = NULL; }
    if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
    if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    freeAnimBuffers();
    if (m_animCoeffU) { free(m_animCoeffU); m_animCoeffU = NULL; }
    if (m_animCoeffV) { free(m_animCoeffV); m_animCoeffV = NULL; }
    m_animCoeffReady = false;
    Init();
}

void CurrentOverlay::clearCache() {
    if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
    if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    // Delete derived GPU textures — they contain view-specific data (gradients)
    if (hasPhysicalTex && physicalTexID) { glDeleteTextures(1, &physicalTexID); physicalTexID = 0; hasPhysicalTex = false; }
    dataReady = false;
    needsRebuild = true;
    // Color map texture (glTexture) preserved — can be updated in-place via glTexSubImage2D
}

void CurrentOverlay::clearCoefficients() {
    ncdfLog("[clearCoeff:cur] cachedU=%p cachedCoeffU=%p\n", (void*)cachedU, (void*)cachedCoeffU);
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    if (hasTexture && glTexture) { glDeleteTextures(1, &glTexture); glTexture = 0; hasTexture = false; }
    dataReady = false;
    gridReady = false;
    needsRebuild = true;
}

void CurrentOverlay::ensureGridAllocated(int ni, int nj) {
    if (cachedU && cachedV && cachedUNj == nj && cachedVNi == ni) return;
    // Free old grids if dimensions changed
    if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; cachedU = NULL; }
    if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; cachedV = NULL; }
    // Allocate new grids (values uninitialized — will be filled by prepareGrid)
    cachedU = new float*[nj]; cachedV = new float*[nj];
    for (int j = 0; j < nj; j++) { cachedU[j] = new float[ni]; cachedV[j] = new float[ni]; }
    cachedUNj = nj; cachedVNi = ni;
    ncdfLog("[prealloc:cur] allocated %dx%d\n", ni, nj);
}

void CurrentOverlay::prepareGrid(MainDialog *gui) {
    if (!gui || !gui->myMessage.hasCurrent()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    auto t0 = std::chrono::high_resolution_clock::now();
    if (!cachedU || !cachedV || cachedUNj != nj || cachedVNi != ni) {
        ncdfLog("[prepare:cur] grid MISS — allocating %dx%d\n", ni, nj);
        if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; }
        if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; }
        cachedU = new float*[nj]; cachedV = new float*[nj];
        for (int j = 0; j < nj; j++) {
            cachedU[j] = new float[ni]; cachedV[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                cachedU[j][i] = (u == ncdf_NOTDEF || !isfinite(u)) ? NAN : (float)u;
                cachedV[j][i] = (v == ncdf_NOTDEF || !isfinite(v)) ? NAN : (float)v;
            }
        }
        cachedUNj = nj; cachedVNi = ni;
    } else {
        ncdfLog("[prepare:cur] grid HIT — reusing %dx%d\n", ni, nj);
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                cachedU[j][i] = (u == ncdf_NOTDEF || !isfinite(u)) ? NAN : (float)u;
                cachedV[j][i] = (v == ncdf_NOTDEF || !isfinite(v)) ? NAN : (float)v;
            }
        }
    }
    // Compute speed range
    float dMin = 1e30f, dMax = -1e30f;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float u = cachedU[j][i], v = cachedV[j][i];
            if (isfinite(u) && isfinite(v)) {
                float spd = sqrtf(u * u + v * v);
                if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
            }
        }
    cachedDataMin = (dMin < dMax) ? dMin : 0;
    cachedDataMax = (dMin < dMax) ? dMax : 1;
    gridReady = true;
    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] Current::prepareGrid ni=%d nj=%d %lldms\n", ni, nj,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

void CurrentOverlay::prepareCoeff(ncdfOverlayFactory *factory) {
    if (!cachedU || !cachedV || !gridReady) return;
    int ni = cachedVNi, nj = cachedUNj;
    auto t0 = std::chrono::high_resolution_clock::now();

    // Always recompute coefficients (data values changed)
    if (!cachedCoeffU || !cachedCoeffV || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        ncdfLog("[prepare:cur] coeff MISS — allocating\n");
        if (cachedCoeffU) free(cachedCoeffU);
        if (cachedCoeffV) free(cachedCoeffV);
        cachedCoeffU = (float*)calloc(nj * ni, sizeof(float));
        cachedCoeffV = (float*)calloc(nj * ni, sizeof(float));
    } else {
        ncdfLog("[prepare:cur] coeff HIT — overwriting\n");
    }
    if (cachedCoeffU && cachedCoeffV) {
        double lonMin = factory->tlon, lonMax = factory->blon;
        double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
        bool isGlobal = (lonMax - lonMin + gridSpacingLon >= 360);
        ncdfOverlayFactory::PrefilterCoefficients(
            cachedCoeffU, cachedCoeffV, cachedU, cachedV,
            ni, nj, isGlobal,
            cachedCoefU_min, cachedCoefU_max, cachedCoefV_min, cachedCoefV_max);
        cachedCoeffNj = nj; cachedCoeffNi = ni;
        const float bsplineScale = 2.5858f;
        cachedCoefMin = cachedCoefU_min * bsplineScale;
        cachedCoefMax = cachedCoefU_max * bsplineScale;
    }
    dataReady = true;
    needsRebuild = true;
    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] Current::prepareCoeff ni=%d nj=%d %lldms\n", ni, nj,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
}

void CurrentOverlay::releaseCoeffData() {
    if (cachedCoeffU) { free(cachedCoeffU); cachedCoeffU = NULL; }
    if (cachedCoeffV) { free(cachedCoeffV); cachedCoeffV = NULL; }
    ncdfLog("[release:cur] coefficients released, grids preserved\n");
}

void CurrentOverlay::ensureAnimBuffers(int ni, int nj) {
    if (m_animUF && m_animVF && m_animNi == ni && m_animNj == nj) return;
    freeAnimBuffers();
    m_animUF = new float*[nj];
    m_animVF = new float*[nj];
    for (int j = 0; j < nj; j++) {
        m_animUF[j] = new float[ni];
        m_animVF[j] = new float[ni];
    }
    m_animNi = ni; m_animNj = nj;
    ncdfLog("[anim:cur] pre-allocated float buffers %dx%d\n", ni, nj);
}

void CurrentOverlay::freeAnimBuffers() {
    if (m_animUF) {
        for (int j = 0; j < m_animNj; j++) delete[] m_animUF[j];
        delete[] m_animUF; m_animUF = NULL;
    }
    if (m_animVF) {
        for (int j = 0; j < m_animNj; j++) delete[] m_animVF[j];
        delete[] m_animVF; m_animVF = NULL;
    }
    m_animNi = m_animNj = 0;
}

void CurrentOverlay::computeAnimCoefficients(ncdfOverlayFactory *factory) {
    if (m_animCoeffReady) return;
    if (!cachedU || !cachedV) return;
    int ni = cachedVNi, nj = cachedUNj;
    if (ni < 2 || nj < 2) return;

    // Free old animation coefficients
    if (m_animCoeffU) { free(m_animCoeffU); m_animCoeffU = NULL; }
    if (m_animCoeffV) { free(m_animCoeffV); m_animCoeffV = NULL; }

    // Allocate flat coefficient arrays (same layout as cachedCoeffU/V)
    m_animCoeffU = (float*)calloc(nj * ni, sizeof(float));
    m_animCoeffV = (float*)calloc(nj * ni, sizeof(float));
    if (!m_animCoeffU || !m_animCoeffV) {
        ncdfLog("[anim:cur] ERROR: failed to allocate coefficient arrays\n");
        return;
    }

    // Determine if data is global (wraps 360°)
    double lonMin = factory->tlon, lonMax = factory->blon;
    double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
    bool isGlobal = (lonMax - lonMin + gridSpacingLon >= 360);

    // Prefilter B-spline coefficients from source U/V grids
    factory->PrefilterCoefficients(
        m_animCoeffU, m_animCoeffV, cachedU, cachedV,
        ni, nj, isGlobal,
        m_animCoefU_min, m_animCoefU_max, m_animCoefV_min, m_animCoefV_max);
    m_animCoeffReady = true;
    ncdfLog("[anim:cur] computed source B-spline coefficients %dx%d, coefU=[%.4f, %.4f]\n",
        ni, nj, m_animCoefU_min, m_animCoefU_max);
}

void CurrentOverlay::Invalidate() {
    needsRebuild = true;
    hasPhysicalTex = false;
}

void CurrentOverlay::prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory) {
    if (!gui || !gui->myMessage.hasCurrent()) return;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    // 1. Build U/V grids as float (reuse existing allocation for same-file switch)
    if (!cachedU || !cachedV || cachedUNj != nj || cachedVNi != ni) {
        ncdfLog("[prepare:cur] grid cache MISS — allocating %dx%d\n", ni, nj);
        if (cachedU) { for (int j = 0; j < cachedUNj; j++) delete[] cachedU[j]; delete[] cachedU; }
        if (cachedV) { for (int j = 0; j < cachedUNj; j++) delete[] cachedV[j]; delete[] cachedV; }
        cachedU = new float*[nj]; cachedV = new float*[nj];
        for (int j = 0; j < nj; j++) {
            cachedU[j] = new float[ni]; cachedV[j] = new float[ni];
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                cachedU[j][i] = (u == ncdf_NOTDEF || !isfinite(u)) ? NAN : (float)u;
                cachedV[j][i] = (v == ncdf_NOTDEF || !isfinite(v)) ? NAN : (float)v;
            }
        }
        cachedUNj = nj; cachedVNi = ni;
    } else {
        // Grid cache HIT — reuse existing allocation, just overwrite values
        ncdfLog("[prepare:cur] grid cache HIT — reusing %dx%d\n", ni, nj);
        for (int j = 0; j < nj; j++) {
            for (int i = 0; i < ni; i++) {
                double u = gui->myMessage.getU(i, j), v = gui->myMessage.getV(i, j);
                cachedU[j][i] = (u == ncdf_NOTDEF || !isfinite(u)) ? NAN : (float)u;
                cachedV[j][i] = (v == ncdf_NOTDEF || !isfinite(v)) ? NAN : (float)v;
            }
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // 2. B-spline coefficients for U and V
    bool coeffRebuilt = false;
    if (!cachedCoeffU || !cachedCoeffV || cachedCoeffNj != nj || cachedCoeffNi != ni) {
        ncdfLog("[prepare:cur] coeff cache MISS — cachedCoeffU=%p cachedNj=%d cachedNi=%d vs nj=%d ni=%d\n",
            (void*)cachedCoeffU, cachedCoeffNj, cachedCoeffNi, nj, ni);
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
            // Set physical value range for shader denormalization (coefRange * bsplineScale)
            const float bsplineScale = 2.5858f;
            cachedCoefMin = cachedCoefU_min * bsplineScale;
            cachedCoefMax = cachedCoefU_max * bsplineScale;
        }
        coeffRebuilt = true;
    }

    // Compute speed range from U/V (for shader normalization)
    float dMin = 1e30f, dMax = -1e30f;
    for (int jj = 0; jj < nj; jj++)
        for (int ii = 0; ii < ni; ii++) {
            float u = cachedU[jj][ii], v = cachedV[jj][ii];
            if (isfinite(u) && isfinite(v)) {
                float spd = sqrtf(u * u + v * v);
                if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
            }
        }
    cachedDataMin = (dMin < dMax) ? dMin : 0;
    cachedDataMax = (dMin < dMax) ? dMax : 1;

    auto t3 = std::chrono::high_resolution_clock::now();

    dataReady = true;
    needsRebuild = coeffRebuilt;  // Only rebuild texture when data actually changed
    auto uvMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto bsplineMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t1).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t0).count();
    ncdfLog("[perf] Current::prepareData ni=%d nj=%d uv_grid=%lldms bspline(U+V)=%lldms total=%lldms\n",
        ni, nj, (long long)uvMs, (long long)bsplineMs, (long long)totalMs);
}

wxColour CurrentOverlay::GetColor(double val_in) {
    static const double stops[][4] = {
        {0.00,  20,  20, 180}, {0.10,  30,  80, 220}, {0.25,   0, 180, 220},
        {0.50,   0, 200,  80}, {0.75, 220, 220,  20}, {1.00, 240, 100,  20},
        {1.50, 220,  20,  20},
    };
    return ncdfOverlayFactory::InterpolateStops(stops, 7, wxMax(val_in, 0.0), false);
}

bool CurrentOverlay::RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                                     ncdfOverlayFactory *factory,
                                     double** animatedGrid, double** animUGrid, double** animVGrid) {
    if (!gui) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Texture reuse: GPU texture already exists, just draw it (view pan / refresh)
    if (hasTexture && !needsRebuild && !animatedGrid) {
        ncdfLog("[render:current] REUSE path: hasTex=%d needsRebuild=%d cachedCoefMin=%f cachedCoefMax=%f\n",
            (int)hasTexture, (int)needsRebuild, cachedCoefMin, cachedCoefMax);
        ncdfOverlayFactory::RenderSettings settings;

        settings.vectorMode = true;  // Texture was created in vector mode (R=U, G=V)
        settings.uGrid = cachedU;
        settings.vGrid = cachedV;
        settings.precompCoeffU = NULL;
        settings.precompCoeffV = NULL;
        settings.dataMin = cachedCoefMin;
        settings.dataMax = cachedCoefMax;
        settings.dataMinV = cachedDataMinV;
        settings.dataMaxV = cachedDataMaxV;
        settings.physMin = cachedPhysMin;
        settings.physMax = cachedPhysMax;
        settings.physMinV = cachedPhysMinV;
        settings.physMaxV = cachedPhysMaxV;
        settings.lutMin = 0.0f;
        settings.lutMax = 1.5f;
        factory->RenderGridOverlay(vp, (float**)(size_t)1,
            CurrentOverlay::GetColor, settings,
            glTexture, hasTexture, needsRebuild,
            dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
            &physicalTexID, &hasPhysicalTex);
        return true;
    }

    // ===== ANIMATION PATH =====
    // Patent-compliant: B-spline coefficients from source U/V computed once, reused every frame.
    // ncdfAnimate internally does SampleSplineCoeff at advected positions.
    // Coefficient texture = source data (static, built once).
    // Physical texture = advected U/V (rebuilt each frame for coastline fallback).
    if (animatedGrid) {
        cachedNj = nj; cachedNi = ni;

        // Convert advected U/V to pre-allocated float buffers for physical texture
        ensureAnimBuffers(ni, nj);
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                m_animUF[j][i] = (float)animUGrid[j][i];
                m_animVF[j][i] = (float)animVGrid[j][i];
            }

        // First frame: compute source B-spline coefficients
        if (!m_animCoeffReady) {
            computeAnimCoefficients(factory);
            if (!m_animCoeffReady) return false;
        }

        ncdfOverlayFactory::RenderSettings settings;
        settings.vectorMode = true;
        settings.uGrid = m_animUF;
        settings.vGrid = m_animVF;
        settings.precompCoeffU = m_animCoeffU;
        settings.precompCoeffV = m_animCoeffV;
        settings.precompCoefU_min = m_animCoefU_min;
        settings.precompCoefU_max = m_animCoefU_max;
        settings.precompCoefV_min = m_animCoefV_min;
        settings.precompCoefV_max = m_animCoefV_max;
        settings.lutMin = 0.0f;
        settings.lutMax = 1.5f;

        needsRebuild = true;
        hasPhysicalTex = false;

        factory->RenderGridOverlay(vp, (float**)(size_t)1,
            CurrentOverlay::GetColor, settings,
            glTexture, hasTexture, needsRebuild,
            dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
            &physicalTexID, &hasPhysicalTex);

        if (hasTexture) {
            cachedCoefMin = settings.dataMin;
            cachedCoefMax = settings.dataMax;
            cachedDataMinV = settings.dataMinV;
            cachedDataMaxV = settings.dataMaxV;
            cachedPhysMin = settings.physMin;
            cachedPhysMax = settings.physMax;
            cachedPhysMinV = settings.physMinV;
            cachedPhysMaxV = settings.physMaxV;
        }
        return true;
    }

    // ===== STATIC PATH =====
    if (!cachedU || !cachedV || !cachedCoeffU || !cachedCoeffV) return false;

    // Build shader settings for static rendering
    ncdfLog("[render:current] REBUILD path: cachedU=%p cachedCoeffU=%p hasTex=%d needsRebuild=%d\n",
        (void*)cachedU, (void*)cachedCoeffU, (int)hasTexture, (int)needsRebuild);
    ncdfOverlayFactory::RenderSettings settings;

    // Static mode: use cached coefficient and physical ranges
    settings.dataMin = cachedCoefMin;
    settings.dataMax = cachedCoefMax;
    settings.dataMinV = cachedDataMinV;
    settings.dataMaxV = cachedDataMaxV;
    settings.physMin = cachedPhysMin;
    settings.physMax = cachedPhysMax;
    settings.physMinV = cachedPhysMinV;
    settings.physMaxV = cachedPhysMaxV;

    // Static mode: use cached u/v grids and precomputed coefficients
    settings.vectorMode = true;
    settings.uGrid = cachedU;
    settings.vGrid = cachedV;
    settings.precompCoeffU = cachedCoeffU;
    settings.precompCoeffV = cachedCoeffV;
    settings.precompCoefU_min = cachedCoefU_min;
    settings.precompCoefU_max = cachedCoefU_max;
    settings.precompCoefV_min = cachedCoefV_min;
    settings.precompCoefV_max = cachedCoefV_max;

    // Compute speed range for shader normalization (only when data changes)
    if (settings.vectorMode && settings.uGrid && settings.vGrid && (needsRebuild || !hasTexture)) {
        float dMin = 1e30f, dMax = -1e30f;
        for (int jj = 0; jj < nj; jj++)
            for (int ii = 0; ii < ni; ii++) {
                float u = settings.uGrid[jj][ii], v = settings.vGrid[jj][ii];
                if (isfinite(u) && isfinite(v)) {
                    float spd = sqrtf(u*u + v*v);
                    if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
                }
            }
        cachedDataMin = (dMin < dMax) ? (float)dMin : 0;
        cachedDataMax = (dMin < dMax) ? (float)dMax : 1;
    }

    // Use cached coefMin/coefMax if texture exists (set by RenderGridOverlay on first frame)
    ncdfLog("[perf] Current RENDER: hasTex=%d coefMin=%f coefMax=%f dataMin=%f dataMax=%f\n",
        (int)hasTexture, cachedCoefMin, cachedCoefMax, cachedDataMin, cachedDataMax);
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
        settings.physMin = cachedDataMin;   // Speed range for physical texture denormalization
        settings.physMax = cachedDataMax;
        settings.physMinV = 0.0f;
        settings.physMaxV = 1.0f;
    }
    settings.lutMin = 0.0f;    // Current speed color stops range
    settings.lutMax = 1.5f;

    float** renderGrid = (float**)(size_t)1;
    factory->RenderGridOverlay(vp, renderGrid,
        CurrentOverlay::GetColor, settings,
        glTexture, hasTexture, needsRebuild,
        dataDim, glDim, lutID, hasLUT, uploadBuf, uploadBufSize,
        &physicalTexID, &hasPhysicalTex);

    // Texture is now on GPU — cache ranges, but skip coefficient release during animation
    // (animation reuses the same texture path each frame; releasing coefficients would
    // force a full pipeline rebuild every frame)
    if (hasTexture) {
        cachedCoefMin = settings.dataMin;
        cachedCoefMax = settings.dataMax;
        cachedDataMinV = settings.dataMinV;
        cachedDataMaxV = settings.dataMaxV;
        cachedPhysMin = settings.physMin;
        cachedPhysMax = settings.physMax;
        cachedPhysMinV = settings.physMinV;
        cachedPhysMaxV = settings.physMaxV;
        if (!animatedGrid && !plugin->m_settingsCurrent.animate &&
            !plugin->m_settingsSeaTemp.animate && !plugin->m_settingsSalinity.animate) {
            // Static mode only: release B-spline coefficient data (texture is on GPU, no longer needed)
            releaseCoeffData();
        }
    }

    return true;
}
