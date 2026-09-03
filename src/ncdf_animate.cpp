//===================================================================
// ncdf_animate.cpp — CPU-based flow animation
// Based on two patents:
//   1. 流场标量色图动态视觉生成方法 (dual-coordinate-field blending)
//   2. 迭代图像重采样链的插值误差控制 (Keys cubic + B-spline)
//
// Key design: u and v are interpolated INDEPENDENTLY (never pre-compute speed),
// then speed = sqrt(u²+v²) is computed from the blended components.
//===================================================================

#include "ncdf_animate.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include "shader/ncdf_animate_shader.h"
#include "ocpn_plugin.h"
#include <GL/gl.h>
#include <cmath>

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_FRAMEBUFFER_BINDING_EXT
#define GL_FRAMEBUFFER_BINDING_EXT 0x8CA6
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#include <cstring>
#include <cstdio>
#include <cstdarg>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void animLog(const char* fmt, ...) {
    FILE* f = fopen("C:\\ProgramData\\opencpn\\ncdf_debug.log", "a");
    if (!f) return;
    fprintf(f, "[animate] ");
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fprintf(f, "\n"); fclose(f);
}

//===================================================================
// Construction / Destruction
//===================================================================

ncdfAnimate::ncdfAnimate()
    : m_initialized(false), m_plugin(NULL), m_gui(NULL),
      m_period(30), m_power(0.6f), m_maxDisp(2.0f), m_frame(0),
      m_dispX(NULL), m_dispY(NULL), m_hasDisp(false), m_dispW(0), m_dispH(0),
      m_c1x(NULL), m_c1y(NULL), m_c2x(NULL), m_c2y(NULL),
      m_hasCoordFields(false), m_coordW(0), m_coordH(0),
      m_srcU(NULL), m_srcV(NULL), m_srcW(0), m_srcH(0),
      m_speedMin(0), m_speedMax(1),
      m_scalarCoeff(NULL), m_scalarSrc(NULL), m_scalarW(0), m_scalarH(0),
      m_advectedScalar(NULL), m_advScalarW(0), m_advScalarH(0),
      m_extUGrid(NULL), m_extVGrid(NULL), m_extNi(0), m_extNj(0),
      m_coeffU(NULL), m_coeffV(NULL), m_hasCoeffs(false),
      m_advectedU(NULL), m_advectedV(NULL), m_advectedSpeed(NULL),
      m_advW(0), m_advH(0),
      m_gpuReady(false), m_gpuDeletePending(false), m_texDisp(0),
      m_texCoordA1(0), m_texCoordA2(0), m_texCoordB1(0), m_texCoordB2(0),
      m_texResult(0), m_fbo(0), m_readbackBuf(NULL), m_readbackBufSize(0)
{
}

ncdfAnimate::~ncdfAnimate() { Cleanup(); }

//===================================================================
// Grid allocation helpers
//===================================================================

bool ncdfAnimate::AllocateFloatGrid(float**& grid, int h, int w) {
    grid = new(std::nothrow) float*[h];
    if (!grid) return false;
    for (int j = 0; j < h; j++) {
        grid[j] = new(std::nothrow) float[w];
        if (!grid[j]) { FreeFloatGrid(grid, j); return false; }
        memset(grid[j], 0, w * sizeof(float));
    }
    return true;
}

void ncdfAnimate::FreeFloatGrid(float**& grid, int h) {
    if (!grid) return;
    for (int j = 0; j < h; j++) delete[] grid[j];
    delete[] grid;
    grid = NULL;
}

bool ncdfAnimate::AllocateGrid(double**& grid, int h, int w) {
    grid = new(std::nothrow) double*[h];
    if (!grid) return false;
    for (int j = 0; j < h; j++) {
        grid[j] = new(std::nothrow) double[w];
        if (!grid[j]) { FreeGrid(grid, j); return false; }
        memset(grid[j], 0, w * sizeof(double));
    }
    return true;
}

void ncdfAnimate::FreeGrid(double**& grid, int h) {
    if (!grid) return;
    for (int j = 0; j < h; j++) delete[] grid[j];
    delete[] grid;
    grid = NULL;
}

//===================================================================
// Init / Cleanup
//===================================================================

bool ncdfAnimate::Init(ncdf_pi* plugin, MainDialog* gui) {
    if (m_initialized) return true;
    m_plugin = plugin;
    m_gui = gui;
    m_initialized = true;
    m_frame = 0;
    animLog("module initialized, period=%d, power=%.1f, maxDisp=%.1f",
            m_period, m_power, m_maxDisp);

    // DEBUG: seed 5 tracer points across the data
    // Will be advected by displacement map, sampled via B-spline, mapped through LUT
    m_tracerSeedCount = 5;
    m_tracerSeeds[0] = {0.25f, 0.25f};  // top-left quadrant
    m_tracerSeeds[1] = {0.75f, 0.25f};  // top-right
    m_tracerSeeds[2] = {0.50f, 0.50f};  // center
    m_tracerSeeds[3] = {0.25f, 0.75f};  // bottom-left
    m_tracerSeeds[4] = {0.75f, 0.75f};  // bottom-right
    m_tracerInitialized = false;

    return true;
}

void ncdfAnimate::Cleanup() {
    CleanupGPU();
    FreeFloatGrid(m_dispX, m_dispH);
    FreeFloatGrid(m_dispY, m_dispH);
    FreeFloatGrid(m_c1x, m_coordH);
    FreeFloatGrid(m_c1y, m_coordH);
    FreeFloatGrid(m_c2x, m_coordH);
    FreeFloatGrid(m_c2y, m_coordH);
    FreeFloatGrid(m_srcU, m_srcH);
    FreeFloatGrid(m_srcV, m_srcH);
    FreeFloatGrid(m_coeffU, m_srcH);
    FreeFloatGrid(m_coeffV, m_srcH);
    FreeGrid(m_advectedU, m_advH);
    FreeGrid(m_advectedV, m_advH);
    FreeGrid(m_advectedSpeed, m_advH);
    FreeFloatGrid(m_scalarCoeff, m_scalarH);
    FreeFloatGrid(m_scalarSrc, m_scalarH);
    FreeGrid(m_advectedScalar, m_advScalarH);
    delete[] m_readbackBuf; m_readbackBuf = NULL; m_readbackBufSize = 0;
    m_hasDisp = false;
    m_hasCoordFields = false;
    m_hasCoeffs = false;
    m_initialized = false;
}

//===================================================================
// Keys cubic convolution kernel (a = -0.5, patent 2 claim 2)
//===================================================================
static inline float KeysCubicW(float t) {
    t = fabsf(t);
    if (t <= 1.0f) return 1.5f*t*t*t - 2.5f*t*t + 1.0f;
    if (t <= 2.0f) return -0.5f*t*t*t + 2.5f*t*t - 4.0f*t + 2.0f;
    return 0.0f;
}

//===================================================================
// Sample coordinate field using Keys cubic convolution
//===================================================================
float ncdfAnimate::SampleCoordField(float** field, float x, float y, int w, int h) {
    x = ClampX(x);
    y = ClampY(y);
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float result = 0;
    for (int m = -1; m <= 2; m++) {
        int jj = iy + m;
        if (jj < 0) jj = 0; else if (jj >= h) jj = h - 1;
        float wy = KeysCubicW(fy - m);
        for (int n = -1; n <= 2; n++) {
            int ii = ix + n;
            if (ii < 0) ii = 0; else if (ii >= w) ii = w - 1;
            float wx = KeysCubicW(fx - n);
            result += wy * wx * field[jj][ii];
        }
    }
    return result;
}

//===================================================================
// Cubic B-spline basis function
//===================================================================
static inline float BSplineCubic(float t) {
    t = fabsf(t);
    if (t < 1.0f) return 0.5f*t*t*t - t*t + 2.0f/3.0f;
    if (t < 2.0f) { float d = 2.0f - t; return d*d*d / 6.0f; }
    return 0.0f;
}

//===================================================================
// Sample B-spline coefficient field (generic, used for both u and v)
//===================================================================
float ncdfAnimate::SampleSplineCoeff(float** coeff, float x, float y) {
    if (!coeff) return 0;
    x = ClampX(x);
    y = ClampY(y);
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    float result = 0;
    for (int m = -1; m <= 2; m++) {
        int jj = iy + m;
        if (jj < 0) jj = 0; else if (jj >= m_srcH) jj = m_srcH - 1;
        float wy = BSplineCubic(fy - m);
        for (int n = -1; n <= 2; n++) {
            int ii = ix + n;
            if (ii < 0) ii = 0; else if (ii >= m_srcW) ii = m_srcW - 1;
            float wx = BSplineCubic(fx - n);
            result += wy * wx * coeff[jj][ii];
        }
    }
    return result;
}

