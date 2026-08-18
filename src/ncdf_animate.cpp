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
#include <cmath>
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
      m_coeffU(NULL), m_coeffV(NULL), m_hasCoeffs(false),
      m_advectedU(NULL), m_advectedV(NULL), m_advectedSpeed(NULL),
      m_advW(0), m_advH(0)
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
    return true;
}

void ncdfAnimate::Cleanup() {
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

//===================================================================
// Coordinate field management
//===================================================================
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

void ncdfAnimate::UpdateCoordFields() {
    if (!m_hasCoordFields || !m_hasDisp) return;
    int ni = m_coordW, nj = m_coordH;

    double phase = M_PI * m_frame / (double)m_period;
    double w1_raw = fmax(cos(phase), 0.0);
    double w2_raw = fmax(-cos(phase), 0.0);

    bool skipC1 = false, skipC2 = false;
    if (w1_raw <= 1e-10) { ResetToIdentity(m_c1x, m_c1y); skipC1 = true; }
    if (w2_raw <= 1e-10) { ResetToIdentity(m_c2x, m_c2y); skipC2 = true; }

    auto composeField = [&](float**& cx, float**& cy) {
        float** tmpX = NULL, **tmpY = NULL;
        AllocateFloatGrid(tmpX, nj, ni); AllocateFloatGrid(tmpY, nj, ni);
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float sx = ClampX(i + m_dispX[j][i]);
                float sy = ClampY(j + m_dispY[j][i]);
                tmpX[j][i] = SampleCoordField(cx, sx, sy, ni, nj);
                tmpY[j][i] = SampleCoordField(cy, sx, sy, ni, nj);
            }
        FreeFloatGrid(cx, nj); FreeFloatGrid(cy, nj);
        cx = tmpX; cy = tmpY;
    };

    if (!skipC1) composeField(m_c1x, m_c1y);
    if (!skipC2) composeField(m_c2x, m_c2y);
}

//===================================================================
// Sample u and v B-spline fields using coordinate fields, blend, compute speed
// u and v are interpolated INDEPENDENTLY (never pre-compute speed)
//===================================================================
void ncdfAnimate::SampleAndBlend() {
    if (!m_hasCoordFields || !m_hasCoeffs) return;
    if (!m_advectedU || !m_advectedV || !m_advectedSpeed) return;
    int ni = m_coordW, nj = m_coordH;

    double phase = M_PI * m_frame / (double)m_period;
    double w1 = fmax(cos(phase), 0.0);
    double w2 = fmax(-cos(phase), 0.0);
    double wsum = w1 + w2;
    double alpha1 = (wsum > 1e-10) ? w1 / wsum : 0.5;
    double alpha2 = (wsum > 1e-10) ? w2 / wsum : 0.5;

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
}

//===================================================================
// Get advected grid for current frame (main entry point)
//===================================================================
double** ncdfAnimate::GetAdvectedGrid() {
    if (!m_gui) return NULL;
    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;
    if (ni < 3 || nj < 3) return NULL;

    if (!m_hasDisp) {
        if (!ComputeDisplacementMap()) return NULL;
    }

    if (!m_hasCoeffs || m_srcW != ni || m_srcH != nj) {
        if (!BuildSourceFields()) return NULL;
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

    UpdateCoordFields();
    SampleAndBlend();
    m_frame = (m_frame + 1) % m_period;

    return m_advectedSpeed;
}

void ncdfAnimate::AdvanceFrame() {
    m_frame = (m_frame + 1) % m_period;
}
