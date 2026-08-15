//===================================================================
// ncdf_animate.cpp — CPU-based flow animation
// Computes advected scalar grid each frame, passed to RenderGridOverlay
//===================================================================

#include "ncdf_animate.h"
#include "ncdf_pi.h"
#include "gui.h"
#include "ncdfdata.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>

static void animLog(const char* fmt, ...) {
    FILE* f = fopen("C:\\ProgramData\\opencpn\\ncdf_debug.log", "a");
    if (!f) return;
    fprintf(f, "[animate] ");
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fprintf(f, "\n"); fclose(f);
}

ncdfAnimate::ncdfAnimate()
    : m_initialized(false), m_plugin(NULL), m_gui(NULL),
      m_period(30), m_power(0.6f), m_maxDisp(2.0f), m_frame(0),
      m_dispX(NULL), m_dispY(NULL), m_hasDisp(false), m_dispW(0), m_dispH(0),
      m_advectedGrid(NULL), m_advW(0), m_advH(0), m_sourceGrid(NULL)
{
}

ncdfAnimate::~ncdfAnimate() { Cleanup(); }

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

bool ncdfAnimate::Init(ncdf_pi* plugin, MainDialog* gui) {
    if (m_initialized) return true;
    m_plugin = plugin;
    m_gui = gui;
    m_initialized = true;
    m_frame = 0;
    animLog("module initialized");
    return true;
}

void ncdfAnimate::Cleanup() {
    FreeGrid(m_dispX, m_dispH);
    FreeGrid(m_dispY, m_dispH);
    FreeGrid(m_advectedGrid, m_advH);
    m_hasDisp = false;
    m_initialized = false;
}

//===================================================================
// Compute displacement map (CPU, one-time)
//===================================================================
bool ncdfAnimate::ComputeDisplacementMap() {
    if (!m_gui || !m_gui->gridu || !m_gui->gridv) return false;
    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;
    if (ni < 3 || nj < 3) return false;

    // Free old
    FreeGrid(m_dispX, m_dispH);
    FreeGrid(m_dispY, m_dispH);

    if (!AllocateGrid(m_dispX, nj, ni)) return false;
    if (!AllocateGrid(m_dispY, nj, ni)) { FreeGrid(m_dispX, nj); return false; }

    double** gu = m_gui->gridu;
    double** gv = m_gui->gridv;

    // Find max velocity
    double vMax = 0;
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++)
            if (gu[j] && gv[j] && gu[j][i] != ncdf_NOTDEF && gv[j][i] != ncdf_NOTDEF)
                vMax = fmax(vMax, sqrt(gu[j][i]*gu[j][i] + gv[j][i]*gv[j][i]));
    if (vMax < 1e-10) vMax = 1.0;

    for (int j = 1; j < nj - 1; j++) {
        for (int i = 1; i < ni - 1; i++) {
            m_dispX[j][i] = 0; m_dispY[j][i] = 0;
            if (!gu[j] || !gv[j]) continue;
            double u = gu[j][i], v = gv[j][i];
            if (u == ncdf_NOTDEF || v == ncdf_NOTDEF) continue;
            double mag = sqrt(u*u + v*v);
            if (mag < 0.001) continue;

            // Visual velocity with power law
            double visSpeed = m_maxDisp * pow(mag / vMax, (double)m_power);
            double nx = u / mag, ny = v / mag;
            double vx = nx * visSpeed, vy = ny * visSpeed;

            // Simplified RK4: k1 + k2 at midpoint
            double k1x = -vx, k1y = -vy;
            double mx = i + k1x*0.5, my = j + k1y*0.5;
            int mi = (int)mx, mj = (int)my;
            double k2x = k1x, k2y = k1y;
            if (mi >= 0 && mi < ni-1 && mj >= 0 && mj < nj-1 && gu[mj] && gv[mj]) {
                int mi1 = (mi+1 < ni) ? mi+1 : mi;
                int mj1 = (mj+1 < nj) ? mj+1 : mj;
                double fu = mx - mi, fv = my - mj;
                double su = (1-fu)*(1-fv)*gu[mj][mi] + fu*(1-fv)*gu[mj][mi1]
                          + (1-fu)*fv*gu[mj1][mi] + fu*fv*gu[mj1][mi1];
                double sv = (1-fu)*(1-fv)*gv[mj][mi] + fu*(1-fv)*gv[mj][mi1]
                          + (1-fu)*fv*gv[mj1][mi] + fu*fv*gv[mj1][mi1];
                if (su != ncdf_NOTDEF && sv != ncdf_NOTDEF) {
                    double sm = sqrt(su*su + sv*sv);
                    if (sm > 0.001) {
                        double svs = m_maxDisp * pow(sm / vMax, (double)m_power);
                        k2x = -(su/sm) * svs; k2y = -(sv/sm) * svs;
                    }
                }
            }
            // Displacement in grid units
            m_dispX[j][i] = (k1x + k2x) * 0.5;
            m_dispY[j][i] = (k1y + k2y) * 0.5;
        }
    }

    m_dispW = ni; m_dispH = nj;
    m_hasDisp = true;
    animLog("displacement map %dx%d, vMax=%.3f", ni, nj, vMax);
    return true;
}