//===================================================================
// B-spline prefilter (IIR, one-time per source field)
//===================================================================
void ncdfAnimate::BicubicSplinePrefilter(float** dst, float** src, int h, int w) {
    const float z = -0.26794919243112270f;
    const float inv = (1.0f - z) * (1.0f - z * z) * (1.0f - z * z * z) * (1.0f - z * z * z * z);

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) dst[j][i] = src[j][i];

        // Causal pass with mirror boundary
        {
            float sum0 = dst[j][0];
            float zk = z;
            for (int m = 1; m < w && m <= 3; m++) { sum0 += dst[j][m] * zk; zk *= z; }
            dst[j][0] = sum0 * inv;
        }
        for (int i = 1; i < w; i++)
            dst[j][i] = dst[j][i] + z * dst[j][i - 1];

        // Anti-causal pass
        {
            float zk = z;
            float sumn = 0;
            for (int m = w - 2; m >= 0 && m >= w - 4; m--) { sumn += dst[j][m] * zk; zk *= z; }
            dst[j][w-1] = (dst[j][w-1] + sumn * 2) * inv;
        }
        for (int i = w - 2; i >= 0; i--)
            dst[j][i] = dst[j][i] + z * dst[j][i + 1];
    }

    for (int i = 0; i < w; i++) {
        {
            float sum0 = dst[0][i];
            float zk = z;
            for (int m = 1; m < h && m <= 3; m++) { sum0 += dst[m][i] * zk; zk *= z; }
            dst[0][i] = sum0 * inv;
        }
        for (int j = 1; j < h; j++)
            dst[j][i] = dst[j][i] + z * dst[j - 1][i];

        {
            float zk = z;
            float sumn = 0;
            for (int m = h - 2; m >= 0 && m >= h - 4; m--) { sumn += dst[m][i] * zk; zk *= z; }
            dst[h-1][i] = (dst[h-1][i] + sumn * 2) * inv;
        }
        for (int j = h - 2; j >= 0; j--)
            dst[j][i] = dst[j][i] + z * dst[j + 1][i];
    }
}

//===================================================================
// Build u and v source fields from current data
//===================================================================
bool ncdfAnimate::BuildSourceFields() {
    if (!m_gui) return false;
    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;
    if (ni < 3 || nj < 3) return false;
    if (!m_gui->myMessage.hasCurrent()) return false;

    if (!m_srcU || m_srcW != ni || m_srcH != nj) {
        FreeFloatGrid(m_srcU, m_srcH);
        FreeFloatGrid(m_srcV, m_srcH);
        FreeFloatGrid(m_coeffU, m_srcH);
        FreeFloatGrid(m_coeffV, m_srcH);
        AllocateFloatGrid(m_srcU, nj, ni);
        AllocateFloatGrid(m_srcV, nj, ni);
        AllocateFloatGrid(m_coeffU, nj, ni);
        AllocateFloatGrid(m_coeffV, nj, ni);
        m_srcW = ni; m_srcH = nj;
        m_hasCoeffs = false;
    }

    // Fill u and v, compute speed range for denormalization
    float sMin = 1e30f, sMax = -1e30f;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            double u = m_gui->myMessage.getU(i, j);
            double v = m_gui->myMessage.getV(i, j);
            if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || !isfinite(u) || !isfinite(v)) {
                m_srcU[j][i] = -1e30f;  // invalid marker
                m_srcV[j][i] = -1e30f;
            } else {
                m_srcU[j][i] = (float)u;
                m_srcV[j][i] = (float)v;
                float spd = (float)sqrt(u * u + v * v);
                if (spd < sMin) sMin = spd;
                if (spd > sMax) sMax = spd;
            }
        }
    m_speedMin = sMin;
    m_speedMax = sMax;

    // Fill invalid pixels with nearest valid neighbor (prevent B-spline contamination)
    auto fillInvalid = [&](float** grid) {
        for (int pass = 0; pass < 3; pass++)
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    if (grid[j][i] > -1e29f) continue;
                    float sum = 0; int cnt = 0;
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++) {
                            int jj = j + dj, ii = i + di;
                            if (jj >= 0 && jj < nj && ii >= 0 && ii < ni && grid[jj][ii] > -1e29f) {
                                sum += grid[jj][ii]; cnt++;
                            }
                        }
                    if (cnt > 0) grid[j][i] = sum / cnt;
                }
    };

    // Use a temp grid for filling (don't modify srcU/srcV which mark invalid)
    float** filledU = NULL, **filledV = NULL;
    AllocateFloatGrid(filledU, nj, ni);
    AllocateFloatGrid(filledV, nj, ni);
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            filledU[j][i] = m_srcU[j][i];
            filledV[j][i] = m_srcV[j][i];
        }
    fillInvalid(filledU);
    fillInvalid(filledV);

    // B-spline prefilter on both u and v (patent 2 step S102)
    BicubicSplinePrefilter(m_coeffU, filledU, nj, ni);
    BicubicSplinePrefilter(m_coeffV, filledV, nj, ni);
    FreeFloatGrid(filledU, nj);
    FreeFloatGrid(filledV, nj);
    m_hasCoeffs = true;

    animLog("source fields built: %dx%d, speed range=[%.4f, %.4f]", ni, nj, sMin, sMax);
    return true;
}

//===================================================================
// Build source fields from external float grids (for overlays without myMessage data)
//===================================================================
bool ncdfAnimate::BuildSourceFields(float* const* extUGrid, float* const* extVGrid, int extNi, int extNj) {
    if (!extUGrid || !extVGrid) return false;
    if (extNi < 3 || extNj < 3) return false;

    int ni = extNi, nj = extNj;

    if (!m_srcU || m_srcW != ni || m_srcH != nj) {
        FreeFloatGrid(m_srcU, m_srcH);
        FreeFloatGrid(m_srcV, m_srcH);
        FreeFloatGrid(m_coeffU, m_srcH);
        FreeFloatGrid(m_coeffV, m_srcH);
        AllocateFloatGrid(m_srcU, nj, ni);
        AllocateFloatGrid(m_srcV, nj, ni);
        AllocateFloatGrid(m_coeffU, nj, ni);
        AllocateFloatGrid(m_coeffV, nj, ni);
        m_srcW = ni; m_srcH = nj;
        m_hasCoeffs = false;
    }

    float sMin = 1e30f, sMax = -1e30f;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float u = extUGrid[j][i], v = extVGrid[j][i];
            if (!isfinite(u) || !isfinite(v)) {
                m_srcU[j][i] = -1e30f;
                m_srcV[j][i] = -1e30f;
            } else {
                m_srcU[j][i] = u;
                m_srcV[j][i] = v;
                float spd = sqrtf(u * u + v * v);
                if (spd < sMin) sMin = spd;
                if (spd > sMax) sMax = spd;
            }
        }
    m_speedMin = sMin;
    m_speedMax = sMax;

    auto fillInvalid = [&](float** grid) {
        for (int pass = 0; pass < 3; pass++)
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    if (grid[j][i] > -1e29f) continue;
                    float sum = 0; int cnt = 0;
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++) {
                            int jj = j + dj, ii = i + di;
                            if (jj >= 0 && jj < nj && ii >= 0 && ii < ni && grid[jj][ii] > -1e29f) {
                                sum += grid[jj][ii]; cnt++;
                            }
                        }
                    if (cnt > 0) grid[j][i] = sum / cnt;
                }
    };

    float** filledU = NULL, **filledV = NULL;
    AllocateFloatGrid(filledU, nj, ni);
    AllocateFloatGrid(filledV, nj, ni);
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            filledU[j][i] = m_srcU[j][i];
            filledV[j][i] = m_srcV[j][i];
        }
    fillInvalid(filledU);
    fillInvalid(filledV);

    BicubicSplinePrefilter(m_coeffU, filledU, nj, ni);
    BicubicSplinePrefilter(m_coeffV, filledV, nj, ni);
    FreeFloatGrid(filledU, nj);
    FreeFloatGrid(filledV, nj);
    m_hasCoeffs = true;

    animLog("source fields built from external grids: %dx%d, speed range=[%.4f, %.4f]", ni, nj, sMin, sMax);
    return true;
}

//===================================================================
// Compute displacement map — full RK4 backward integration
//===================================================================
bool ncdfAnimate::ComputeDisplacementMap() {
    if (!m_gui || !m_gui->myMessage.hasCurrent()) return false;
    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;
    if (ni < 3 || nj < 3) return false;

    FreeFloatGrid(m_dispX, m_dispH);
    FreeFloatGrid(m_dispY, m_dispH);
    if (!AllocateFloatGrid(m_dispX, nj, ni)) return false;
    if (!AllocateFloatGrid(m_dispY, nj, ni)) { FreeFloatGrid(m_dispX, nj); return false; }

    // Find max velocity
    double vMax = 0;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            double u = m_gui->myMessage.getU(i, j);
            double v = m_gui->myMessage.getV(i, j);
            if (u != ncdf_NOTDEF && v != ncdf_NOTDEF && isfinite(u) && isfinite(v))
                vMax = fmax(vMax, sqrt(u*u + v*v));
        }
    if (vMax < 1e-10) vMax = 1.0;

    // Helper: visual velocity at arbitrary position (bilinear on u/v)
    auto visVelAt = [&](double px, double py, double& ovx, double& ovy) -> bool {
        if (px < 0) px = 0; if (px > ni - 1) px = ni - 1;
        if (py < 0) py = 0; if (py > nj - 1) py = nj - 1;
        int ix = (int)px, iy = (int)py;
        int ix1 = (ix + 1 < ni) ? ix + 1 : ix;
        int iy1 = (iy + 1 < nj) ? iy + 1 : iy;
        float fx = (float)(px - ix), fy = (float)(py - iy);
        // Interpolate u and v SEPARATELY (never pre-compute speed)
        double u = (1-fx)*(1-fy)*m_gui->myMessage.getU(ix,iy) + fx*(1-fy)*m_gui->myMessage.getU(ix1,iy)
                 + (1-fx)*fy*m_gui->myMessage.getU(ix,iy1) + fx*fy*m_gui->myMessage.getU(ix1,iy1);
        double v = (1-fx)*(1-fy)*m_gui->myMessage.getV(ix,iy) + fx*(1-fy)*m_gui->myMessage.getV(ix1,iy)
                 + (1-fx)*fy*m_gui->myMessage.getV(ix,iy1) + fx*fy*m_gui->myMessage.getV(ix1,iy1);
        if (u == ncdf_NOTDEF || v == ncdf_NOTDEF || !isfinite(u) || !isfinite(v)) return false;
        double mag = sqrt(u*u + v*v);
        if (mag < 0.001) { ovx = 0; ovy = 0; return true; }
        double visSpeed = (double)m_maxDisp * pow(mag / vMax, (double)m_power);
        ovx = (u / mag) * visSpeed;
        ovy = (v / mag) * visSpeed;
        return true;
    };

    // Full RK4 backward integration
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            m_dispX[j][i] = 0; m_dispY[j][i] = 0;
            double vx0, vy0;
            if (!visVelAt(i, j, vx0, vy0)) continue;
            double k1x = -vx0, k1y = -vy0;
            // DEBUG: log visVel at pixel (100,100) first time
            if (i == 100 && j == 100) {
                static bool s_visDbg = false;
                if (!s_visDbg) {
                    animLog("[DISP-DBG] px(100,100) visVel=(%.6f,%.6f) D=%.1f p=%.2f vMax=%.3f",
                        vx0, vy0, (double)m_maxDisp, (double)m_power, vMax);
                    s_visDbg = true;
                }
            }
            double vx1, vy1;
            if (!visVelAt(i + k1x*0.5, j + k1y*0.5, vx1, vy1)) {
                m_dispX[j][i] = (float)k1x; m_dispY[j][i] = (float)k1y; continue;
            }
            double k2x = -vx1, k2y = -vy1;
            double vx2, vy2;
            if (!visVelAt(i + k2x*0.5, j + k2y*0.5, vx2, vy2)) {
                m_dispX[j][i] = (float)((k1x + 2*k2x) / 3.0);
                m_dispY[j][i] = (float)((k1y + 2*k2y) / 3.0); continue;
            }
            double k3x = -vx2, k3y = -vy2;
            double vx3, vy3;
            if (!visVelAt(i + k3x, j + k3y, vx3, vy3)) {
                m_dispX[j][i] = (float)((k1x + 2*k2x + 2*k3x) / 5.0);
                m_dispY[j][i] = (float)((k1y + 2*k2y + 2*k3y) / 5.0); continue;
            }
            double k4x = -vx3, k4y = -vy3;
            m_dispX[j][i] = (float)((k1x + 2*k2x + 2*k3x + k4x) / 6.0);
            m_dispY[j][i] = (float)((k1y + 2*k2y + 2*k3y + k4y) / 6.0);
        }

    m_dispW = ni; m_dispH = nj;
    m_hasDisp = true;
    animLog("RK4 displacement map %dx%d, vMax=%.4f", ni, nj, vMax);
    return true;
}

// Overload: compute displacement from float grids directly (new data pipeline)
bool ncdfAnimate::ComputeDisplacementMap(float* const* uGrid, float* const* vGrid, int ni, int nj) {
    animLog("ComputeDisplacementMap(float): uGrid=%p vGrid=%p ni=%d nj=%d",
        (void*)uGrid, (void*)vGrid, ni, nj);
    if (!uGrid || !vGrid || ni < 3 || nj < 3) { animLog("ComputeDisplacementMap: early return (invalid params)"); return false; }

    FreeFloatGrid(m_dispX, m_dispH);
    FreeFloatGrid(m_dispY, m_dispH);
    animLog("ComputeDisplacementMap: allocating %dx%d", ni, nj);
    if (!AllocateFloatGrid(m_dispX, nj, ni)) { animLog("ComputeDisplacementMap: alloc dispX failed"); return false; }
    if (!AllocateFloatGrid(m_dispY, nj, ni)) { FreeFloatGrid(m_dispX, nj); animLog("ComputeDisplacementMap: alloc dispY failed"); return false; }
    animLog("ComputeDisplacementMap: grids allocated");

    // Find max velocity from float grids
    double vMax = 0;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float u = uGrid[j][i], v = vGrid[j][i];
            if (isfinite(u) && isfinite(v)) {
                double mag = sqrt(u*u + v*v);
                if (mag > vMax) vMax = mag;
            }
        }
    if (!isfinite(vMax) || vMax < 1e-10) vMax = 1.0;

    // Patent formula: V_vis = D * (|V| / Vmax)^p * direction
    // V and Vmax are in the same units (m/s) — ratio is dimensionless
    // No unit conversion needed; D controls the output pixel displacement
    animLog("ComputeDisplacementMap: vMax=%.4f m/s, D=%.1f p=%.2f, ni=%d nj=%d",
            vMax, (double)m_maxDisp, (double)m_power, ni, nj);

    // Helper: visual velocity at arbitrary position using Keys cubic convolution (a=-0.5)
    auto visVelAt = [&](double px, double py, double& ovx, double& ovy) -> bool {
        if (px < 0) px = 0; if (px > ni - 1) px = ni - 1;
        if (py < 0) py = 0; if (py > nj - 1) py = nj - 1;
        // Keys cubic 4x4 separable sampling
        float tx = (float)px - 0.5f, ty = (float)py - 0.5f;
        int ix = (int)floorf(tx), iy = (int)floorf(ty);
        float fx = tx - ix, fy = ty - iy;
        float wx[4], wy[4];
        for (int k = 0; k < 4; k++) { wx[k] = KeysCubicW(fx - k + 1); wy[k] = KeysCubicW(fy - k + 1); }
        double u = 0, v = 0;
        for (int dj = 0; dj < 4; dj++) {
            int jj = iy + dj - 1;
            if (jj < 0) jj = 0; if (jj >= nj) jj = nj - 1;
            for (int di = 0; di < 4; di++) {
                int ii = ix + di - 1;
                if (ii < 0) ii = 0; if (ii >= ni) ii = ni - 1;
                float w = wx[di] * wy[dj];
                u += uGrid[jj][ii] * w;
                v += vGrid[jj][ii] * w;
            }
        }
        if (!isfinite(u) || !isfinite(v)) return false;
        double mag = sqrt(u*u + v*v);
        if (mag < 0.001) { ovx = 0; ovy = 0; return true; }
        // Patent formula: V_vis = D * (|V|/Vmax)^p * direction
        double visSpeed = (double)m_maxDisp * pow(mag / vMax, (double)m_power);
        ovx = (u / mag) * visSpeed;
        ovy = (v / mag) * visSpeed;
        return true;
    };

    // Full RK4 backward integration
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            m_dispX[j][i] = 0; m_dispY[j][i] = 0;
            double vx0, vy0;
            if (!visVelAt(i, j, vx0, vy0)) continue;
            double k1x = -vx0, k1y = -vy0;
            // DEBUG: log visVel at pixel (100,100) first time
            if (i == 100 && j == 100) {
                static bool s_visDbg = false;
                if (!s_visDbg) {
                    animLog("[DISP-DBG] px(100,100) visVel=(%.6f,%.6f) D=%.1f p=%.2f vMax=%.3f",
                        vx0, vy0, (double)m_maxDisp, (double)m_power, vMax);
                    s_visDbg = true;
                }
            }
            double vx1, vy1;
            if (!visVelAt(i + k1x*0.5, j + k1y*0.5, vx1, vy1)) {
                m_dispX[j][i] = (float)k1x; m_dispY[j][i] = (float)k1y; continue;
            }
            double k2x = -vx1, k2y = -vy1;
            double vx2, vy2;
            if (!visVelAt(i + k2x*0.5, j + k2y*0.5, vx2, vy2)) {
                m_dispX[j][i] = (float)((k1x + 2*k2x) / 3.0);
                m_dispY[j][i] = (float)((k1y + 2*k2y) / 3.0); continue;
            }
            double k3x = -vx2, k3y = -vy2;
            double vx3, vy3;
            if (!visVelAt(i + k3x, j + k3y, vx3, vy3)) {
                m_dispX[j][i] = (float)((k1x + 2*k2x + 2*k3x) / 5.0);
                m_dispY[j][i] = (float)((k1y + 2*k2y + 2*k3y) / 5.0); continue;
            }
            double k4x = -vx3, k4y = -vy3;
            m_dispX[j][i] = (float)((k1x + 2*k2x + 2*k3x + k4x) / 6.0);
            m_dispY[j][i] = (float)((k1y + 2*k2y + 2*k3y + k4y) / 6.0);
        }

    m_dispW = ni; m_dispH = nj;
    m_hasDisp = true;
    animLog("RK4 displacement map (float grids) %dx%d, vMax=%.4f", ni, nj, vMax);
    return true;
}
void ncdfAnimate::ResetToIdentity(float** cx, float** cy) {
    for (int j = 0; j < m_coordH; j++)
        for (int i = 0; i < m_coordW; i++) {
            cx[j][i] = (float)i;
            cy[j][i] = (float)j;
        }
}

void ncdfAnimate::InitCoordFields() {
    int ni = m_dispW, nj = m_dispH;
    if (!m_hasDisp) return;
    if (!m_c1x || m_coordW != ni || m_coordH != nj) {
        FreeFloatGrid(m_c1x, m_coordH); FreeFloatGrid(m_c1y, m_coordH);
        FreeFloatGrid(m_c2x, m_coordH); FreeFloatGrid(m_c2y, m_coordH);
        AllocateFloatGrid(m_c1x, nj, ni); AllocateFloatGrid(m_c1y, nj, ni);
        AllocateFloatGrid(m_c2x, nj, ni); AllocateFloatGrid(m_c2y, nj, ni);
        m_coordW = ni; m_coordH = nj;
    }
    ResetToIdentity(m_c1x, m_c1y);
    ResetToIdentity(m_c2x, m_c2y);

    // Pre-iterate C2 by T/2 steps
    int halfPeriod = m_period / 2;
    for (int t = 0; t < halfPeriod; t++) {
        float** newCx = NULL, **newCy = NULL;
        AllocateFloatGrid(newCx, nj, ni); AllocateFloatGrid(newCy, nj, ni);
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float sx = ClampX(i + m_dispX[j][i]);
                float sy = ClampY(j + m_dispY[j][i]);
                newCx[j][i] = SampleCoordField(m_c2x, sx, sy, ni, nj);
                newCy[j][i] = SampleCoordField(m_c2y, sx, sy, ni, nj);
            }
        FreeFloatGrid(m_c2x, nj); FreeFloatGrid(m_c2y, nj);
        m_c2x = newCx; m_c2y = newCy;
    }
    m_hasCoordFields = true;
    m_frame = 0;
    animLog("coord fields initialized, C2 pre-iterated %d steps", halfPeriod);
}