//===================================================================
// Compute advected grid for current frame
//===================================================================
void ncdfAnimate::ComputeAdvectedGrid() {
    if (!m_sourceGrid || !m_dispX || !m_dispY) return;
    int ni = m_dispW, nj = m_dispH;

    // Dual-path blending weights (patent: half-period phase offset)
    double phase = 2.0 * M_PI * m_frame / (double)m_period;
    double w1 = 0.5 + 0.5 * cos(phase);
    double w2 = 1.0 - w1;

    for (int j = 0; j < nj; j++) {
        for (int i = 0; i < ni; i++) {
            if (!m_advectedGrid[j]) continue;

            // Check source validity
            if (!m_sourceGrid[j] || m_sourceGrid[j][i] == ncdf_NOTDEF) {
                m_advectedGrid[j][i] = ncdf_NOTDEF;
                continue;
            }

            // Bilinear sample at displaced position A (forward)
            double ax = i + m_dispX[j][i];
            double ay = j + m_dispY[j][i];
            double valA = ncdf_NOTDEF;
            int axi = (int)ax, ayi = (int)ay;
            if (axi >= 0 && axi < ni-1 && ayi >= 0 && ayi < nj-1 &&
                m_sourceGrid[ayi] && m_sourceGrid[ayi+1]) {
                double fx = ax - axi, fy = ay - ayi;
                double s00 = m_sourceGrid[ayi][axi], s10 = m_sourceGrid[ayi][axi+1];
                double s01 = m_sourceGrid[ayi+1][axi], s11 = m_sourceGrid[ayi+1][axi+1];
                if (s00 != ncdf_NOTDEF && s10 != ncdf_NOTDEF &&
                    s01 != ncdf_NOTDEF && s11 != ncdf_NOTDEF) {
                    valA = (1-fx)*(1-fy)*s00 + fx*(1-fy)*s10 + (1-fx)*fy*s01 + fx*fy*s11;
                }
            }

            // Bilinear sample at displaced position B (backward)
            double bx = i - m_dispX[j][i];
            double by = j - m_dispY[j][i];
            double valB = ncdf_NOTDEF;
            int bxi = (int)bx, byi = (int)by;
            if (bxi >= 0 && bxi < ni-1 && byi >= 0 && byi < nj-1 &&
                m_sourceGrid[byi] && m_sourceGrid[byi+1]) {
                double fx = bx - bxi, fy = by - byi;
                double s00 = m_sourceGrid[byi][bxi], s10 = m_sourceGrid[byi][bxi+1];
                double s01 = m_sourceGrid[byi+1][bxi], s11 = m_sourceGrid[byi+1][bxi+1];
                if (s00 != ncdf_NOTDEF && s10 != ncdf_NOTDEF &&
                    s01 != ncdf_NOTDEF && s11 != ncdf_NOTDEF) {
                    valB = (1-fx)*(1-fy)*s00 + fx*(1-fy)*s10 + (1-fx)*fy*s01 + fx*fy*s11;
                }
            }

            // Blend
            if (valA == ncdf_NOTDEF && valB == ncdf_NOTDEF) {
                m_advectedGrid[j][i] = ncdf_NOTDEF;
            } else if (valA == ncdf_NOTDEF) {
                m_advectedGrid[j][i] = valB;
            } else if (valB == ncdf_NOTDEF) {
                m_advectedGrid[j][i] = valA;
            } else {
                m_advectedGrid[j][i] = w1 * valA + w2 * valB;
            }
        }
    }
}

//===================================================================
// Get advected grid for current frame
//===================================================================
double** ncdfAnimate::GetAdvectedGrid() {
    // Determine source grid from current display
    double** src = NULL;
    if (m_plugin->m_bShowCurrentForce && m_gui && m_gui->gridMag) src = m_gui->gridMag;
    else if (m_plugin->m_bShowSeaTemp && m_gui && m_gui->gridSST) src = m_gui->gridSST;
    else if (m_plugin->m_bShowSalinity && m_gui && m_gui->gridSalinity) src = m_gui->gridSalinity;
    if (!src) return NULL;

    int ni = m_gui->myMessage.lonLength;
    int nj = m_gui->myMessage.latLength;

    // Allocate advected grid if needed
    if (!m_advectedGrid || m_advW != ni || m_advH != nj) {
        FreeGrid(m_advectedGrid, m_advH);
        if (!AllocateGrid(m_advectedGrid, nj, ni)) return NULL;
        m_advW = ni; m_advH = nj;
    }

    m_sourceGrid = src;
    ComputeAdvectedGrid();
    return m_advectedGrid;
}

void ncdfAnimate::AdvanceFrame() {
    m_frame = (m_frame + 1) % m_period;
}