// Overload: init coordinate fields with explicit dimensions (GPU path, no BuildSourceFields)
void ncdfAnimate::InitCoordFields(int ni, int nj) {
    FreeFloatGrid(m_c1x, m_coordH); FreeFloatGrid(m_c1y, m_coordH);
    FreeFloatGrid(m_c2x, m_coordH); FreeFloatGrid(m_c2y, m_coordH);
    if (!AllocateFloatGrid(m_c1x, nj, ni)) return;
    if (!AllocateFloatGrid(m_c1y, nj, ni)) return;
    if (!AllocateFloatGrid(m_c2x, nj, ni)) return;
    if (!AllocateFloatGrid(m_c2y, nj, ni)) return;
    m_coordW = ni; m_coordH = nj;
    m_srcW = ni; m_srcH = nj;  // ClampX/ClampY need these for bounds

    ResetToIdentity(m_c1x, m_c1y);
    ResetToIdentity(m_c2x, m_c2y);
    animLog("[INIT-COORD] after reset: c1x[240][180]=%.3f c2x[240][180]=%.3f",
        (nj>240 && ni>180) ? m_c1x[240][180] : -1.0f,
        (nj>240 && ni>180) ? m_c2x[240][180] : -1.0f);

    // Pre-iterate C2 by T/2 steps
    int halfPeriod = m_period / 2;
    for (int t = 0; t < halfPeriod; t++) {
        float** newCx = NULL, **newCy = NULL;
        AllocateFloatGrid(newCx, nj, ni); AllocateFloatGrid(newCy, nj, ni);
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float sx = ClampX(i + m_dispX[j][i]);
                float sy = ClampY(j + m_dispY[j][i]);
                newCx[j][i] = SampleCoordField(m_c2x, sx, sy, ni, nj);
                newCy[j][i] = SampleCoordField(m_c2y, sx, sy, ni, nj);
            }
        FreeFloatGrid(m_c2x, nj); FreeFloatGrid(m_c2y, nj);
        m_c2x = newCx; m_c2y = newCy;
        if (t < 3 || t == halfPeriod - 1) {
            float sx = ClampX(180 + m_dispX[240][180]);
            float sy = ClampY(240 + m_dispY[240][180]);
            float sampled = SampleCoordField(m_c2x, sx, sy, ni, nj);
            animLog("[PRE-ITER] t=%d c2x[240][180]=%.3f disp=(%.4f,%.4f) sample_pos=(%.3f,%.3f) sampled=%.3f "
                    "c2x[239][179]=%.3f c2x[240][180]=%.3f c2x[241][181]=%.3f",
                t, m_c2x[240][180], m_dispX[240][180], m_dispY[240][180],
                sx, sy, sampled,
                (nj>239 && ni>179) ? m_c2x[239][179] : -1.0f,
                (nj>240 && ni>180) ? m_c2x[240][180] : -1.0f,
                (nj>241 && ni>181) ? m_c2x[241][181] : -1.0f);
        }
    }
    m_hasCoordFields = true;
    m_frame = 0;
    animLog("[INIT-GPU] done: c2x[240][180]=%.3f c2y[240][180]=%.3f m_coordW=%d m_coordH=%d",
        (nj>240 && ni>180) ? m_c2x[240][180] : -1.0f,
        (nj>240 && ni>180) ? m_c2y[240][180] : -1.0f, m_coordW, m_coordH);
    animLog("coord fields initialized (GPU path), C2 pre-iterated %d steps, %dx%d",
        halfPeriod, ni, nj);
}

//===================================================================
// Sample u and v B-spline fields using coordinate fields, blend, compute speed
// u and v are interpolated INDEPENDENTLY (never pre-compute speed)
//===================================================================
void ncdfAnimate::SampleAndBlend() {
    if (!m_hasCoordFields || !m_hasCoeffs) return;
    if (!m_advectedU || !m_advectedV || !m_advectedSpeed) return;
    int ni = m_coordW, nj = m_coordH;

    // Patent sine weights: w1=sin(pi*(t%T)/T), w2=sin(pi*((t+T/2)%T)/T)
    // Both always >= 0, always blend (never exclusive)
    int tMod = m_frame % m_period;
    double w1 = sin(M_PI * tMod / (double)m_period);
    int tMod2 = (m_frame + m_period / 2) % m_period;
    double w2 = sin(M_PI * tMod2 / (double)m_period);
    double wsum = w1 + w2;
    double alpha1 = w1 / wsum;
    double alpha2 = w2 / wsum;

    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            // Check source validity
            if (m_srcU[j][i] < -1e29f || m_srcV[j][i] < -1e29f) {
                m_advectedU[j][i] = ncdf_NOTDEF;
                m_advectedV[j][i] = ncdf_NOTDEF;
                m_advectedSpeed[j][i] = ncdf_NOTDEF;
                continue;
            }

            // Sample u at C1 and C2 positions
            float u1 = SampleSplineCoeff(m_coeffU, ClampX(m_c1x[j][i]), ClampY(m_c1y[j][i]));
            float u2 = SampleSplineCoeff(m_coeffU, ClampX(m_c2x[j][i]), ClampY(m_c2y[j][i]));

            // Sample v at C1 and C2 positions
            float v1 = SampleSplineCoeff(m_coeffV, ClampX(m_c1x[j][i]), ClampY(m_c1y[j][i]));
            float v2 = SampleSplineCoeff(m_coeffV, ClampX(m_c2x[j][i]), ClampY(m_c2y[j][i]));

            // Blend u and v independently (patent 1 formula 11)
            double uBlend = alpha1 * u1 + alpha2 * u2;
            double vBlend = alpha1 * v1 + alpha2 * v2;

            m_advectedU[j][i] = uBlend;
            m_advectedV[j][i] = vBlend;

            // Compute speed from blended u/v (NOT from blended speed)
            double spd = sqrt(uBlend * uBlend + vBlend * vBlend);
            m_advectedSpeed[j][i] = spd;
        }

    // DEBUG: trace pixel (100,100) for current overlay animation
    {
        int pi = 100, pj = 100;
        if (pi < ni && pj < nj && m_srcU[pj][pi] > -1e29f) {
            float c1x = m_c1x[pj][pi], c1y = m_c1y[pj][pi];
            float c2x = m_c2x[pj][pi], c2y = m_c2y[pj][pi];
            float u1 = SampleSplineCoeff(m_coeffU, ClampX(c1x), ClampY(c1y));
            float u2 = SampleSplineCoeff(m_coeffU, ClampX(c2x), ClampY(c2y));
            float v1 = SampleSplineCoeff(m_coeffV, ClampX(c1x), ClampY(c1y));
            float v2 = SampleSplineCoeff(m_coeffV, ClampX(c2x), ClampY(c2y));
            float uB = (float)(alpha1 * u1 + alpha2 * u2);
            float vB = (float)(alpha1 * v1 + alpha2 * v2);
            float spd = sqrtf(uB*uB + vB*vB);
            animLog("[TRACE-CUR] frame=%d px(%d,%d) "
                "C1=(%.3f,%.3f) C2=(%.3f,%.3f) "
                "u1=%.4f u2=%.4f v1=%.4f v2=%.4f "
                "a1=%.3f a2=%.3f uB=%.4f vB=%.4f spd=%.4f",
                m_frame, pi, pj,
                c1x, c1y, c2x, c2y,
                u1, u2, v1, v2,
                alpha1, alpha2, uB, vB, spd);
        }
    }
}

//===================================================================
// Get advected grid for current frame (main entry point)
//===================================================================
double** ncdfAnimate::GetAdvectedGrid() {
    if (!m_gui) { animLog("GetAdvectedGrid: m_gui=NULL"); return NULL; }
    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;
    if (ni < 3 || nj < 3) { animLog("GetAdvectedGrid: dims too small %dx%d", ni, nj); return NULL; }

    if (!m_hasDisp) {
        if (!ComputeDisplacementMap()) { animLog("GetAdvectedGrid: displacement failed"); return NULL; }
    }

    if (!m_hasCoeffs || m_srcW != ni || m_srcH != nj) {
        if (!BuildSourceFields()) {
            animLog("GetAdvectedGrid: BuildSourceFields failed, trying ext grids (extNi=%d extNj=%d)", m_extNi, m_extNj);
            if (m_extUGrid && m_extVGrid && m_extNi == ni && m_extNj == nj) {
                if (!BuildSourceFields(m_extUGrid, m_extVGrid, m_extNi, m_extNj)) {
                    animLog("GetAdvectedGrid: external BuildSourceFields also failed"); return NULL;
                }
            } else {
                animLog("GetAdvectedGrid: no external grids available"); return NULL;
            }
        }
    }

    if (!m_hasCoordFields || m_coordW != ni || m_coordH != nj) {
        InitCoordFields();
    }

    // Allocate all three output grids
    if (!m_advectedSpeed || m_advW != ni || m_advH != nj) {
        FreeGrid(m_advectedU, m_advH);
        FreeGrid(m_advectedV, m_advH);
        FreeGrid(m_advectedSpeed, m_advH);
        AllocateGrid(m_advectedU, nj, ni);
        AllocateGrid(m_advectedV, nj, ni);
        AllocateGrid(m_advectedSpeed, nj, ni);
        m_advW = ni; m_advH = nj;
    }

    // GPU coordinate field update (replaces CPU UpdateCoordFields)
    if (!UpdateCoordFieldsGPU()) {
        animLog("GetAdvectedGrid: GPU update failed, skipping frame");
        return NULL;
    }
    SampleAndBlend();

    // DEBUG: RK4 tracer — advect seed points, sample scalar, map to LUT
    if (m_hasDisp && m_hasCoeffs && m_tracerSeedCount > 0) {
        if (!m_tracerInitialized) {
            // Initialize tracer positions from seed normalized coords
            for (int t = 0; t < m_tracerSeedCount; t++) {
                m_tracerCurX[t] = m_tracerSeeds[t].nx * (ni - 1);
                m_tracerCurY[t] = m_tracerSeeds[t].ny * (nj - 1);
            }
            m_tracerInitialized = true;
        }

        for (int t = 0; t < m_tracerSeedCount; t++) {
            float px = m_tracerCurX[t], py = m_tracerCurY[t];

            // RK4 step using displacement map (bilinear interpolation)
            auto dispAt = [&](float x, float y, float& dx, float& dy) {
                x = ClampX(x); y = ClampY(y);
                int ix = (int)x, iy = (int)y;
                int ix1 = (ix + 1 < ni) ? ix + 1 : ix;
                int iy1 = (iy + 1 < nj) ? iy + 1 : nj;
                float fx = x - ix, fy = y - iy;
                dx = (1-fx)*(1-fy)*m_dispX[iy][ix] + fx*(1-fy)*m_dispX[iy][ix1]
                   + (1-fx)*fy*m_dispX[iy1][ix] + fx*fy*m_dispX[iy1][ix1];
                dy = (1-fx)*(1-fy)*m_dispY[iy][ix] + fx*(1-fy)*m_dispY[iy][ix1]
                   + (1-fx)*fy*m_dispY[iy1][ix] + fx*fy*m_dispY[iy1][ix1];
            };

            float k1x, k1y, k2x, k2y, k3x, k3y, k4x, k4y;
            dispAt(px, py, k1x, k1y);
            dispAt(px + k1x*0.5f, py + k1y*0.5f, k2x, k2y);
            dispAt(px + k2x*0.5f, py + k2y*0.5f, k3x, k3y);
            dispAt(px + k3x, py + k3y, k4x, k4y);

            float newPx = ClampX(px + (k1x + 2*k2x + 2*k3x + k4x) / 6.0f);
            float newPy = ClampY(py + (k1y + 2*k2y + 2*k3y + k4y) / 6.0f);

            // Sample B-spline coefficient at advected position
            float s1 = SampleSplineCoeff(m_coeffU, newPx, newPy);  // U component
            float s2 = SampleSplineCoeff(m_coeffV, newPx, newPy);  // V component
            float spd = sqrtf(s1*s1 + s2*s2);

            // Blend with C2 (simplified: only C1 for now)
            float blended = spd;  // full weight on C1

            animLog("[TRACER] frame=%d seed%d (%.1f,%.1f)->(%.1f,%.1f) "
                "disp=(%.3f,%.3f) U=%.4f V=%.4f spd=%.4f",
                m_frame, t, px, py, newPx, newPy,
                k1x, k1y, s1, s2, spd);

            m_tracerCurX[t] = newPx;
            m_tracerCurY[t] = newPy;
        }
    }

    // Note: m_frame is NOT incremented here.
    // The caller (ncdfoverlayfactory) advances the frame after both
    // GetAdvectedGrid() and GetAdvectedScalarGrid() have completed,
    // so that coord field updates (skip flags), U/V blending weights,
    // and scalar blending weights all use the same m_frame value.

    return m_advectedSpeed;
}

void ncdfAnimate::AdvanceFrame() {
    m_frame = (m_frame + 1) % m_period;
}

//===================================================================
// Scalar advection: sample source scalar field at advected coordinates
// B-spline coefficients are computed once from srcGrid, then reused each frame
//===================================================================
double** ncdfAnimate::GetAdvectedScalarGrid(float* const* srcGrid, int ni, int nj) {
    animLog("[SCALAR-ENTRY] srcGrid=%p ni=%d nj=%d hasCoord=%d coordW=%d coordH=%d hasCoeffs=%d",
        (void*)srcGrid, ni, nj, (int)m_hasCoordFields, m_coordW, m_coordH, (int)m_hasCoeffs);
    if (!srcGrid || ni < 3 || nj < 3) return NULL;
    if (!m_hasCoordFields || m_coordW != ni || m_coordH != nj) return NULL;
    // Note: scalar path uses its own m_scalarCoeff, not m_coeffU/V (U/V coefficients)
    // Note: coordinate fields are already updated by GetAdvectedGrid() — don't update again here

    // Compute scalar B-spline coefficients once (or when dimensions change)
    if (!m_scalarCoeff || m_scalarW != ni || m_scalarH != nj) {
        FreeFloatGrid(m_scalarCoeff, m_scalarH);
        FreeFloatGrid(m_scalarSrc, m_scalarH);
        AllocateFloatGrid(m_scalarCoeff, nj, ni);
        AllocateFloatGrid(m_scalarSrc, nj, ni);
        if (!m_scalarCoeff || !m_scalarSrc) return NULL;
        m_scalarW = ni; m_scalarH = nj;

        // Copy source scalar, fill invalid with nearest neighbor
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float v = srcGrid[j][i];
                m_scalarSrc[j][i] = isfinite(v) ? v : -1e30f;
            }
        for (int pass = 0; pass < 3; pass++)
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    if (m_scalarSrc[j][i] > -1e29f) continue;
                    float sum = 0; int cnt = 0;
                    for (int dj = -1; dj <= 1; dj++)
                        for (int di = -1; di <= 1; di++) {
                            int jj = j + dj, ii = i + di;
                            if (jj >= 0 && jj < nj && ii >= 0 && ii < ni && m_scalarSrc[jj][ii] > -1e29f) {
                                sum += m_scalarSrc[jj][ii]; cnt++;
                            }
                        }
                    if (cnt > 0) m_scalarSrc[j][i] = sum / cnt;
                }

        // B-spline prefilter
        BicubicSplinePrefilter(m_scalarCoeff, m_scalarSrc, nj, ni);
        animLog("scalar B-spline coefficients computed: %dx%d", ni, nj);
    }

    // Allocate output grid
    if (!m_advectedScalar || m_advScalarW != ni || m_advScalarH != nj) {
        FreeGrid(m_advectedScalar, m_advScalarH);
        AllocateGrid(m_advectedScalar, nj, ni);
        m_advScalarW = ni; m_advScalarH = nj;
    }

    // Sample scalar at advected coordinate positions, blend dual fields
    // Patent sine weights: w1=sin(pi*(t%T)/T), w2=sin(pi*((t+T/2)%T)/T)
    // Both always >= 0, always blend (never exclusive)
    int tMod = m_frame % m_period;
    double w1 = sin(M_PI * tMod / (double)m_period);
    int tMod2 = (m_frame + m_period / 2) % m_period;
    double w2 = sin(M_PI * tMod2 / (double)m_period);
    double wsum = w1 + w2;
    double alpha1 = w1 / wsum;
    double alpha2 = w2 / wsum;

    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            if (m_scalarSrc[j][i] < -1e29f) {
                m_advectedScalar[j][i] = ncdf_NOTDEF;
                continue;
            }
            float s1 = SampleSplineCoeff(m_scalarCoeff, ClampX(m_c1x[j][i]), ClampY(m_c1y[j][i]));
            float s2 = SampleSplineCoeff(m_scalarCoeff, ClampX(m_c2x[j][i]), ClampY(m_c2y[j][i]));
            // Keep in coefficient domain; shader converts to physical via dataMin/dataMax
            m_advectedScalar[j][i] = alpha1 * s1 + alpha2 * s2;
        }

    // DEBUG: trace pixel (100,100) — near data edge where gradients are steeper
    {
        int pi = 100, pj = 100;
        if (pi < ni && pj < nj && m_scalarSrc[pj][pi] > -1e29f) {
            float c1x = m_c1x[pj][pi], c1y = m_c1y[pj][pi];
            float c2x = m_c2x[pj][pi], c2y = m_c2y[pj][pi];
            float s1 = SampleSplineCoeff(m_scalarCoeff, ClampX(c1x), ClampY(c1y));
            float s2 = SampleSplineCoeff(m_scalarCoeff, ClampX(c2x), ClampY(c2y));
            float blended = (float)(alpha1 * s1 + alpha2 * s2);
            float srcVal = m_scalarSrc[pj][pi];
            animLog("[TRACE] frame=%d pixel(%d,%d) src=%.4f "
                "C1=(%.3f,%.3f)->s1=%.4f C2=(%.3f,%.3f)->s2=%.4f "
                "a1=%.3f a2=%.3f blend=%.4f",
                m_frame, pi, pj, srcVal,
                c1x, c1y, s1, c2x, c2y, s2,
                alpha1, alpha2, blended);
        }
    }

    return m_advectedScalar;
}

//===================================================================
// GPU Animation Path
//===================================================================

bool ncdfAnimate::InitGPU(int w, int h) {
    if (m_gpuReady) return true;
    if (!ncdf_animate_shader_init()) {
        animLog("GPU: shader init failed");
        return false;
    }
    m_fbo = ncdf_animate_create_fbo();
    if (!m_fbo) { animLog("GPU: FBO creation failed"); return false; }

    // Displacement map (RGBA8: dual-channel encoded, GL_NEAREST)
    m_texDisp = ncdf_animate_create_encoded_texture(w, h);
    // Ping-pong coordinate field textures (RGBA8: dual-channel, GL_NEAREST)
    m_texCoordA1 = ncdf_animate_create_encoded_texture(w, h);
    m_texCoordA2 = ncdf_animate_create_encoded_texture(w, h);
    m_texCoordB1 = ncdf_animate_create_encoded_texture(w, h);
    m_texCoordB2 = ncdf_animate_create_encoded_texture(w, h);
    // Result texture (RGBA8 for final display, GL_LINEAR)
    m_texResult = ncdf_animate_create_rgba_texture(w, h);

    if (!m_texDisp || !m_texCoordA1 || !m_texCoordA2 ||
        !m_texCoordB1 || !m_texCoordB2 || !m_texResult) {
        animLog("GPU: texture creation failed");
        CleanupGPU();
        return false;
    }

    m_gpuReady = true;
    animLog("GPU: initialized %dx%d, fbo=%u", w, h, m_fbo);
    return true;
}

void ncdfAnimate::CleanupGPU() {
    // Mark for deferred deletion (actual GL calls happen in render thread)
    m_gpuDeletePending = true;
    m_gpuReady = false;
}

// Called at start of RenderAnimationFrame (render thread has GL context)
void ncdfAnimate::FlushPendingGLDeletes() {
    if (!m_gpuDeletePending) return;
    if (m_texDisp) { glDeleteTextures(1, &m_texDisp); m_texDisp = 0; }
    if (m_texCoordA1) { glDeleteTextures(1, &m_texCoordA1); m_texCoordA1 = 0; }
    if (m_texCoordA2) { glDeleteTextures(1, &m_texCoordA2); m_texCoordA2 = 0; }
    if (m_texCoordB1) { glDeleteTextures(1, &m_texCoordB1); m_texCoordB1 = 0; }
    if (m_texCoordB2) { glDeleteTextures(1, &m_texCoordB2); m_texCoordB2 = 0; }
    if (m_texResult) { glDeleteTextures(1, &m_texResult); m_texResult = 0; }
    if (m_fbo) { ncdf_animate_delete_fbo(m_fbo); m_fbo = 0; }
    m_gpuDeletePending = false;
}

// Encode float coordinate [0, gridDim-1] to normalized [0,1] then to R+G dual-channel
static inline void encodeCoord(unsigned char& r, unsigned char& g, float coord, float gridDim) {
    float norm = (gridDim > 1.0f) ? coord / (gridDim - 1.0f) : 0.0f;
    if (norm < 0.0f) norm = 0.0f; if (norm > 1.0f) norm = 1.0f;
    unsigned int val = (unsigned int)(norm * 65535.0f + 0.5f);
    r = (unsigned char)(val >> 8);
    g = (unsigned char)(val & 0xFF);
}

bool ncdfAnimate::UploadDisplacementMap() {
    if (!m_hasDisp || !m_dispX || !m_dispY) return false;
    int w = m_dispW, h = m_dispH;

    // Pack displacement as RGBA8: R+G=dx (normalized), B+A=dy (normalized)
    unsigned char* buf = new(std::nothrow) unsigned char[w * h * 4];
    if (!buf) return false;
    // Normalize displacement to [0,1]: disp ranges from -maxDisp to +maxDisp
    float dispScale = m_maxDisp > 0 ? m_maxDisp : 1.0f;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int idx = (j * w + i) * 4;
            float normDx = (m_dispX[j][i] / dispScale + 1.0f) * 0.5f;  // [-1,1] → [0,1]
            float normDy = (m_dispY[j][i] / dispScale + 1.0f) * 0.5f;
            unsigned int ix = (unsigned int)(normDx * 65535.0f + 0.5f);
            unsigned int iy = (unsigned int)(normDy * 65535.0f + 0.5f);
            buf[idx + 0] = (unsigned char)(ix >> 8);
            buf[idx + 1] = (unsigned char)(ix & 0xFF);
            buf[idx + 2] = (unsigned char)(iy >> 8);
            buf[idx + 3] = (unsigned char)(iy & 0xFF);
        }
    }
    glBindTexture(GL_TEXTURE_2D, m_texDisp);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    delete[] buf;
    animLog("GPU: displacement map uploaded %dx%d", w, h);
    return true;
}

bool ncdfAnimate::UploadCoordFields() {
    if (!m_hasCoordFields || !m_c1x || !m_c1y) return false;
    int w = m_coordW, h = m_coordH;
    float gw = (float)w, gh = (float)h;
    animLog("[UPLOAD-START] w=%d h=%d c1x[240][180]=%.3f c2x[240][180]=%.3f",
        w, h, (h>240 && w>180) ? m_c1x[240][180] : -1.0f,
        (h>240 && w>180) ? m_c2x[240][180] : -1.0f);

    unsigned char* buf = new(std::nothrow) unsigned char[w * h * 4];
    if (!buf) return false;

    // Upload C1 to coordA1
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int idx = (j * w + i) * 4;
            encodeCoord(buf[idx+0], buf[idx+1], m_c1x[j][i], gw);
            encodeCoord(buf[idx+2], buf[idx+3], m_c1y[j][i], gh);
        }
    glBindTexture(GL_TEXTURE_2D, m_texCoordA1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    // Upload C2 to coordB1
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int idx = (j * w + i) * 4;
            encodeCoord(buf[idx+0], buf[idx+1], m_c2x[j][i], gw);
            encodeCoord(buf[idx+2], buf[idx+3], m_c2y[j][i], gh);
        }
    // DEBUG: check C2 at multiple positions
    animLog("[UPLOAD-C2] dim=%dx%d c2x[0][0]=%.3f c2x[240][180]=%.3f c2x[480][359]=%.3f",
        w, h, m_c2x[0][0], (h>240 && w>180) ? m_c2x[240][180] : -1.0f,
        (h>480 && w>359) ? m_c2x[480][359] : -1.0f);
    glBindTexture(GL_TEXTURE_2D, m_texCoordB1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    delete[] buf;

    // DEBUG: verify upload at multiple points
    auto pt = [&](const char* lbl, int jj, int ii) {
        animLog("[UPLOAD-VERIFY] C2 %s pixel(%d,%d)=(%.2f,%.2f) norm=(%.4f,%.4f)",
            lbl, ii, jj, m_c2x[jj][ii], m_c2y[jj][ii],
            m_c2x[jj][ii]/(gw-1), m_c2y[jj][ii]/(gh-1));
    };
    pt("TL",   h/4,   w/4);
    pt("TR",   h/4,   w*3/4);
    pt("CC",   h/2,   w/2);
    pt("BL",   h*3/4, w/4);
    pt("BR",   h*3/4, w*3/4);

    animLog("GPU: coord fields uploaded %dx%d, C2 center=(%.3f,%.3f)", w, h,
        (h>240 && w>180) ? m_c2x[240][180] : -1.0f,
        (h>240 && w>180) ? m_c2y[240][180] : -1.0f);
    return true;
}

void ncdfAnimate::RenderAnimationFrame(PlugIn_ViewPort *vp, GLuint scalarTexID, int scalarW, int scalarH,
                                        GLuint lutTexID, float coefMin, float coefMax,
                                        float lutMin, float lutRange) {
    animLog("RenderAnimationFrame: initialized=%d hasDisp=%d gui=%p scalarTex=%u scalarW=%d scalarH=%d lutTex=%u",
        (int)m_initialized, (int)m_hasDisp, (void*)m_gui, scalarTexID, scalarW, scalarH, lutTexID);
    if (!m_initialized || !m_hasDisp || !m_gui) { animLog("RenderAnimationFrame: early return"); return; }

    // Flush any pending GL deletes from previous CleanupGPU calls
    FlushPendingGLDeletes();

    int ni = m_dispW, nj = m_dispH;
    if (ni < 2 || nj < 2) { animLog("GPU: invalid dims ni=%d nj=%d", ni, nj); return; }

    // Lazy GPU init
    if (!m_gpuReady) {
        animLog("GPU: m_gpuReady=false, calling InitGPU(%d, %d)", ni, nj);
        if (!InitGPU(ni, nj)) { animLog("GPU: InitGPU FAILED"); return; }
        animLog("GPU: InitGPU OK, uploading displacement map");
        if (!UploadDisplacementMap()) { animLog("GPU: UploadDisplacementMap FAILED"); return; }
        animLog("GPU: uploading coord fields");
        if (!UploadCoordFields()) { animLog("GPU: UploadCoordFields FAILED"); return; }
        animLog("GPU: all uploads done, starting animation");
    }

    if (!ncdf_animate_shader_initialized()) {
        animLog("GPU: shader not initialized, calling init");
        if (!ncdf_animate_shader_init()) { animLog("GPU: shader init FAILED"); return; }
    }
    animLog("GPU: shader ready, running passes");

    // Save GL state
    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFBO);
    GLint prevProg; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glPushAttrib(GL_VIEWPORT_BIT | GL_ENABLE_BIT | GL_SCISSOR_BIT);

    // Determine ping-pong phase (even frames: read from 1 write to 2, odd: reverse)
    bool evenFrame = (m_frame % 2 == 0);
    GLuint readA = evenFrame ? m_texCoordA1 : m_texCoordA2;
    GLuint writeA = evenFrame ? m_texCoordA2 : m_texCoordA1;
    GLuint readB = evenFrame ? m_texCoordB1 : m_texCoordB2;
    GLuint writeB = evenFrame ? m_texCoordB2 : m_texCoordB1;

    // === Pass 1: Update coordinate field A (C1) ===
    ncdf_animate_use_program(ncdf_animate_get_update_program());
    AnimUniforms uUpd = ncdf_animate_get_update_uniforms();
    ncdf_animate_uniform_2f(uUpd.texSize, (float)ni, (float)nj);
    ncdf_animate_uniform_1f(uUpd.maxDisp, m_maxDisp);

    ncdf_animate_active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, readA);
    ncdf_animate_uniform_1i(uUpd.uTex0, 0);

    ncdf_animate_active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texDisp);
    ncdf_animate_uniform_1i(uUpd.uTex1, 1);

    ncdf_animate_bind_fbo(m_fbo, writeA);
    glViewport(0, 0, ni, nj);
    ncdf_animate_draw_quad();

    // === Pass 2: Update coordinate field B (C2) ===
    ncdf_animate_active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, readB);
    ncdf_animate_uniform_1i(uUpd.uTex0, 0);

    ncdf_animate_bind_fbo(m_fbo, writeB);
    ncdf_animate_draw_quad();

    // === Pass 3: Advect + blend + LUT display ===
    ncdf_animate_bind_fbo(m_fbo, m_texResult);
    glViewport(0, 0, ni, nj);

    // Save and set identity matrix for FBO rendering (NDC coordinates)
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    // Use advect shader
    ncdf_animate_use_program(ncdf_animate_get_advect_program());
    AnimUniforms uAdv = ncdf_animate_get_advect_uniforms();
    ncdf_animate_uniform_2f(uAdv.scalarSize, (float)scalarW, (float)scalarH);
    ncdf_animate_uniform_1i(uAdv.frame, m_frame);
    ncdf_animate_uniform_1i(uAdv.period, m_period);
    ncdf_animate_active_texture(0x84C0);
    glBindTexture(GL_TEXTURE_2D, scalarTexID);
    ncdf_animate_uniform_1i(uAdv.uTex0, 0);
    ncdf_animate_active_texture(0x84C1);
    glBindTexture(GL_TEXTURE_2D, m_texCoordA1);
    ncdf_animate_uniform_1i(uAdv.uTex1, 1);
    ncdf_animate_active_texture(0x84C2);
    glBindTexture(GL_TEXTURE_2D, m_texCoordB1);
    ncdf_animate_uniform_1i(uAdv.uTex2, 2);
    ncdf_animate_active_texture(0x84C3);
    glBindTexture(GL_TEXTURE_2D, lutTexID);
    ncdf_animate_uniform_1i(uAdv.uColorLUT, 3);
    ncdf_animate_uniform_1f(uAdv.dataMin, coefMin);
    ncdf_animate_uniform_1f(uAdv.dataMax, coefMax);
    ncdf_animate_uniform_1f(uAdv.lutMin, lutMin);
    ncdf_animate_uniform_1f(uAdv.lutMax, lutMin + lutRange);

    // DEBUG: log all uniform values
    animLog("Pass3: frame=%d scalarTex=%u lutTex=%u coordA=%u coordB=%u "
            "dataMin=%.3f dataMax=%.3f lutMin=%.3f lutMax=%.3f "
            "scalarSize=%dx%d uTex0=%d uTex1=%d uTex2=%d uLUT=%d",
        m_frame, scalarTexID, lutTexID, m_texCoordA1, m_texCoordB1,
        coefMin, coefMax, lutMin, lutMin + lutRange,
        scalarW, scalarH, uAdv.uTex0, uAdv.uTex1, uAdv.uTex2, uAdv.uColorLUT);

    ncdf_animate_draw_quad();

    // DEBUG: read back FBO pixels to verify shader output
    static int s_pass3dbg = 0;
    if (s_pass3dbg < 3) {
        unsigned char px[4];
        glReadPixels(ni/2, nj/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("Pass3: FBO center(%d,%d) RGBA=(%d,%d,%d,%d)", ni/2, nj/2, px[0], px[1], px[2], px[3]);
        glReadPixels(ni/4, nj/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("Pass3: FBO quarter(%d,%d) RGBA=(%d,%d,%d,%d)", ni/4, nj/4, px[0], px[1], px[2], px[3]);
        s_pass3dbg++;
    }

    ncdf_animate_unbind_fbo();

    // Restore matrices
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();

    // Note: m_frame is advanced by GetAdvectedGrid() (the caller), not here.
    // This avoids double-increment when both are called each render cycle.

    // Restore GL state
    glPopAttrib();
    ncdf_animate_use_program(prevProg);
}

//===================================================================
// GPU coordinate field update: ping-pong FBO + readback to CPU
// Replaces CPU UpdateCoordFields() with GPU shader passes
//===================================================================
bool ncdfAnimate::UpdateCoordFieldsGPU() {
    if (!m_hasDisp || !m_hasCoordFields) return false;
    int ni = m_coordW, nj = m_coordH;
    if (ni < 2 || nj < 2) return false;

    // Lazy GPU init
    if (!m_gpuReady) {
        if (!InitGPU(ni, nj)) return false;
        if (!UploadDisplacementMap()) return false;
        if (!UploadCoordFields()) return false;
        animLog("GPU: first-time init done %dx%d", ni, nj);
    }

    if (!ncdf_animate_shader_initialized()) {
        if (!ncdf_animate_shader_init()) return false;
    }

    // Determine skip flags (patent: sine weights)
    // w1=max(sin(pi*t/T),0), w2=max(-sin(pi*t/T),0)
    // w1=0 at t=0 -> C1 resets; w2=0 at t=T/2 -> C2 resets
    bool skipC1 = (m_frame == 0);              // w1=0 -> C1 reset
    bool skipC2 = (m_frame == m_period / 2);   // w2=0 -> C2 reset
    if (skipC1 && skipC2) return true;         // only at T=0 edge case

    // Ping-pong: even frame reads from1, writes to2; odd reverses
    bool evenFrame = (m_frame % 2 == 0);
    GLuint readA = evenFrame ? m_texCoordA1 : m_texCoordA2;
    GLuint writeA = evenFrame ? m_texCoordA2 : m_texCoordA1;
    GLuint readB = evenFrame ? m_texCoordB1 : m_texCoordB2;
    GLuint writeB = evenFrame ? m_texCoordB2 : m_texCoordB1;

    // Save GL state
    GLint prevFBO; glGetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &prevFBO);
    GLint prevProg; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    GLint prevVP[4]; glGetIntegerv(GL_VIEWPORT, prevVP);

    // Set identity matrix for FBO rendering (NDC coordinates)
    // Without this, gl_ModelViewProjectionMatrix in the vertex shader uses
    // OpenCPN's map projection, which clips the NDC quad and nothing is rasterized.
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    // === Pass 1: Update C1 ===
    ncdf_animate_use_program(ncdf_animate_get_update_program());
    AnimUniforms uUpd = ncdf_animate_get_update_uniforms();
    ncdf_animate_uniform_2f(uUpd.texSize, (float)ni, (float)nj);
    ncdf_animate_uniform_1f(uUpd.maxDisp, m_maxDisp);

    // Bind displacement texture to unit1
    ncdf_animate_active_texture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texDisp);
    ncdf_animate_uniform_1i(uUpd.uTex1, 1);

    ncdf_animate_uniform_1i(uUpd.forceIdentity, skipC1 ? 1 : 0);
    if (!skipC1) {
        ncdf_animate_active_texture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, readA);
        ncdf_animate_uniform_1i(uUpd.uTex0, 0);
    }
    ncdf_animate_bind_fbo(m_fbo, writeA);
    glViewport(0, 0, ni, nj);

    // Ensure readback buffer is allocated (before first use)
    {
        int needed = ni * nj * 4;
        if (!m_readbackBuf || m_readbackBufSize < needed) {
            delete[] m_readbackBuf;
            m_readbackBuf = new unsigned char[needed];
            m_readbackBufSize = needed;
        }
    }

    ncdf_animate_draw_quad();

    // --- Patent verify 1: reset frame forceIdentity=1 should output identity ---
    if (skipC1) {
        unsigned char px[4];
        glReadPixels(ni/2, nj/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        float decodedX = (px[0]*256+px[1]) / 65535.0f * (ni-1);
        float decodedY = (px[2]*256+px[3]) / 65535.0f * (nj-1);
        float expectX = (float)(ni/2), expectY = (float)(nj/2);
        animLog("[PATENT-C1-RESET] frame=%d forceIdentity=1 | center decoded=(%.2f,%.2f) expect=(%.2f,%.2f) err=(%.3f,%.3f)",
            m_frame, decodedX, decodedY, expectX, expectY, decodedX-expectX, decodedY-expectY);
        // Sample corners too
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PATENT-C1-RESET] corner(0,0) decoded=(%.2f,%.2f) expect=(0.00,0.00)",
            (px[0]*256+px[1])/65535.0f*(ni-1), (px[2]*256+px[3])/65535.0f*(nj-1));
        glReadPixels(ni-1, nj-1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PATENT-C1-RESET] corner(%d,%d) decoded=(%.2f,%.2f) expect=(%.2f,%.2f)",
            ni-1, nj-1,
            (px[0]*256+px[1])/65535.0f*(ni-1), (px[2]*256+px[3])/65535.0f*(nj-1),
            (float)(ni-1), (float)(nj-1));
    }

    // DEBUG: verify Pass 1 FBO has data (sample center pixel without full readback)
    if (!skipC1) {
        unsigned char px[4];
        glReadPixels(ni/2, nj/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PASS1-CHECK] frame=%d center RGBA=(%d,%d,%d,%d) decoded=(%.2f,%.2f)",
            m_frame, px[0], px[1], px[2], px[3],
            (px[0]*256+px[1])/65535.0f * (ni-1),
            (px[2]*256+px[3])/65535.0f * (nj-1));
    }

    // Read C1 while FBO bound
    if (!skipC1) {
        glReadPixels(0, 0, ni, nj, GL_RGBA, GL_UNSIGNED_BYTE, m_readbackBuf);
        int ci = ni/2, cj = nj/2;
        int cidx = (cj * ni + ci) * 4;
        animLog("[C1-READ] frame=%d center raw RGBA=(%d,%d,%d,%d) decoded=(%.3f,%.3f)",
            m_frame,
            m_readbackBuf[cidx], m_readbackBuf[cidx+1],
            m_readbackBuf[cidx+2], m_readbackBuf[cidx+3],
            (m_readbackBuf[cidx]*256+m_readbackBuf[cidx+1])/65535.0f * (ni-1),
            (m_readbackBuf[cidx+2]*256+m_readbackBuf[cidx+3])/65535.0f * (nj-1));
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                int idx = (j * ni + i) * 4;
                float normX = (m_readbackBuf[idx] * 256 + m_readbackBuf[idx+1]) / 65535.0f;
                float normY = (m_readbackBuf[idx+2] * 256 + m_readbackBuf[idx+3]) / 65535.0f;
                m_c1x[j][i] = normX * (ni - 1);
                m_c1y[j][i] = normY * (nj - 1);
            }
    }

    // === Pass 2: Update C2 ===
    ncdf_animate_uniform_1i(uUpd.forceIdentity, skipC2 ? 1 : 0);
    ncdf_animate_bind_fbo(m_fbo, writeB);
    if (!skipC2) {
        ncdf_animate_active_texture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, readB);
        ncdf_animate_uniform_1i(uUpd.uTex0, 0);
    }
    // DEBUG: verify state before draw
    {
        GLint curProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
        GLint fboBound = 0; glGetIntegerv(0x8CA6, &fboBound);
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        animLog("[PASS2-PRE] prog=%d fbo=%d vp=%d,%d,%d,%d forceId_loc=%d skipC2=%d",
            curProg, fboBound, vp[0], vp[1], vp[2], vp[3], uUpd.forceIdentity, (int)skipC2);
    }
    ncdf_animate_draw_quad();

    // --- Patent verify 2: C2 reset frame forceIdentity=1 should output identity ---
    if (skipC2) {
        unsigned char px[4];
        glReadPixels(ni/2, nj/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        float decodedX = (px[0]*256+px[1]) / 65535.0f * (ni-1);
        float decodedY = (px[2]*256+px[3]) / 65535.0f * (nj-1);
        animLog("[PATENT-C2-RESET] frame=%d forceIdentity=1 | center decoded=(%.2f,%.2f) expect=(%.2f,%.2f) err=(%.3f,%.3f)",
            m_frame, decodedX, decodedY, (float)(ni/2), (float)(nj/2), decodedX-(float)(ni/2), decodedY-(float)(nj/2));
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PATENT-C2-RESET] corner(0,0) decoded=(%.2f,%.2f) expect=(0.00,0.00)",
            (px[0]*256+px[1])/65535.0f*(ni-1), (px[2]*256+px[3])/65535.0f*(nj-1));
        glReadPixels(ni-1, nj-1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PATENT-C2-RESET] corner(%d,%d) decoded=(%.2f,%.2f) expect=(%.2f,%.2f)",
            ni-1, nj-1,
            (px[0]*256+px[1])/65535.0f*(ni-1), (px[2]*256+px[3])/65535.0f*(nj-1),
            (float)(ni-1), (float)(nj-1));
    }

    // DEBUG: verify Pass 2 FBO has data
    if (!skipC2) {
        unsigned char px[4];
        glReadPixels(ni/2, nj/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        animLog("[PASS2-CHECK] frame=%d center RGBA=(%d,%d,%d,%d) decoded=(%.2f,%.2f)",
            m_frame, px[0], px[1], px[2], px[3],
            (px[0]*256+px[1])/65535.0f * (ni-1),
            (px[2]*256+px[3])/65535.0f * (nj-1));
    }

    // Read C2 while FBO bound
    if (!skipC2) {
        glReadPixels(0, 0, ni, nj, GL_RGBA, GL_UNSIGNED_BYTE, m_readbackBuf);
        int ci = ni/2, cj = nj/2;
        int cidx = (cj * ni + ci) * 4;
        animLog("[C2-READ] frame=%d center raw RGBA=(%d,%d,%d,%d) decoded=(%.3f,%.3f)",
            m_frame,
            m_readbackBuf[cidx], m_readbackBuf[cidx+1],
            m_readbackBuf[cidx+2], m_readbackBuf[cidx+3],
            (m_readbackBuf[cidx]*256+m_readbackBuf[cidx+1])/65535.0f * (ni-1),
            (m_readbackBuf[cidx+2]*256+m_readbackBuf[cidx+3])/65535.0f * (nj-1));
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                int idx = (j * ni + i) * 4;
                float normX = (m_readbackBuf[idx] * 256 + m_readbackBuf[idx+1]) / 65535.0f;
                float normY = (m_readbackBuf[idx+2] * 256 + m_readbackBuf[idx+3]) / 65535.0f;
                m_c2x[j][i] = normX * (ni - 1);
                m_c2y[j][i] = normY * (nj - 1);
            }
    }

    // Copy FBO to read textures for next frame's ping-pong
    ncdf_animate_unbind_fbo();
    GLuint copyToA = evenFrame ? m_texCoordA1 : m_texCoordA2;
    GLuint copyToB = evenFrame ? m_texCoordB1 : m_texCoordB2;
    if (!skipC1) {
        ncdf_animate_bind_fbo(m_fbo, writeA);
        glBindTexture(GL_TEXTURE_2D, copyToA);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ni, nj);
    }
    if (!skipC2) {
        ncdf_animate_bind_fbo(m_fbo, writeB);
        glBindTexture(GL_TEXTURE_2D, copyToB);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ni, nj);
    }
    ncdf_animate_unbind_fbo();

    // Restore matrices
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();

    // DEBUG: multi-point sampling of both coordinate fields
    {
        struct SamplePt { int i; int j; const char* label; };
        SamplePt pts[] = {
            { ni/4,   nj/4,   "TL" },
            { ni*3/4, nj/4,   "TR" },
            { ni/2,   nj/2,   "CC" },
            { ni/4,   nj*3/4, "BL" },
            { ni*3/4, nj*3/4, "BR" },
        };
        // C1 field — should update every frame (skipC1 only at frame==0)
        char buf[512];
        int off = 0;
        off += snprintf(buf+off, sizeof(buf)-off, "[C1-FIELD] frame=%d skipC1=%d |", m_frame, (int)skipC1);
        for (auto& p : pts)
            off += snprintf(buf+off, sizeof(buf)-off, " %s=(%.2f,%.2f)", p.label, m_c1x[p.j][p.i], m_c1y[p.j][p.i]);
        animLog("%s", buf);

        // C2 field — should update every frame (skipC2 only at frame==period/2)
        off = 0;
        off += snprintf(buf+off, sizeof(buf)-off, "[C2-FIELD] frame=%d skipC2=%d |", m_frame, (int)skipC2);
        for (auto& p : pts)
            off += snprintf(buf+off, sizeof(buf)-off, " %s=(%.2f,%.2f)", p.label, m_c2x[p.j][p.i], m_c2y[p.j][p.i]);
        animLog("%s", buf);

        // Delta from identity — how far each field has drifted
        off = 0;
        off += snprintf(buf+off, sizeof(buf)-off, "[COORD-DELTA] frame=%d | C1:", m_frame);
        for (auto& p : pts) {
            float dx = m_c1x[p.j][p.i] - (float)p.i;
            float dy = m_c1y[p.j][p.i] - (float)p.j;
            off += snprintf(buf+off, sizeof(buf)-off, " %s(%.2f,%.2f)", p.label, dx, dy);
        }
        off += snprintf(buf+off, sizeof(buf)-off, " | C2:");
        for (auto& p : pts) {
            float dx = m_c2x[p.j][p.i] - (float)p.i;
            float dy = m_c2y[p.j][p.i] - (float)p.j;
            off += snprintf(buf+off, sizeof(buf)-off, " %s(%.2f,%.2f)", p.label, dx, dy);
        }
        animLog("%s", buf);

        // --- Patent verify 3: mixing weight verification ---
        // Patent: w1=sin(pi*(t%T)/T), w2=sin(pi*((t+T/2)%T)/T)
        // Both always >=0, always blend (never exclusive)
        {
            int t = m_frame;
            int tMod = t % m_period;
            double w1 = sin(M_PI * tMod / (double)m_period);
            int tMod2 = (t + m_period / 2) % m_period;
            double w2 = sin(M_PI * tMod2 / (double)m_period);
            double wsum = w1 + w2;
            double a1 = w1 / wsum;
            double a2 = w2 / wsum;

            // Reset: w1=0 at t=0 -> C1 reset; w2=0 at t=T/2 -> C2 reset
            bool shouldResetC1 = (t == 0);
            bool shouldResetC2 = (t == m_period / 2);
            bool match = (shouldResetC1 == skipC1) && (shouldResetC2 == skipC2);

            animLog("[PATENT-WEIGHT] t=%d/%d | w1=%.4f w2=%.4f a1=%.4f a2=%.4f | "
                    "expect:skipC1=%d skipC2=%d | code:skipC1=%d skipC2=%d | %s",
                t, m_period, w1, w2, a1, a2,
                (int)shouldResetC1, (int)shouldResetC2,
                (int)skipC1, (int)skipC2,
                match ? "MATCH=YES" : "MATCH=NO");
        }
    }

    // Restore GL state
    ncdf_animate_unbind_fbo();
    ncdf_animate_bind_fbo_only(prevFBO);
    glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
    ncdf_animate_use_program(prevProg);

    return true;
}
