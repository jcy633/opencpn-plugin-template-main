// ncdf_feature.cpp — ocean feature detection engine (P1: fronts / convergence-divergence / shear)
//
// Architecture (plan docs/ocean-feature-detection-plan.md):
//   L1 folded separable 5x5 kernels (binomial pre-smooth + Scharr folded together)
//   L2 derived scalars: divergence, strain, vorticity, NGM of density (T+S fused)
//   L3 feature objects: threshold masks -> connected components -> boundary/path tracing
//
// Disciplines: compute only on data-update events; buffers reused (C2/C9);
// no GL calls here (render thread only, C5); transient derived fields
// row-block allocated.
//
// v1.1 fixes (user P1 test feedback):
//   - long wrapping polylines: lons unrolled along each object (359->1 seam no
//     longer draws a line across the whole map)
//   - front scribbles: turn constraint + blob-interior rejection + pure-loop
//     phase (thick mask no longer snakes into giant encircling lines)
//   - overlapping zones: shear mask excludes divergence-dominated pixels
//   - UI jank: NaN-injected kernel validity (removed per-pixel validity loops),
//     partial-wave recompute (computeAll waves flag), region matching now
//     accumulates across lon shifts (global -180/180 no longer half-windowed)

#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>
#include "ncdf.h"
#include "ncdf_feature.h"

extern void ncdfLog(const char* format, ...);

// ---------------------------------------------------------------------------
// Folded kernels: binomial pre-smooth [1,2,1]/4 convolved with
//   derivative [-1,0,1]  -> gD5 = [-1,-2,0,2,1]/4   (ramp response +2)
//   smoother  [3,10,3]/16 -> gC5 = [3,16,26,16,3]/64 (sum 1, Scharr column)
// d/dq smoothed = gC5(p) x gD5(q), d/dp smoothed = gD5(p) x gGC5(q).
// Ramp normalization: divide by 2.
static const double kGD5[5] = {-1.0/4, -2.0/4, 0.0, 2.0/4, 1.0/4};
static const double kGC5[5] = {3.0/64, 16.0/64, 26.0/64, 16.0/64, 3.0/64};
static const double kNorm   = 0.5;

static const double kAlpha = 1.7e-4;   // thermal expansion  [1/K]
static const double kBeta  = 7.6e-4;   // haline contraction [1/PSU]
static const double kRho0  = 1027.0;
static const double kKmPerDeg = 111.32;

// Sentinel -> NaN: kernel sums become NaN near invalid data automatically,
// replacing the old per-pixel validity double-loops (major constant-factor win).
static inline double dv(double v) {
    return (v == ncdf_NOTDEF || !std::isfinite(v)) ? (double)NAN : v;
}
static inline float dvf(float v) {
    return (v == (float)ncdf_NOTDEF || !std::isfinite(v)) ? NAN : v;
}
// sanitize overloads used by the shared kernel templates
static inline double san(double v) { return dv(v); }
static inline double san(float v)  { return std::isfinite(v) ? (double)v : (double)NAN; }

// ---- hoisted folded kernels (shared by compute passes, self-test, spot probe)
// kernDerivQ: d/dq (column/x) of the binomial-smoothed field, per index, ramp
// response exactly 1 (exact for polynomials up to degree 2).
// kernDerivP: d/dp (row/y).
template <typename T>
static double kernDerivQ(const T *F, size_t stride, int gi, int gj)
{
    double acc[5];
    for (int p = -2; p <= 2; p++) {
        const T *r = F + (size_t)(gj + p) * stride;
        double s = 0;
        for (int q = -2; q <= 2; q++) s += kGD5[q + 2] * san(r[gi + q]);
        acc[p + 2] = s;
    }
    double s = 0;
    for (int p = 0; p < 5; p++) s += kGC5[p] * acc[p];
    return s * kNorm;
}
template <typename T>
static double kernDerivP(const T *F, size_t stride, int gi, int gj)
{
    double acc[5];
    for (int q = -2; q <= 2; q++) {
        double s = 0;
        for (int p = -2; p <= 2; p++) s += kGD5[p + 2] * san(F[(size_t)(gj + p) * stride + (gi + q)]);
        acc[q + 2] = s;
    }
    double s = 0;
    for (int q = 0; q < 5; q++) s += kGC5[q] * acc[q];
    return s * kNorm;
}

// Row-block overloads: FeatRowGrid rows are individually allocated, so
// cross-row access MUST go through the row pointer table (crash fix: passing a
// single-row pointer to the flat-grid kernels read out of bounds).
static double kernDerivQ(float *const *rows, int gi, int gj)
{
    double acc[5];
    for (int p = -2; p <= 2; p++) {
        const float *r = rows[gj + p];
        double s = 0;
        for (int q = -2; q <= 2; q++) s += kGD5[q + 2] * san(r[gi + q]);
        acc[p + 2] = s;
    }
    double s = 0;
    for (int p = 0; p < 5; p++) s += kGC5[p] * acc[p];
    return s * kNorm;
}
static double kernDerivP(float *const *rows, int gi, int gj)
{
    double acc[5];
    for (int q = -2; q <= 2; q++) {
        double s = 0;
        for (int p = -2; p <= 2; p++) s += kGD5[p + 2] * san(rows[gj + p][gi + q]);
        acc[q + 2] = s;
    }
    double s = 0;
    for (int q = 0; q < 5; q++) s += kGC5[q] * acc[q];
    return s * kNorm;
}

// ---------------------------------------------------------------------------
// Analytic kernel self-test (verification deliverable): synthetic fields with
// known derivatives -> validates the actual C++ kernels incl. signs/constants.
// Logs PASS/FAIL to the debug log; runs once per session.
static void selfTestKernels()
{
    static bool done = false;
    if (done) return;
    done = true;
    const int N = 11, C = 5;
    std::vector<double> f((size_t)(N * N));
    double maxErr = 0;
    // Test A: f = 3q - 2p + 7 (linear) -> dQ=3, dP=-2 exactly
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            f[(size_t)(j * N + i)] = 3.0 * i - 2.0 * j + 7.0;
    {
        double dQ = kernDerivQ(&f[0], N, C, C), dP = kernDerivP(&f[0], N, C, C);
        maxErr = wxMax(maxErr, std::fabs(dQ - 3.0));
        maxErr = wxMax(maxErr, std::fabs(dP + 2.0));
        ncdfLog("[selftest] linear field: dQ=%.8f (exp 3) dP=%.8f (exp -2)\n", dQ, dP);
    }
    // Test B: f = q^2 -> dQ = 2q exactly at center; f = p^2 -> dP = 2p
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            f[(size_t)(j * N + i)] = (double)i * i;
    {
        double dQ = kernDerivQ(&f[0], N, C, C);
        maxErr = wxMax(maxErr, std::fabs(dQ - 2.0 * C));
        ncdfLog("[selftest] quadratic field: dQ=%.8f (exp %d)\n", dQ, 2 * C);
    }
    // Test C: solid-body rotation u = -(p-p0), v = (q-q0) at unit rate
    //   -> div = 0, vorticity = 2 (index units), strain = 0
    std::vector<double> u((size_t)(N * N)), v((size_t)(N * N));
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            u[(size_t)(j * N + i)] = -(double)(j - C);
            v[(size_t)(j * N + i)] = (double)(i - C);
        }
    {
        double dudq = kernDerivQ(&u[0], N, C, C), dudp = kernDerivP(&u[0], N, C, C);
        double dvdq = kernDerivQ(&v[0], N, C, C), dvdp = kernDerivP(&v[0], N, C, C);
        double div = dudq + dvdp;
        double vort = dvdq - dudp;
        double strain = dvdq + dudp;
        maxErr = wxMax(maxErr, std::fabs(div));
        maxErr = wxMax(maxErr, std::fabs(vort - 2.0));
        maxErr = wxMax(maxErr, std::fabs(strain));
        ncdfLog("[selftest] solid rotation: div=%.8f (exp 0) vort=%.8f (exp 2) strain=%.8f (exp 0)\n",
            div, vort, strain);
    }
    // Test D: NaN propagation (invalid cell poisons the whole 5x5 window)
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
            f[(size_t)(j * N + i)] = 3.0 * i - 2.0 * j + 7.0;
    f[(size_t)(C * N + C)] = NAN;
    {
        double dQ = kernDerivQ(&f[0], N, C, C);
        ncdfLog("[selftest] NaN propagation: dQ=%f (exp nan) ok=%d\n", dQ, (int)std::isnan(dQ));
        if (!std::isnan(dQ)) maxErr = 1.0;
    }
    // Test E: row-block kernel overloads (the crash-fix path)
    {
        std::vector<float*> rows((size_t)N);
        for (int j = 0; j < N; j++) {
            rows[(size_t)j] = new float[(size_t)N];
            for (int i = 0; i < N; i++) rows[(size_t)j][i] = (float)(3.0 * i - 2.0 * j + 7.0);
        }
        double dQ = kernDerivQ(&rows[0], C, C), dP = kernDerivP(&rows[0], C, C);
        maxErr = wxMax(maxErr, std::fabs(dQ - 3.0));
        maxErr = wxMax(maxErr, std::fabs(dP + 2.0));
        ncdfLog("[selftest] row-block kernels: dQ=%.8f (exp 3) dP=%.8f (exp -2)\n", dQ, dP);
        for (int j = 0; j < N; j++) delete[] rows[(size_t)j];
    }
    ncdfLog("[selftest] Kernel self-test %s (maxErr=%.3e)\n", (maxErr < 1e-5) ? "PASS" : "FAIL", maxErr);
}

// ---------------------------------------------------------------------------
// Real-data spot probe (verification deliverable): dumps raw + derived values
// at fixed geographic points covering known currents, for cross-checking
// against an independent reader (python/h5py). Runs once per file.
void FeatureEngine::spotReport(MainDialog *gui)
{
    if (m_spotDone || !gui) return;
    const ncdfDataMessage &msg = gui->myMessage;
    if (!msg.lonValues || !msg.latValues || msg.lonLength < 16 || msg.latLength < 16) return;
    m_spotDone = true;

    static const double pts[][2] = {   // {lat, lon}
        { 28.0, 130.0},   // Kuroshio axis (south of Kyushu)
        { 30.5, 128.0},   // Kuroshio landward front
        { 35.0, 145.0},   // Kuroshio extension
        { 36.0, 290.0},   // Gulf Stream (lon 290E = 70W)
        {-35.0,  25.0},   // Agulhas
        {-55.0,  60.0},   // ACC / Southern Ocean
        {  0.0, 160.0},   // Equatorial current
        { 15.0, 115.0},   // South China Sea
    };
    const size_t lonLen = msg.lonLength;
    const double *U = msg.ucurr.data(), *V = msg.vcurr.data();
    const double *T = msg.hasSSTData() ? msg.sst.data() : NULL;
    const double *S = msg.hasSalData() ? msg.salinity.data() : NULL;
    const double *Z = msg.hasZosData() ? msg.zos.data() : NULL;

    ncdfLog("[spot] ==== real-data spot probe (file grid %zux%zu, lat[0]=%.3f lat[-1]=%.3f lon[0]=%.3f lon[-1]=%.3f) ====\n",
        msg.latLength, msg.lonLength,
        msg.latValues[0], msg.latValues[msg.latLength - 1],
        msg.lonValues[0], msg.lonValues[msg.lonLength - 1]);

    for (size_t k = 0; k < sizeof(pts) / sizeof(pts[0]); k++) {
        double lat = pts[k][0], lon = pts[k][1];
        // nearest grid index: two-pass 1D nearest (O(N) total)
        int bi = -1, bj = -1;
        {
            double bErr = 1e30;
            for (size_t j = 0; j < msg.latLength; j++) {
                double e = std::fabs(msg.latValues[j] - lat);
                if (e < bErr) { bErr = e; bj = (int)j; }
            }
            bErr = 1e30;
            for (size_t i = 0; i < msg.lonLength; i++) {
                double e = std::fabs(std::fmod(std::fabs(msg.lonValues[i] - lon) + 180.0, 360.0) - 180.0);
                if (e < bErr) { bErr = e; bi = (int)i; }
            }
        }
        if (bi < 2 || bj < 2 || bi + 2 >= (int)lonLen || bj + 2 >= (int)msg.latLength) {
            ncdfLog("[spot] (%.1f,%.1f): OUT OF GRID\n", lat, lon);
            continue;
        }
        size_t idx = (size_t)bj * lonLen + bi;
        double u = U ? U[idx] : 0, v = V ? V[idx] : 0;
        double t = T ? T[idx] : 0, s = S ? S[idx] : 0, z = Z ? Z[idx] : 0;
        u = dv(u); v = dv(v); t = T ? dv(t) : NAN; s = S ? dv(s) : NAN; z = Z ? dv(z) : NAN;
        double spd = std::sqrt(u * u + v * v);

        // per-index derivatives via the same kernels
        double dudq = U ? kernDerivQ(U, lonLen, bi, bj) : NAN;
        double dudp = U ? kernDerivP(U, lonLen, bi, bj) : NAN;
        double dvdq = V ? kernDerivQ(V, lonLen, bi, bj) : NAN;
        double dvdp = V ? kernDerivP(V, lonLen, bi, bj) : NAN;
        double latDeg = msg.latValues[bj];
        double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latDeg * M_PI / 180.0);
        double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
        double sgn = (msg.latValues[1] < msg.latValues[0]) ? -1.0 : 1.0;
        double dudx = dudq / (cellKmQ * 1000.0), dvdx = dvdq / (cellKmQ * 1000.0);
        double dudy = sgn * dudp / (cellKmP * 1000.0), dvdy = sgn * dvdp / (cellKmP * 1000.0);
        double divv = dudx + dvdy;
        double vort = dvdx - dudy;
        double sn = dudx - dvdy;
        double ss = dvdx + dudy;
        double det = dudx * dvdy - dudy * dvdx;
        double tr = divv;
        double ow = sn * sn + ss * ss - vort * vort;
        double disc = det - 0.25 * tr * tr;
        double liutex = (disc > 0) ? std::sqrt(disc) : 0.0;

        // density gradient (per km) at point
        double gradRho = NAN;
        if (T || S) {
            double drdq = 0, drdp = 0;
            if (T) {
                drdq += kRho0 * (-kAlpha) * kernDerivQ(T, lonLen, bi, bj);
                drdp += kRho0 * (-kAlpha) * kernDerivP(T, lonLen, bi, bj);
            }
            if (S) {
                drdq += kRho0 * (kBeta) * kernDerivQ(S, lonLen, bi, bj);
                drdp += kRho0 * (kBeta) * kernDerivP(S, lonLen, bi, bj);
            }
            gradRho = std::sqrt(drdq * drdq / (cellKmQ * cellKmQ) + drdp * drdp / (cellKmP * cellKmP));
        }

        ncdfLog("[spot] (%.1f,%.1f) grid=(%d,%d) u=%.5f v=%.5f spd=%.5f T=%.3f S=%.3f zos=%.4f | gradRho=%.3e kg/m3/km zeta=%.3e div=%.3e sn=%.3e ss=%.3e OW=%.3e liutex=%.3e\n",
            lat, lon, bi, bj, u, v, spd, t, s, z, gradRho, vort, divv, sn, ss, ow, liutex);
    }
    ncdfLog("[spot] ==== end spot probe ====\n");
}

// Unroll longitude run: keeps each object's lons continuous across the
// 0/360 seam (projection is periodic, so rendering is unaffected elsewhere).
static void unrollLons(std::vector<double> &pts) {
    for (size_t k = 2; k < pts.size(); k += 2) {
        double d = pts[k] - pts[k - 2];
        if (d > 180.0) pts[k] -= 360.0;
        else if (d < -180.0) pts[k] += 360.0;
    }
}

FeatureEngine::FeatureEngine()
    : m_latMin(15), m_latMax(45), m_lonMin(100), m_lonMax(135),
      m_sensPct(85), m_visitStamp(0)
{
    for (int i = 0; i < FK_COUNT; i++) m_enabled[i] = false;
}

FeatureEngine::~FeatureEngine() { resetFile(); }

void FeatureEngine::setRegion(double latMin, double latMax, double lonMin, double lonMax)
{
    m_latMin = latMin; m_latMax = latMax;
    m_lonMin = lonMin; m_lonMax = lonMax;
}

void FeatureEngine::setSensitivity(int pct)
{
    if (pct < 50) pct = 50;
    if (pct > 99) pct = 99;
    m_sensPct = pct;
}

void FeatureEngine::setEnabled(int kind, bool on)
{
    if (kind >= 0 && kind < FK_COUNT) m_enabled[kind] = on;
}

bool FeatureEngine::needUV() const
{
    return m_enabled[FK_CONVERG] || m_enabled[FK_DIVERG] || m_enabled[FK_SHEAR]
        || m_enabled[FK_EDDY] || m_enabled[FK_JET];   // rotation confirm / jet axes
}

bool FeatureEngine::needTS() const
{
    return m_enabled[FK_FRONT] || m_enabled[FK_EDDY];   // eddy core typing
}

bool FeatureEngine::needZos() const
{
    return m_enabled[FK_EDDY];   // closed-contour detection
}

bool FeatureEngine::anyEnabled() const
{
    for (int i = 0; i < FK_COUNT; i++) if (m_enabled[i]) return true;
    return false;
}

void FeatureEngine::resetFile()
{
    m_set.clearKeepCapacity();
    m_jetCache.clear();
    m_jetArena.clear();
    m_div.release(); m_strain.release(); m_vort.release();
    m_ngm.release(); m_rho.release();
    m_bimodal.release(); m_mask.release(); m_visited.release();
}

// ---------------------------------------------------------------------------
// Percentile via 256-bin histogram (O(N), avoids sorting).
float FeatureEngine::percentileOf(const FeatRowGrid &g, int i0, int i1, int j0, int j1, float pct) const
{
    const int BINS = 256;
    double lo = 1e30, hi = -1e30;
    for (int j = j0; j < j1; j++) {
        const float *r = g[j];
        for (int i = i0; i < i1; i++) {
            float v = r[i];
            if (!std::isfinite(v)) continue;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    if (lo > hi) return 0.0f;
    if (hi <= lo) return (float)lo;
    std::vector<int> hist((size_t)BINS, 0);
    long long total = 0;
    double scale = (BINS - 1) / (hi - lo);
    for (int j = j0; j < j1; j++) {
        const float *r = g[j];
        for (int i = i0; i < i1; i++) {
            float v = r[i];
            if (!std::isfinite(v)) continue;
            int b = (int)((v - lo) * scale);
            if (b < 0) b = 0;
            if (b >= BINS) b = BINS - 1;
            hist[(size_t)b]++;
            total++;
        }
    }
    if (total == 0) return 0.0f;
    long long want = (long long)(total * (pct / 100.0));
    long long acc = 0;
    for (int b = 0; b < BINS; b++) {
        acc += hist[(size_t)b];
        if (acc > want) return (float)(lo + (hi - lo) * b / (BINS - 1));
    }
    return (float)hi;
}

// ---------------------------------------------------------------------------
// Iterative Douglas-Peucker (explicit stack, no recursion) on interleaved xy.
void FeatureEngine::douglasPeucker(std::vector<double> &xy, double tol)
{
    size_t n = xy.size() / 2;
    if (n < 3) return;
    std::vector<unsigned char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<size_t> stack;
    stack.reserve(64);
    stack.push_back(0);
    stack.push_back(n - 1);
    while (!stack.empty()) {
        size_t b = stack.back(); stack.pop_back();
        size_t a = stack.back(); stack.pop_back();
        if (b <= a + 1) continue;
        double ax = xy[2 * a], ay = xy[2 * a + 1];
        double bx = xy[2 * b], by = xy[2 * b + 1];
        double dx = bx - ax, dy = by - ay;
        double len2 = dx * dx + dy * dy;
        double best = -1; size_t bestIdx = a;
        for (size_t k = a + 1; k < b; k++) {
            double px = xy[2 * k] - ax, py = xy[2 * k + 1] - ay;
            double d;
            if (len2 <= 1e-30) {
                d = px * px + py * py;
            } else {
                double t = (px * dx + py * dy) / len2;
                if (t < 0) t = 0; else if (t > 1) t = 1;
                double ex = px - t * dx, ey = py - t * dy;
                d = ex * ex + ey * ey;
            }
            if (d > best) { best = d; bestIdx = k; }
        }
        if (best > tol * tol) {
            keep[bestIdx] = 1;
            stack.push_back(a); stack.push_back(bestIdx);
            stack.push_back(bestIdx); stack.push_back(b);
        }
    }
    size_t w = 0;
    for (size_t k = 0; k < n; k++) if (keep[k]) {
        xy[2 * w] = xy[2 * k];
        xy[2 * w + 1] = xy[2 * k + 1];
        w++;
    }
    xy.resize(2 * w);
}

// ---------------------------------------------------------------------------
// L1+L2 wave 1: velocity-gradient derived scalars (divergence, strain, vorticity).
// Output units: per second (SI). Invalid windows -> NaN via dv() injection.
void FeatureEngine::computeFlowDeriv(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    const size_t lonLen = msg.lonLength;
    const double *U = msg.ucurr.data();
    const double *V = msg.vcurr.data();

    int w = i1 - i0, h = j1 - j0;
    if (w < 8 || h < 8) return;
    m_div.allocate(w, h);
    m_strain.allocate(w, h);
    m_vort.allocate(w, h);
    m_mask.allocate(w, h);        // scratch (extraction phase)
    m_visited.allocate(w, h);

    auto derivQ = [&](const double *F, int gi, int gj) -> float {
        return (float)kernDerivQ(F, lonLen, gi, gj);
    };
    auto derivP = [&](const double *F, int gi, int gj) -> float {
        return (float)kernDerivP(F, lonLen, gi, gj);
    };

    const float sign = (msg.latValues && msg.latLength > 1 && msg.latValues[1] < msg.latValues[0]) ? -1.0f : 1.0f;
    for (int j = 0; j < h; j++) {
        float *dr = m_div[j], *sr = m_strain[j], *vr = m_vort[j];
        const int gj = j0 + j;
        const bool rowOk = (gj >= 2 && gj + 2 < (int)msg.latLength);
        double latDeg = msg.latValues ? msg.latValues[gj] : 0.0;
        double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latDeg * M_PI / 180.0);
        double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
        if (cellKmQ < 1e-6) cellKmQ = 1e-6;
        if (cellKmP < 1e-6) cellKmP = 1e-6;
        for (int i = 0; i < w; i++) {
            const int gi = i0 + i;
            if (!rowOk || gi < 2 || gi + 2 >= (int)lonLen) {
                dr[i] = sr[i] = vr[i] = NAN;
                continue;
            }
            float dudq = derivQ(U, gi, gj) / (float)cellKmQ * 0.001f;  // per meter
            float dudp = derivP(U, gi, gj) / (float)cellKmP * 0.001f;
            float dvdq = derivQ(V, gi, gj) / (float)cellKmQ * 0.001f;
            float dvdp = derivP(V, gi, gj) / (float)cellKmP * 0.001f;
            float dvdx = dvdq;
            float dudy = sign * dudp;
            float dvdy = sign * dvdp;
            float dudx = dudq;
            dr[i] = dudx + dvdy;                    // divergence  [1/s]
            sr[i] = dvdx + dudy;                    // shear strain[1/s]
            vr[i] = dvdx - dudy;                    // vorticity   [1/s]
        }
    }
}

// ---------------------------------------------------------------------------
// L1+L2 wave 2: density (T+S fused) and its normalized gradient magnitude.
void FeatureEngine::computeRhoNGM(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    const size_t lonLen = msg.lonLength;
    const bool hasT = msg.hasSSTData();
    const bool hasS = msg.hasSalData();
    const double *T = hasT ? msg.sst.data() : NULL;
    const double *S = hasS ? msg.salinity.data() : NULL;
    if (!T && !S) return;

    int w = i1 - i0, h = j1 - j0;
    if (w < 8 || h < 8) return;
    m_rho.allocate(w, h);
    m_ngm.allocate(w, h);
    m_mask.allocate(w, h);
    m_visited.allocate(w, h);

    // density proxy rho' = rho0 * (-alpha*T + beta*S); invalid -> NaN via dv()
    for (int j = 0; j < h; j++) {
        float *rr = m_rho[j];
        const int gj = j0 + j;
        for (int i = 0; i < w; i++) {
            const int gi = i0 + i;
            double rho = 0.0;
            if (T) rho += kRho0 * (-kAlpha * dv(T[(size_t)gj * lonLen + gi]));
            if (S) rho += kRho0 * ( kBeta  * dv(S[(size_t)gj * lonLen + gi]));
            rr[i] = (float)rho;
        }
    }

    // Row-block kernels (crash fix): m_rho rows are individually allocated,
    // cross-row access must go through the row pointer table, and the 5x5
    // window must stay inside the local grid -> interior [2..w-2)x[2..h-2),
    // borders stay NaN.
    for (int j = 0; j < h; j++) {
        float *gr = m_ngm[j];
        const int gj = j0 + j;
        double latDeg = msg.latValues ? msg.latValues[gj] : 0.0;
        double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latDeg * M_PI / 180.0);
        double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
        if (cellKmQ < 1e-6) cellKmQ = 1e-6;
        if (cellKmP < 1e-6) cellKmP = 1e-6;
        for (int i = 0; i < w; i++) gr[i] = NAN;
        if (j < 2 || j + 2 >= h) continue;
        for (int i = 2; i < w - 2; i++) {
            double dq = kernDerivQ(&m_rho.rows[0], i, j) / cellKmQ;
            double dp = kernDerivP(&m_rho.rows[0], i, j) / cellKmP;
            gr[i] = (float)std::sqrt(dq * dq + dp * dp);   // |grad rho| per km
        }
    }

    // normalize by P50 -> dimensionless NGM (Belkin-O'Reilly style)
    float p50 = percentileOf(m_ngm, 2, w - 2, 2, h - 2, 50.0f);
    double norm = (p50 > 1e-9f) ? p50 : 1.0;
    for (int j = 2; j < h - 2; j++) {
        float *gr = m_ngm[j];
        for (int i = 2; i < w - 2; i++) {
            if (std::isfinite(gr[i])) gr[i] = (float)(gr[i] / norm);
        }
    }
}

// ---------------------------------------------------------------------------
// L3: threshold-zone extraction (convergence / divergence / shear).
void FeatureEngine::extractZones(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    int w = i1 - i0, h = j1 - j0;
    if (w < 8 || h < 8) return;

    struct ZoneJob { const FeatRowGrid *field; bool splitSign; bool excludeDivPixels; };
    std::vector<ZoneJob> jobs;
    const bool wantConv = m_enabled[FK_CONVERG], wantDiv = m_enabled[FK_DIVERG];
    const bool wantShear = m_enabled[FK_SHEAR];
    if (wantConv || wantDiv)
        jobs.push_back({&m_div, true, false});
    if (wantShear)
        jobs.push_back({&m_strain, false, true});   // exclusivity: no overlap with div zones

    if (jobs.empty() || !m_div.rows.size()) return;

    if (!m_visited.rows.size()) m_visited.allocate(w, h);
    if (!m_mask.rows.size()) m_mask.allocate(w, h);
    m_visitStamp = 0;
    const double latMid = msg.latValues ? 0.5 * (msg.latValues[j0] + msg.latValues[j1 - 1]) : 0.0;
    const double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latMid * M_PI / 180.0);
    const double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
    const double cellArea = wxMax(0.01, cellKmQ * cellKmP);

    // thresholds on |value| (abs basis kept in m_mask during measurement)
    float thrDiv = 0, p99Div = 0, thrStrain = 0, p99Strain = 0;
    {
        for (int j = 0; j < h; j++) {
            float *mr = m_mask[j];
            const float *fr = m_div[j];
            for (int i = 0; i < w; i++) mr[i] = std::isfinite(fr[i]) ? (float)std::fabs(fr[i]) : NAN;
        }
        thrDiv = percentileOf(m_mask, 0, w, 0, h, (float)m_sensPct);
        p99Div = percentileOf(m_mask, 0, w, 0, h, 99.0f);
    }
    if (wantShear) {
        for (int j = 0; j < h; j++) {
            float *mr = m_mask[j];
            const float *fr = m_strain[j];
            for (int i = 0; i < w; i++) mr[i] = std::isfinite(fr[i]) ? (float)std::fabs(fr[i]) : NAN;
        }
        thrStrain = percentileOf(m_mask, 0, w, 0, h, (float)m_sensPct);
        p99Strain = percentileOf(m_mask, 0, w, 0, h, 99.0f);
    }

    for (size_t job = 0; job < jobs.size(); job++) {
        const FeatRowGrid &field = *jobs[job].field;
        const bool isDivJob = jobs[job].splitSign;
        float thr = isDivJob ? thrDiv : thrStrain;
        float p99 = isDivJob ? p99Div : p99Strain;
        if (!(thr > 0)) continue;

        // binary mask; shear job excludes divergence-dominated pixels (plan: zones disjoint)
        for (int j = 0; j < h; j++) {
            float *mr = m_mask[j];
            const float *fr = field[j];
            for (int i = 0; i < w; i++) {
                float v = fr[i];
                bool on = std::isfinite(v) && std::fabs(v) > thr;
                if (on && jobs[job].excludeDivPixels && thrDiv > 0) {
                    float dvv = m_div[j][i];
                    if (std::isfinite(dvv) && std::fabs(dvv) > thrDiv) on = false;
                }
                mr[i] = on ? 1.0f : 0.0f;
            }
        }

        // connected components
        m_visitStamp++;
        if (m_visitStamp > 1000000) {
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++) m_visited[j][i] = 0;
            m_visitStamp = 1;
        }
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                if (m_mask[j][i] < 0.5f) continue;
                if (m_visited[j][i] == (float)m_visitStamp) continue;
                // flood fill
                m_stack.clear();
                m_compPixels.clear();
                m_stack.push_back(j * w + i);
                m_visited[j][i] = (float)m_visitStamp;
                double sumVal = 0, peak = 0;
                while (!m_stack.empty()) {
                    int cur = m_stack.back(); m_stack.pop_back();
                    int cj = cur / w, ci = cur % w;
                    m_compPixels.push_back(cur);
                    float v = field[cj][ci];
                    sumVal += v;
                    if (std::fabs(v) > std::fabs(peak)) peak = v;
                    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
                    static const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                    for (int d = 0; d < 8; d++) {
                        int ni = ci + DX[d], nj = cj + DY[d];
                        if (ni < 0 || nj < 0 || ni >= w || nj >= h) continue;
                        if (m_mask[nj][ni] < 0.5f) continue;
                        if (m_visited[nj][ni] == (float)m_visitStamp) continue;
                        m_visited[nj][ni] = (float)m_visitStamp;
                        m_stack.push_back(nj * w + ni);
                    }
                }
                int nPix = (int)m_compPixels.size();
                double area = nPix * cellArea;
                if (nPix < 8 || area < 25.0) continue;   // area filter (plan §2.4)

                double meanVal = sumVal / nPix;
                int kind;
                if (isDivJob) kind = (meanVal < 0) ? FK_CONVERG : FK_DIVERG;
                else kind = FK_SHEAR;
                if (!splitSignGate(kind)) continue;

                // ---- Moore boundary trace of this component ----
                int si = w, sj = h;
                for (size_t k = 0; k < m_compPixels.size(); k++) {
                    int pj = m_compPixels[k] / w, pi = m_compPixels[k] % w;
                    if (pj < sj || (pj == sj && pi < si)) { sj = pj; si = pi; }
                }
                auto isMember = [&](int ci, int cj) -> bool {
                    if (ci < 0 || cj < 0 || ci >= w || cj >= h) return false;
                    return m_visited[cj][ci] == (float)m_visitStamp;
                };
                m_tmpXY.clear();
                static const int DX8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
                static const int DY8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                int ci = si, cj = sj;
                int backDir = 4;  // came from west (start is leftmost of top row)
                int steps = 0;
                const int maxSteps = 4 * nPix + 16;
                do {
                    m_tmpXY.push_back((float)ci);
                    m_tmpXY.push_back((float)cj);
                    int found = -1;
                    for (int k = 1; k <= 8; k++) {
                        int d = (backDir + k) % 8;
                        if (isMember(ci + DX8[d], cj + DY8[d])) { found = d; break; }
                    }
                    if (found < 0) break;
                    ci += DX8[found];
                    cj += DY8[found];
                    backDir = (found + 4 + 1) % 8;
                    steps++;
                } while (!(ci == si && cj == sj) && steps < maxSteps);

                if (m_tmpXY.size() < 8) continue;  // < 4 points

                // pixel coords -> lon/lat (unrolled across the date line)
                std::vector<double> pts;
                pts.reserve(m_tmpXY.size());
                float lonMin = 1e30f, latMin = 1e30f, lonMax = -1e30f, latMax = -1e30f;
                for (size_t k = 0; k < m_tmpXY.size(); k += 2) {
                    int pi = i0 + (int)m_tmpXY[k];
                    int pj = j0 + (int)m_tmpXY[k + 1];
                    double lo = msg.lonValues ? msg.lonValues[pi] : pi;
                    double la = msg.latValues ? msg.latValues[pj] : pj;
                    pts.push_back(lo); pts.push_back(la);
                }
                unrollLons(pts);
                for (size_t k = 0; k < pts.size(); k += 2) {
                    if (pts[k] < lonMin) lonMin = (float)pts[k];
                    if (pts[k] > lonMax) lonMax = (float)pts[k];
                    if (pts[k + 1] < latMin) latMin = (float)pts[k + 1];
                    if (pts[k + 1] > latMax) latMax = (float)pts[k + 1];
                }
                double tol = std::fabs(msg.iDirectionIncr) + std::fabs(msg.jDirectionIncr);
                douglasPeucker(pts, tol);
                if (pts.size() < 6) continue;   // need >= 3 vertices

                ZoneFeature zf;
                zf.kind = kind;
                zf.areaKm2 = (float)area;
                zf.meanVal = (float)(meanVal * 1e6);    // micro per second
                zf.peakVal = (float)(peak * 1e6);
                zf.strength = (p99 > thr) ? (float)wxMin(1.0, (std::fabs(meanVal) - thr) / (p99 - thr + 1e-30)) : 1.0f;
                if (zf.strength < 0) zf.strength = 0;
                zf.bbox[0] = lonMin; zf.bbox[1] = latMin;
                zf.bbox[2] = lonMax; zf.bbox[3] = latMax;
                zf.vOff = (int)m_set.arena.size();
                zf.vCnt = (int)(pts.size() / 2);
                for (size_t k = 0; k < pts.size(); k += 2) {
                    m_set.arena.push_back((float)pts[k]);
                    m_set.arena.push_back((float)pts[k + 1]);
                }
                m_set.zones.push_back(zf);
                m_set.detected[kind]++;
                ncdfLog("[feat] zone kind=%d nPix=%d area=%.0fkm2 mean=%.2f peak=%.2f (1e-6/s) verts=%d\n",
                    kind, nPix, area, zf.meanVal, zf.peakVal, zf.vCnt);
            }
        }
    }
}

bool FeatureEngine::splitSignGate(int kind) const
{
    if (kind == FK_CONVERG) return m_enabled[FK_CONVERG];
    if (kind == FK_DIVERG)  return m_enabled[FK_DIVERG];
    return m_enabled[FK_SHEAR];
}

// ---------------------------------------------------------------------------
// L3: front extraction (NGM threshold + Cayula-style bimodal-window confirm).
void FeatureEngine::extractFronts(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    const size_t lonLen = msg.lonLength;
    int w = i1 - i0, h = j1 - j0;
    if (w < 10 || h < 10 || !m_ngm.rows.size()) return;

    const bool hasT = msg.hasSSTData();
    const bool hasS = msg.hasSalData();
    int cls = (hasT && hasS) ? 0 : (hasT ? 1 : 2);

    // ---- bimodal window verdicts (25x25 tiles, Otsu between-class ratio) ----
    if (!m_bimodal.rows.size()) m_bimodal.allocate(w, h);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) m_bimodal[j][i] = 0.0f;

    const int TILE = 25, HBINS = 20;
    for (int tj = 2; tj < h - 2; tj += TILE) {
        for (int ti = 2; ti < w - 2; ti += TILE) {
            int imin = ti, imax = wxMin(ti + TILE, w - 2);
            int jmin = tj, jmax = wxMin(tj + TILE, h - 2);
            double lo = 1e30, hi = -1e30;
            int nValid = 0;
            for (int j = jmin; j < jmax; j++) {
                const float *rr = m_rho[j];
                for (int i = imin; i < imax; i++) {
                    float v = rr[i];
                    if (!std::isfinite(v)) continue;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                    nValid++;
                }
            }
            if (nValid < 50 || hi <= lo) continue;
            std::vector<double> hist((size_t)HBINS, 0.0);
            for (int j = jmin; j < jmax; j++) {
                const float *rr = m_rho[j];
                for (int i = imin; i < imax; i++) {
                    float v = rr[i];
                    if (!std::isfinite(v)) continue;
                    int b = (int)((v - lo) / (hi - lo) * (HBINS - 1));
                    if (b < 0) b = 0; if (b >= HBINS) b = HBINS - 1;
                    hist[(size_t)b] += 1.0;
                }
            }
            // Otsu between-class variance ratio
            double total = 0, sumAll = 0;
            for (int b = 0; b < HBINS; b++) { total += hist[(size_t)b]; sumAll += hist[(size_t)b] * b; }
            if (total < 50) continue;
            double sumB = 0, wB = 0, maxBetween = 0, varAll = 0;
            double meanAll = sumAll / total;
            for (int b = 0; b < HBINS; b++) varAll += hist[(size_t)b] * (b - meanAll) * (b - meanAll);
            for (int b = 0; b < HBINS; b++) {
                wB += hist[(size_t)b];
                if (wB == 0) continue;
                double wF = total - wB;
                if (wF == 0) break;
                sumB += hist[(size_t)b] * b;
                double mB = sumB / wB, mF = (sumAll - sumB) / wF;
                double between = wB * wF * (mB - mF) * (mB - mF);
                if (between > maxBetween) maxBetween = between;
            }
            double ratio = (varAll > 1e-9) ? maxBetween / varAll : 0.0;
            if (ratio > 0.85) {
                for (int j = jmin; j < jmax; j++)
                    for (int i = imin; i < imax; i++)
                        if (std::isfinite(m_rho[j][i])) m_bimodal[j][i] = 1.0f;
            }
        }
    }

    // ---- front mask: NGM > thr && bimodal ----
    float thr = percentileOf(m_ngm, 2, w - 2, 2, h - 2, (float)m_sensPct);
    if (!(thr > 0)) return;
    float p99 = percentileOf(m_ngm, 2, w - 2, 2, h - 2, 99.0f);
    if (!m_mask.rows.size()) m_mask.allocate(w, h);
    if (!m_visited.rows.size()) m_visited.allocate(w, h);
    for (int j = 0; j < h; j++) {
        float *mr = m_mask[j];
        for (int i = 0; i < w; i++) {
            float g = m_ngm[j][i];
            mr[i] = (std::isfinite(g) && g > thr && m_bimodal[j][i] > 0.5f) ? 1.0f : 0.0f;
        }
    }

    // ---- 8-connected polyline walk (scribble-proof: turn constraint + blob rejection) ----
    m_visitStamp++;
    if (m_visitStamp > 1000000) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) m_visited[j][i] = 0;
        m_visitStamp = 1;
    }
    auto member = [&](int i, int j) -> bool {
        return i >= 0 && j >= 0 && i < w && j < h && m_mask[j][i] > 0.5f;
    };
    auto visited = [&](int i, int j) -> bool {
        return m_visited[j][i] == (float)m_visitStamp;
    };

    static const int DX8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int DY8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    auto nCount = [&](int i, int j) -> int {
        int c = 0;
        for (int d = 0; d < 8; d++) if (member(i + DX8[d], j + DY8[d])) c++;
        return c;
    };

    const int kMaxChain = 3000;   // hard cap (a ridge, not a snake)
    auto walkChain = [&](int si, int sj) {
        std::vector<int> chain;
        chain.reserve(256);
        chain.push_back(sj * w + si);
        m_visited[sj][si] = (float)m_visitStamp;
        int ci = si, cj = sj;
        int pdx = 0, pdy = 0;
        while ((int)chain.size() < kMaxChain) {
            int bestD = -1, bestScore = -1;
            for (int d = 0; d < 8; d++) {
                int ni = ci + DX8[d], nj = cj + DY8[d];
                if (!member(ni, nj) || visited(ni, nj)) continue;
                // turn constraint: no reversal / sharp fold-back (prevents scribbles)
                if (pdx != 0 || pdy != 0) {
                    int dot = DX8[d] * pdx + DY8[d] * pdy;
                    if (dot <= 0) continue;             // >90-degree turns rejected
                }
                // blob-interior rejection: don't dive into thick masses
                if (chain.size() > 2 && nCount(ni, nj) >= 6) continue;
                int score = (pdx == 0 && pdy == 0) ? 0 : (DX8[d] * pdx + DY8[d] * pdy);
                if (score > bestScore) { bestScore = score; bestD = d; }
            }
            if (bestD < 0) break;
            pdx = DX8[bestD]; pdy = DY8[bestD];
            ci += pdx; cj += pdy;
            m_visited[cj][ci] = (float)m_visitStamp;
            chain.push_back(cj * w + ci);
        }
        return chain;
    };

    const double latMid = msg.latValues ? 0.5 * (msg.latValues[j0] + msg.latValues[j1 - 1]) : 0.0;
    const double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latMid * M_PI / 180.0);
    const double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
    const double minLenKm = 30.0;

    auto emitChain = [&](std::vector<int> &chain) {
        int nPix = (int)chain.size();
        if (nPix < 4) return;
        // chain quality filter (v1.2): drop chains threading blob interiors /
        // thick mask boundaries (they read as coastal snakes, not fronts)
        {
            int sumDeg = 0, maxDeg = 0;
            for (int k = 0; k < nPix; k++) {
                int deg = nCount(chain[(size_t)k] % w, chain[(size_t)k] / w);
                sumDeg += deg;
                if (deg > maxDeg) maxDeg = deg;
            }
            if (maxDeg >= 7 || (double)sumDeg / nPix > 4.2) return;
        }
        std::vector<double> pts;
        pts.reserve((size_t)nPix * 2);
        for (int k = 0; k < nPix; k++) {
            int pj = chain[(size_t)k] / w, pi = chain[(size_t)k] % w;
            int gi = i0 + pi, gj = j0 + pj;
            double lo = msg.lonValues ? msg.lonValues[gi] : gi;
            double la = msg.latValues ? msg.latValues[gj] : gj;
            pts.push_back(lo); pts.push_back(la);
        }
        unrollLons(pts);   // date-line seam fix
        double lenKm = 0;
        for (int k = 1; k < nPix; k++) {
            int a = chain[(size_t)(k - 1)], b = chain[(size_t)k];
            double di = (b % w) - (a % w), dj = (b / w) - (a / w);
            lenKm += std::sqrt(di * di * cellKmQ * cellKmQ + dj * dj * cellKmP * cellKmP);
        }
        if (lenKm < minLenKm) return;

        double tol = std::fabs(msg.iDirectionIncr) + std::fabs(msg.jDirectionIncr);
        douglasPeucker(pts, tol);
        if (pts.size() < 4) return;

        // ---- cross-front samples at midpoint ----
        int kmid = nPix / 2;
        int a = chain[(size_t)wxMax(0, kmid - 2)], b = chain[(size_t)wxMin(nPix - 1, kmid + 2)];
        double ti = (double)((b % w) - (a % w)), tj = (double)((b / w) - (a / w));
        double tl = std::sqrt(ti * ti + tj * tj);
        if (tl < 1e-6) { ti = 1; tj = 0; tl = 1; }
        double niN = -tj / tl, njN = ti / tl;
        int off = 2;
        int ri = (int)wxMin(w - 1, wxMax(0, (chain[(size_t)kmid] % w) + (int)std::lround(niN * off)));
        int rj = (int)wxMin(h - 1, wxMax(0, (chain[(size_t)kmid] / w) + (int)std::lround(njN * off)));
        int li = (int)wxMin(w - 1, wxMax(0, (chain[(size_t)kmid] % w) - (int)std::lround(niN * off)));
        int lj = (int)wxMin(h - 1, wxMax(0, (chain[(size_t)kmid] / w) - (int)std::lround(njN * off)));
        int giR = i0 + ri, gjR = j0 + rj, giL = i0 + li, gjL = j0 + lj;
        double rhoR = m_rho[rj][ri], rhoL = m_rho[lj][li];
        double dSigma = std::isfinite(rhoR) && std::isfinite(rhoL) ? (rhoR - rhoL) : 0.0;
        double dTemp = 0, shear = 0;
        if (hasT) {
            double tR = msg.sst[(size_t)gjR * lonLen + giR];
            double tL = msg.sst[(size_t)gjL * lonLen + giL];
            if (std::isfinite(tR) && std::isfinite(tL)) dTemp = tR - tL;
        }
        if (msg.ucurr.size() && msg.vcurr.size()) {
            auto speedAt = [&](int gi, int gj) -> double {
                double u = msg.ucurr[(size_t)gj * lonLen + gi];
                double v = msg.vcurr[(size_t)gj * lonLen + gi];
                return (std::isfinite(u) && std::isfinite(v)) ? std::sqrt(u * u + v * v) : 0.0;
            };
            shear = std::fabs(speedAt(giR, gjR) - speedAt(giL, gjL));
        }
        double sampleDistKm = 2 * off * std::sqrt(
            (niN * cellKmQ) * (niN * cellKmQ) + (njN * cellKmP) * (njN * cellKmP));
        if (sampleDistKm < 1e-3) sampleDistKm = 1e-3;
        double gradPerKm = std::fabs(dSigma) / sampleDistKm;

        double sumG = 0; int nG = 0;
        for (int k = 0; k < nPix; k++) {
            float g = m_ngm[chain[(size_t)k] / w][chain[(size_t)k] % w];
            if (std::isfinite(g)) { sumG += g; nG++; }
        }
        double meanG = nG ? sumG / nG : 0;
        float strength = (p99 > thr) ? (float)wxMin(1.0, (meanG - thr) / (p99 - thr + 1e-30)) : 1.0f;
        if (strength < 0) strength = 0;

        FrontFeature ff;
        ff.cls = cls;
        ff.lengthKm = (float)lenKm;
        ff.dSigma = (float)dSigma;
        ff.dTemp = (float)dTemp;
        ff.gradPerKm = (float)gradPerKm;
        ff.shearAcross = (float)shear;
        ff.strength = strength;
        float lonMin = 1e30f, latMin = 1e30f, lonMax = -1e30f, latMax = -1e30f;
        ff.vOff = (int)m_set.arena.size();
        ff.vCnt = (int)(pts.size() / 2);
        for (size_t k = 0; k < pts.size(); k += 2) {
            m_set.arena.push_back((float)pts[k]);
            m_set.arena.push_back((float)pts[k + 1]);
            if (pts[k] < lonMin) lonMin = (float)pts[k];
            if (pts[k] > lonMax) lonMax = (float)pts[k];
            if (pts[k + 1] < latMin) latMin = (float)pts[k + 1];
            if (pts[k + 1] > latMax) latMax = (float)pts[k + 1];
        }
        ff.bbox[0] = lonMin; ff.bbox[1] = latMin;
        ff.bbox[2] = lonMax; ff.bbox[3] = latMax;
        m_set.fronts.push_back(ff);
        m_set.detected[FK_FRONT]++;
        ncdfLog("[feat] front len=%.0fkm verts=%d strength=%.2f dT=%.2f dSigma=%.2f grad=%.4f/km shear=%.3f\n",
            lenKm, ff.vCnt, strength, dTemp, dSigma, gradPerKm, shear);
    };

    // phase A: start at endpoints (exactly 1 member neighbor) — thin ridges
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (!member(i, j) || visited(i, j)) continue;
            if (nCount(i, j) == 1) {
                std::vector<int> chain = walkChain(i, j);
                emitChain(chain);
            }
        }
    }
    // phase B: closed loops only (every pixel exactly 2 neighbors) — no scribbles
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (!member(i, j) || visited(i, j)) continue;
            bool pure = true;
            for (int d = 0; d < 8 && pure; d++) {
                int ni = i + DX8[d], nj = j + DY8[d];
                if (member(ni, nj) && nCount(ni, nj) != 2) pure = false;
            }
            if (pure) {
                std::vector<int> chain = walkChain(i, j);
                emitChain(chain);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void FeatureEngine::computeAll(MainDialog *gui, int waves)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    // partial recompute: clear only the kinds this run refreshes (arena compaction)
    if (waves & WAVE_FLOW) m_set.detected[FK_CONVERG] = m_set.detected[FK_DIVERG] = m_set.detected[FK_SHEAR] = 0;
    if (waves & WAVE_RHO) m_set.detected[FK_FRONT] = 0;
    if (waves & WAVE_EDDY) m_set.detected[FK_EDDY] = 0;
    if (waves & WAVE_JET) m_set.detected[FK_JET] = 0;
    rebuildArena(!(waves & WAVE_RHO), !(waves & WAVE_EDDY), !(waves & WAVE_JET), !(waves & WAVE_FLOW));
    if (!gui || !anyEnabled()) return;

    const ncdfDataMessage &msg = gui->myMessage;
    if (!msg.lonValues || !msg.latValues || msg.lonLength < 16 || msg.latLength < 16) {
        ncdfLog("[feat] computeAll: grid too small or no coords\n");
        return;
    }

    // Verification deliverables (once per session / once per file)
    selfTestKernels();
    spotReport(gui);

    // ---- region -> index window (accumulate across ALL lon shifts; v1.1 fix
    //      for global -180/180 requests on 0/360 files being half-windowed) ----
    double lonShift[3] = {0.0, 360.0, -360.0};
    int i0 = (int)msg.lonLength, i1 = 0;
    for (int s = 0; s < 3; s++) {
        double loA = m_lonMin + lonShift[s], loB = m_lonMax + lonShift[s];
        for (size_t i = 0; i < msg.lonLength; i++) {
            double v = msg.lonValues[i];
            if (v >= loA - 1e-9 && v <= loB + 1e-9) {
                if ((int)i < i0) i0 = (int)i;
                if ((int)i + 1 > i1) i1 = (int)i + 1;
            }
        }
    }
    int j0 = (int)msg.latLength, j1 = 0;
    {
        double laA = wxMin(m_latMin, m_latMax), laB = wxMax(m_latMin, m_latMax);
        for (size_t j = 0; j < msg.latLength; j++) {
            double v = msg.latValues[j];
            if (v >= laA - 1e-9 && v <= laB + 1e-9) {
                if ((int)j < j0) j0 = (int)j;
                if ((int)j + 1 > j1) j1 = (int)j + 1;
            }
        }
    }
    if (i1 - i0 < 16 || j1 - j0 < 16) {
        ncdfLog("[feat] computeAll: region has no overlap with file data (i=%d..%d j=%d..%d)\n", i0, i1, j0, j1);
        return;
    }
    ncdfLog("[feat] computeAll: waves=%d window i=[%d,%d) j=[%d,%d) pts=%d\n", waves, i0, i1, j0, j1, (i1 - i0) * (j1 - j0));

    // Wave 1: flow-derived (convergence / divergence / shear)
    if ((waves & WAVE_FLOW) && needUV() && msg.hasCurrent()) {
        computeFlowDeriv(gui, i0, i1, j0, j1);
        extractZones(gui, i0, i1, j0, j1);
    }
    // Wave 2: density fronts (T/S)
    if ((waves & WAVE_RHO) && m_enabled[FK_FRONT] && (msg.hasSSTData() || msg.hasSalData())) {
        computeRhoNGM(gui, i0, i1, j0, j1);
        extractFronts(gui, i0, i1, j0, j1);
    }
    // Wave 4: strong current axes -- ALWAYS computed when eddies are on (the
    // meander filter needs them) even if the jet display checkbox is off;
    // only the copy into the render set is gated by FK_JET.
    if ((waves & (WAVE_JET | WAVE_EDDY)) && (m_enabled[FK_JET] || m_enabled[FK_EDDY]) && msg.hasCurrent()) {
        extractJets(gui, i0, i1, j0, j1);
    }
    // Wave 3: mesoscale eddies (zos contours + Liutex confirm + T/S typing)
    if ((waves & WAVE_EDDY) && needZos() && msg.hasZosData()) {
        extractEddies(gui, i0, i1, j0, j1);
    }

    // declutter prep (plan 4.1): strongest-first ordering -- draw/hit use caps
    std::sort(m_set.fronts.begin(), m_set.fronts.end(),
        [](const FrontFeature &a, const FrontFeature &b) { return a.strength > b.strength; });
    std::sort(m_set.jets.begin(), m_set.jets.end(),
        [](const JetFeature &a, const JetFeature &b) { return a.strength > b.strength; });

    auto t1 = std::chrono::high_resolution_clock::now();
    ncdfLog("[perf] FeatureEngine::computeAll(waves=%d) %lldms | fronts=%d eddies=%d converg=%d diverg=%d shear=%d\n",
        waves,
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        m_set.detected[FK_FRONT], m_set.detected[FK_EDDY], m_set.detected[FK_CONVERG], m_set.detected[FK_DIVERG], m_set.detected[FK_SHEAR]);
}


// ---------------------------------------------------------------------------
// L3 (P3-prep): strong current axis extraction (plan section 3.5, simplified).
// Speed-ridge tracking: per-row speed maxima above threshold linked across rows
// with lateral continuity -> axis polylines (Kuroshio/Gulf-Stream class jets).
void FeatureEngine::extractJets(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    if (!msg.hasCurrent()) return;
    int w = i1 - i0, h = j1 - j0;
    if (w < 16 || h < 16) return;
    const size_t lonLen = msg.lonLength;

    // speed grid (scratch, reused)
    m_mask.allocate(w, h);      // reuse abs-value scratch for speed
    for (int j = 0; j < h; j++) {
        float *sr = m_mask[j];
        for (int i = 0; i < w; i++) {
            double u = dv(msg.ucurr[(size_t)(j0 + j) * lonLen + (i0 + i)]);
            double v = dv(msg.vcurr[(size_t)(j0 + j) * lonLen + (i0 + i)]);
            sr[i] = (float)std::sqrt(u * u + v * v);
        }
    }
    // adaptive + absolute speed threshold (plan: axes > 0.4 m/s typical; P90 adaptive)
    float p90 = percentileOf(m_mask, 0, w, 0, h, 90.0f);
    float thr = wxMax(0.30f, p90);
    ncdfLog("[feat] jets: speed thr=%.3f (P90=%.3f)\n", thr, p90);

    // per-row local maxima above threshold
    std::vector<std::vector<int> > rowMax((size_t)h);
    for (int j = 1; j < h - 1; j++) {
        const float *sr = m_mask[j];
        for (int i = 2; i < w - 2; i++) {
            float s = sr[i];
            if (!(s > thr)) continue;
            bool isMax = true;
            for (int d = -2; d <= 2 && isMax; d++)
                if (sr[i + d] > s) isMax = false;
            if (isMax) rowMax[(size_t)j].push_back(i);
        }
    }

    // link maxima across rows (lateral continuity <= 5 px/row), grow chains
    std::vector<std::vector<int> > chains;   // chains of (j * w + i)
    std::vector<char> used((size_t)(w * h), 0);
    auto idx = [&](int i, int j) { return j * w + i; };
    for (int j0c = 1; j0c < h - 1; j0c++) {
        for (size_t m = 0; m < rowMax[(size_t)j0c].size(); m++) {
            int ci = rowMax[(size_t)j0c][m];
            if (used[(size_t)idx(ci, j0c)]) continue;
            // grow chain forward and backward
            std::vector<int> chain;
            chain.push_back(idx(ci, j0c));
            used[(size_t)idx(ci, j0c)] = 1;
            // forward
            int lastI = ci, lastJ = j0c;
            for (int j = j0c + 1; j < h - 1; j++) {
                int bestI = -1, bestD = 6;
                for (size_t k = 0; k < rowMax[(size_t)j].size(); k++) {
                    int ni = rowMax[(size_t)j][k];
                    if (used[(size_t)idx(ni, j)]) continue;
                    int d = std::abs(ni - lastI);
                    if (d < bestD) { bestD = d; bestI = ni; }
                }
                if (bestI < 0) break;
                chain.push_back(idx(bestI, j));
                used[(size_t)idx(bestI, j)] = 1;
                lastI = bestI; lastJ = j;
            }
            // backward
            lastI = ci; lastJ = j0c;
            for (int j = j0c - 1; j >= 1; j--) {
                int bestI = -1, bestD = 6;
                for (size_t k = 0; k < rowMax[(size_t)j].size(); k++) {
                    int ni = rowMax[(size_t)j][k];
                    if (used[(size_t)idx(ni, j)]) continue;
                    int d = std::abs(ni - lastI);
                    if (d < bestD) { bestD = d; bestI = ni; }
                }
                if (bestI < 0) break;
                chain.push_back(idx(bestI, j));
                used[(size_t)idx(bestI, j)] = 1;
                lastI = bestI; lastJ = j;
            }
            if (chain.size() >= 8) chains.push_back(chain);
        }
    }

    const double latMid = msg.latValues ? 0.5 * (msg.latValues[j0] + msg.latValues[j1 - 1]) : 0.0;
    const double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latMid * M_PI / 180.0);
    const double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
    const double minLenKm = 200.0;

    for (size_t c = 0; c < chains.size(); c++) {
        std::vector<int> &chain = chains[c];
        // sort by j (backward growth appended out of order)
        std::sort(chain.begin(), chain.end(), [&](int a, int b) { return (a / w) < (b / w); });
        // to lon/lat, unroll, length
        std::vector<double> pts;
        pts.reserve(chain.size() * 2);
        double sumSpd = 0, maxSpd = 0;
        for (size_t k = 0; k < chain.size(); k++) {
            int gi = i0 + chain[k] % w, gj = j0 + chain[k] / w;
            double lo = msg.lonValues ? msg.lonValues[gi] : gi;
            double la = msg.latValues ? msg.latValues[gj] : gj;
            pts.push_back(lo); pts.push_back(la);
            float s = m_mask[chain[k] / w][chain[k] % w];
            sumSpd += s;
            if (s > maxSpd) maxSpd = s;
        }
        unrollLons(pts);
        double lenKm = 0;
        for (size_t k = 2; k < pts.size(); k += 2) {
            double dLon = pts[k] - pts[k - 2];
            double dLat = pts[k + 1] - pts[k - 1];
            lenKm += std::sqrt(std::pow(dLon * kKmPerDeg * std::cos(latMid * M_PI / 180.0), 2)
                             + std::pow(dLat * kKmPerDeg, 2));
        }
        if (lenKm < minLenKm) continue;
        double meanSpd = sumSpd / chain.size();
        if (meanSpd < 0.35) continue;

        // half-max width at mid axis (transverse scan in local index space)
        double widthKm = 30.0;
        {
            size_t midK = chain.size() / 2;
            int mi = chain[midK] % w, mj = chain[midK] / w;
            float smax = m_mask[mj][mi];
            double half = smax * 0.5;
            int iL = mi, iR = mi;
            while (iL > 0 && m_mask[mj][iL] > half) iL--;
            while (iR < w - 1 && m_mask[mj][iR] > half) iR++;
            widthKm = (iR - iL) * cellKmQ;
        }

        JetFeature jf;
        jf.meanSpeed = (float)meanSpd;
        jf.maxSpeed = (float)maxSpd;
        jf.width_m = (float)(widthKm * 1000.0);
        jf.strength = (float)wxMin(1.0, meanSpd / 1.5);
        double tol = std::fabs(msg.iDirectionIncr) + std::fabs(msg.jDirectionIncr);
        douglasPeucker(pts, tol * 2);
        if (pts.size() < 4) continue;
        float lonMin = 1e30f, latMin = 1e30f, lonMax = -1e30f, latMax = -1e30f;
        jf.vOff = (int)m_jetArena.size();
        jf.vCnt = (int)(pts.size() / 2);
        for (size_t k = 0; k < pts.size(); k += 2) {
            m_jetArena.push_back((float)pts[k]);
            m_jetArena.push_back((float)pts[k + 1]);
            if (pts[k] < lonMin) lonMin = (float)pts[k];
            if (pts[k] > lonMax) lonMax = (float)pts[k];
            if (pts[k + 1] < latMin) latMin = (float)pts[k + 1];
            if (pts[k + 1] > latMax) latMax = (float)pts[k + 1];
        }
        jf.bbox[0] = lonMin; jf.bbox[1] = latMin;
        jf.bbox[2] = lonMax; jf.bbox[3] = latMax;
        m_jetCache.push_back(jf);
        ncdfLog("[feat] jet len=%.0fkm mean=%.2f max=%.2f width=%.0fkm verts=%d\n",
            lenKm, meanSpd, maxSpd, widthKm, jf.vCnt);
    }

    // display gate: copy into the render set ONLY when the jet checkbox is on
    if (m_enabled[FK_JET]) {
        for (size_t t = 0; t < m_jetCache.size(); t++) {
            JetFeature jf = m_jetCache[t];
            int off = (int)m_set.arena.size();
            for (int k = 0; k < jf.vCnt; k++) {
                m_set.arena.push_back(m_jetArena[(size_t)(jf.vOff + 2 * k)]);
                m_set.arena.push_back(m_jetArena[(size_t)(jf.vOff + 2 * k + 1)]);
            }
            jf.vOff = off;
            m_set.jets.push_back(jf);
            m_set.detected[FK_JET]++;
        }
    }
}

// ---------------------------------------------------------------------------
// Arena compaction: keep selected object kinds, drop others, fix vOff offsets.
void FeatureEngine::rebuildArena(bool keepFronts, bool keepEddies, bool keepJets, bool keepZones)
{
    std::vector<float> newArena;
    newArena.reserve(m_set.arena.size());
    if (keepFronts) {
        for (size_t f = 0; f < m_set.fronts.size(); f++) {
            FrontFeature ff = m_set.fronts[f];
            int off = (int)newArena.size();
            for (int k = 0; k < ff.vCnt; k++) {
                newArena.push_back(m_set.arena[(size_t)(ff.vOff + 2 * k)]);
                newArena.push_back(m_set.arena[(size_t)(ff.vOff + 2 * k + 1)]);
            }
            ff.vOff = off;
            m_set.fronts[f] = ff;
        }
    } else {
        m_set.fronts.clear();
    }
    if (keepEddies) {
        for (size_t e = 0; e < m_set.eddies.size(); e++) {
            EddyFeature ef = m_set.eddies[e];
            int off = (int)newArena.size();
            for (int k = 0; k < ef.vCnt; k++) {
                newArena.push_back(m_set.arena[(size_t)(ef.vOff + 2 * k)]);
                newArena.push_back(m_set.arena[(size_t)(ef.vOff + 2 * k + 1)]);
            }
            ef.vOff = off;
            m_set.eddies[e] = ef;
        }
    } else {
        m_set.eddies.clear();
    }
    if (keepJets) {
        for (size_t t = 0; t < m_set.jets.size(); t++) {
            JetFeature jf = m_set.jets[t];
            int off = (int)newArena.size();
            for (int k = 0; k < jf.vCnt; k++) {
                newArena.push_back(m_set.arena[(size_t)(jf.vOff + 2 * k)]);
                newArena.push_back(m_set.arena[(size_t)(jf.vOff + 2 * k + 1)]);
            }
            jf.vOff = off;
            m_set.jets[t] = jf;
        }
    } else {
        m_set.jets.clear();
    }
    if (keepZones) {
        for (size_t z = 0; z < m_set.zones.size(); z++) {
            ZoneFeature zf = m_set.zones[z];
            int off = (int)newArena.size();
            for (int k = 0; k < zf.vCnt; k++) {
                newArena.push_back(m_set.arena[(size_t)(zf.vOff + 2 * k)]);
                newArena.push_back(m_set.arena[(size_t)(zf.vOff + 2 * k + 1)]);
            }
            zf.vOff = off;
            m_set.zones[z] = zf;
        }
    } else {
        m_set.zones.clear();
    }
    m_set.arena.swap(newArena);
}

// ---------------------------------------------------------------------------
// L3 (P2): mesoscale eddy extraction (plan section 3.4)
//   1. local extrema of zos as candidates (subsampled window scan, early exit)
//   2. radial-probe closed-contour growth (per-azimuth coherent radius)
//   3. Liutex/vorticity rotation confirmation + polarity check
//   4. T/S core anomaly typing (warm/cold/haline)
void FeatureEngine::extractEddies(MainDialog *gui, int i0, int i1, int j0, int j1)
{
    const ncdfDataMessage &msg = gui->myMessage;
    if (!msg.hasZosData()) {
        ncdfLog("[feat] extractEddies: zos NOT loaded (hasZosData=0) - aborting");
        return;
    }
    int w = i1 - i0, h = j1 - j0;
    if (w < 16 || h < 16) return;
    const size_t lonLen = msg.lonLength;
    const bool hasUV = msg.hasCurrent();
    const bool hasT = msg.hasSSTData(), hasS = msg.hasSalData();
    int nCand = 0, nRad = 0, nAmp = 0, nDup = 0, nMeander = 0;

    const double latMid = msg.latValues ? 0.5 * (msg.latValues[j0] + msg.latValues[j1 - 1]) : 0.0;
    double cellKmQ = std::fabs(msg.iDirectionIncr) * kKmPerDeg * std::cos(latMid * M_PI / 180.0);
    double cellKmP = std::fabs(msg.jDirectionIncr) * kKmPerDeg;
    double cellKm = wxMax(0.5, 0.5 * (cellKmQ + cellKmP));

    // deformation radius band (plan: fallback 30-50km -> LR = 35km)
    const double LRkm = 35.0;
    const double rMinPx = 0.5 * LRkm / cellKm;
    const double rMaxPx = 250.0 / cellKm;   // absolute search ceiling (was 3xLR=105km: saturated real large eddies at 97km)
    const int WIN = 6;             // extremum search window (~65 km at 1/12 deg)
    const int NAZ = 24;
    double ampFloor = 0.005 + (m_sensPct - 70) * 0.002;  // slider-linked 0.5~5.5 cm

    // bilinear zos sample in local index space (NaN outside valid data)
    auto sampleZ = [&](double fi, double fj) -> double {
        int i = (int)fi, j = (int)fj;
        if (i < 0 || j < 0 || i + 1 >= w || j + 1 >= h) return (double)NAN;
        double tx = fi - i, ty = fj - j;
        double z00 = dv(msg.zos[(size_t)(j0 + j) * lonLen + (i0 + i)]);
        double z10 = dv(msg.zos[(size_t)(j0 + j) * lonLen + (i0 + i + 1)]);
        double z01 = dv(msg.zos[(size_t)(j0 + j + 1) * lonLen + (i0 + i)]);
        double z11 = dv(msg.zos[(size_t)(j0 + j + 1) * lonLen + (i0 + i + 1)]);
        return (1 - ty) * ((1 - tx) * z00 + tx * z10) + ty * ((1 - tx) * z01 + tx * z11);
    };
    auto valZ = [&](int i, int j) -> double {
        return dv(msg.zos[(size_t)(j0 + j) * lonLen + (i0 + i)]);
    };

    const double sgnLat = (latMid < 0) ? -1.0 : 1.0;
    std::vector<EddyFeature> found;
    std::vector<std::vector<double> > foundRings;   // per-eddy contour lon/lat

    for (int j = WIN; j < h - WIN; j += 2) {
        for (int i = WIN; i < w - WIN; i += 2) {
            double zc = valZ(i, j);
            if (!std::isfinite(zc)) continue;
            bool isMax = true, isMin = true;
            for (int dj = -WIN; dj <= WIN && (isMax || isMin); dj++) {
                for (int di = -WIN; di <= WIN && (isMax || isMin); di++) {
                    double zv = valZ(i + di, j + dj);
                    if (!std::isfinite(zv)) continue;
                    if (zv > zc) isMax = false;
                    if (zv < zc) isMin = false;
                }
            }
            if (!isMax && !isMin) continue;
            nCand++;
            int type = isMax ? +1 : -1;   // +1 anticyclone candidate (high zos)

            // Radial closed-contour growth (v2, fixed): walk each ray outward
            // while the field decays away from center; the first sustained
            // reversal (value climbing back, i.e. saddle/neighbor eddy) is the
            // ray edge.  Amplitude = center vs edge value (true amplitude,
            // NOT the detection floor -- the old definition was circular and
            // rejected 100% of candidates).
            const double riseTol = 0.3 * ampFloor;
            double rayR[24];
            double ringSum = 0; int ringN = 0;
            for (int a = 0; a < NAZ; a++) {
                double th = 2.0 * M_PI * a / NAZ;
                double ci = std::cos(th), si = std::sin(th);
                double rOk = 0, runExt = zc;
                for (double r = 1.5; r <= rMaxPx + 1e-9; r += 1.0) {
                    double zv = sampleZ(i + ci * r, j + si * r);
                    if (!std::isfinite(zv)) break;
                    if (type > 0) {
                        if (zv > runExt + riseTol) break;   // reversal upward -> edge
                        if (zv < runExt) runExt = zv;
                    } else {
                        if (zv < runExt - riseTol) break;   // reversal downward -> edge
                        if (zv > runExt) runExt = zv;
                    }
                    rOk = r;
                }
                rayR[a] = rOk;
                if (rOk >= rMinPx) { ringSum += runExt; ringN++; }
            }
            if (ringN < (3 * NAZ) / 4) continue;   // open contour
            nRad++;
            double rMean = 0;
            for (int a = 0; a < NAZ; a++) rMean += rayR[a];
            rMean /= NAZ;
            if (rMean < rMinPx) continue;
            // amplitude = center vs edge-ring mean (true amplitude)
            double edge = (ringN > 0) ? ringSum / ringN : zc;
            double amp = type * (zc - edge);   // positive when coherent
            if (amp < ampFloor) continue;
            nAmp++;

            // ---- jet-assist meander filter (P3-prep): eddy cores rotate slowly,
            // jet meander crests carry fast through-flow.  Reject when the core
            // is fast relative to its own ring, or (with jets known) when the
            // core sits on a jet axis while being fast.
            if (hasUV) {
                double coreSpd = 0, ringSpd = 0;
                {
                    double u = dv(msg.ucurr[(size_t)(j0 + j) * lonLen + (i0 + i)]);
                    double v = dv(msg.vcurr[(size_t)(j0 + j) * lonLen + (i0 + i)]);
                    coreSpd = std::sqrt(u * u + v * v);
                    int nR = 0;
                    for (int a = 0; a < NAZ; a++) {
                        double th2 = 2.0 * M_PI * a / NAZ;
                        int ii = i + (int)std::lround(std::cos(th2) * rMean);
                        int jj = j + (int)std::lround(std::sin(th2) * rMean);
                        if (ii < 0 || jj < 0 || ii >= w || jj >= h) continue;
                        double uu = dv(msg.ucurr[(size_t)(j0 + jj) * lonLen + (i0 + ii)]);
                        double vv = dv(msg.vcurr[(size_t)(j0 + jj) * lonLen + (i0 + ii)]);
                        double s2 = std::sqrt(uu * uu + vv * vv);
                        if (std::isfinite(s2)) { ringSpd += s2; nR++; }
                    }
                    if (nR > 0) ringSpd /= nR;
                }
                // jet proximity (axes ALWAYS cached for filtering, display-independent)
                bool onJet = false;
                {
                    double lo = msg.lonValues ? msg.lonValues[i0 + i] : 0;
                    double la = msg.latValues ? msg.latValues[j0 + j] : 0;
                    for (size_t t = 0; t < m_jetCache.size() && !onJet; t++) {
                        const JetFeature &jf = m_jetCache[t];
                        for (int k = 0; k < jf.vCnt; k++) {
                            double jl = m_jetArena[(size_t)(jf.vOff + 2 * k)];
                            double ja = m_jetArena[(size_t)(jf.vOff + 2 * k + 1)];
                            double dLon = lo - jl;
                            while (dLon > 180) dLon -= 360;
                            while (dLon < -180) dLon += 360;
                            double lx = dLon * kKmPerDeg * std::cos(la * M_PI / 180.0);
                            double ly = (la - ja) * kKmPerDeg;
                            if (std::sqrt(lx * lx + ly * ly) < wxMax(25.0, jf.width_m / 2000.0)) { onJet = true; break; }
                        }
                    }
                }
                bool meander = (coreSpd > 0.6 * ringSpd && coreSpd > 0.30) ||
                               (onJet && coreSpd > 0.5 * ringSpd && coreSpd > 0.30);
                if (meander) { nMeander++; continue; }
            }

            // dedupe: skip if near an already accepted eddy (km space)
            bool dup = false;
            for (size_t e = 0; e < found.size() && !dup; e++) {
                double lo = msg.lonValues ? msg.lonValues[i0 + i] : 0;
                double la = msg.latValues ? msg.latValues[j0 + j] : 0;
                double dLon = lo - found[e].clon;
                while (dLon > 180) dLon -= 360;
                while (dLon < -180) dLon += 360;
                double lx = dLon * kKmPerDeg * std::cos(la * M_PI / 180.0);
                double ly = (la - found[e].clat) * kKmPerDeg;
                double distKm = std::sqrt(lx * lx + ly * ly);
                double sepKm = 0.6 * (found[e].radius_m + rMean * cellKm * 1000.0) / 1000.0;
                if (distKm < sepKm) dup = true;
            }
            if (dup) { nDup++; continue; }

            // ---- Liutex / vorticity rotation confirmation ----
            double vort = NAN, liutex = 0;
            int gi = i0 + i, gj = j0 + j;
            if (hasUV && gi >= 2 && gj >= 2 && gi + 2 < (int)lonLen && gj + 2 < (int)msg.latLength) {
                double dudq = kernDerivQ(msg.ucurr.data(), lonLen, gi, gj);
                double dudp = kernDerivP(msg.ucurr.data(), lonLen, gi, gj);
                double dvdq = kernDerivQ(msg.vcurr.data(), lonLen, gi, gj);
                double dvdp = kernDerivP(msg.vcurr.data(), lonLen, gi, gj);
                double sgn = (msg.latValues && msg.latLength > 1 && msg.latValues[1] < msg.latValues[0]) ? -1.0 : 1.0;
                double dudx = dudq / (cellKmQ * 1000.0), dvdx = dvdq / (cellKmQ * 1000.0);
                double dudy = sgn * dudp / (cellKmP * 1000.0), dvdy = sgn * dvdp / (cellKmP * 1000.0);
                vort = dvdx - dudy;
                double det = dudx * dvdy - dudy * dvdx;
                double tr = dudx + dvdy;
                double disc = det - 0.25 * tr * tr;
                liutex = (disc > 0) ? std::sqrt(disc) : 0.0;
            }
            float confidence = 0.55f;   // no UV -> low
            if (std::isfinite(vort)) {
                // NH: anticyclone (type +1) has negative vorticity; SH flips
                // (per-eddy latitude sign -- window mid-lat was wrong for SH)
                double sgnLatE = (msg.latValues && msg.latValues[gj] < 0) ? -1.0 : 1.0;
                double expect = -type * sgnLatE;
                double vsgn = (vort >= 0) ? 1.0 : -1.0;
                if (vsgn == expect) confidence = (liutex > 0) ? 0.9f : 0.75f;
                else confidence = 0.35f;
            }

            // ---- T/S core typing ----
            double coreT = 0, coreS = 0, ringT = 0, ringS = 0;
            int nCore = 0, nRing = 0;
            {
                double rC = rMean * 0.5, rR = rMean * 1.7;
                for (int dj = -(int)rC - 1; dj <= (int)rC + 1; dj++)
                    for (int di = -(int)rC - 1; di <= (int)rC + 1; di++) {
                        if (di * di + dj * dj > rC * rC) continue;
                        int ii = i + di, jj = j + dj;
                        if (ii < 0 || jj < 0 || ii >= w || jj >= h) continue;
                        if (hasT) { double t = dv(msg.sst[(size_t)(j0 + jj) * lonLen + (i0 + ii)]); if (std::isfinite(t)) coreT += t; }
                        if (hasS) { double s = dv(msg.salinity[(size_t)(j0 + jj) * lonLen + (i0 + ii)]); if (std::isfinite(s)) coreS += s; }
                        nCore++;
                    }
                for (int a = 0; a < NAZ; a++) {
                    double th = 2.0 * M_PI * a / NAZ;
                    int ii = i + (int)std::lround(std::cos(th) * rR);
                    int jj = j + (int)std::lround(std::sin(th) * rR);
                    if (ii < 0 || jj < 0 || ii >= w || jj >= h) continue;
                    if (hasT) { double t = dv(msg.sst[(size_t)(j0 + jj) * lonLen + (i0 + ii)]); if (std::isfinite(t)) ringT += t; }
                    if (hasS) { double s = dv(msg.salinity[(size_t)(j0 + jj) * lonLen + (i0 + ii)]); if (std::isfinite(s)) ringS += s; }
                    nRing++;
                }
                coreT = (hasT && nCore > 0 && nRing > 0) ? (coreT / nCore - ringT / nRing) : 0;
                coreS = (hasS && nCore > 0 && nRing > 0) ? (coreS / nCore - ringS / nRing) : 0;
            }

            // ---- emit ----
            EddyFeature ef;
            ef.clon = msg.lonValues ? msg.lonValues[i0 + i] : (i0 + i);
            ef.clat = msg.latValues ? msg.latValues[j0 + j] : (j0 + j);
            ef.polarity = type;
            ef.radius_m = (float)(rMean * cellKm * 1000.0);
            ef.amplitude_m = (float)amp;
            ef.coreTAnom = (float)coreT;
            ef.coreSAnom = (float)coreS;
            ef.meanVort = std::isfinite(vort) ? (float)vort : 0.0f;
            ef.confidence = confidence;
            ef.strength = (float)wxMin(1.0, amp / 0.15);
            ef.vOff = 0; ef.vCnt = 0;
            float lonMin = 1e30f, latMin = 1e30f, lonMax = -1e30f, latMax = -1e30f;

            // contour ring: per-azimuth edge points (star-shaped, unrolled lons)
            std::vector<double> ring;
            ring.reserve((size_t)NAZ * 2);
            for (int a = 0; a < NAZ; a++) {
                double th = 2.0 * M_PI * a / NAZ;
                double px = i + std::cos(th) * rayR[a];
                double py = j + std::sin(th) * rayR[a];
                double lo = msg.lonValues ? msg.lonValues[i0 + (int)px] : (i0 + px);
                double la = msg.latValues ? msg.latValues[j0 + (int)py] : (j0 + py);
                ring.push_back(lo);
                ring.push_back(la);
            }
            unrollLons(ring);
            for (size_t k = 0; k < ring.size(); k += 2) {
                if (ring[k] < lonMin) lonMin = (float)ring[k];
                if (ring[k] > lonMax) lonMax = (float)ring[k];
                if (ring[k + 1] < latMin) latMin = (float)ring[k + 1];
                if (ring[k + 1] > latMax) latMax = (float)ring[k + 1];
            }
            ef.bbox[0] = lonMin; ef.bbox[1] = latMin;
            ef.bbox[2] = lonMax; ef.bbox[3] = latMax;
            found.push_back(ef);
            foundRings.push_back(ring);
        }
    }

    ncdfLog("[feat] eddy funnel: candidates=%d contour-ok=%d amp-ok=%d meander-drop=%d dup-drop=%d final=%zu ampFloor=%.3f rBand=%.1f..%.1fpx\n",
        nCand, nRad, nAmp, nMeander, nDup, found.size(), ampFloor, rMinPx, rMaxPx);

    // emit accepted eddies into the set (cap 40 per run, strongest first)
    std::vector<size_t> order(found.size());
    for (size_t e = 0; e < order.size(); e++) order[e] = e;
    for (size_t a = 0; a < order.size(); a++)
        for (size_t b = a + 1; b < order.size(); b++)
            if (found[order[b]].amplitude_m > found[order[a]].amplitude_m) {
                size_t t = order[a]; order[a] = order[b]; order[b] = t;
            }
    int emitted = 0;
    for (size_t oi = 0; oi < order.size() && emitted < 40; oi++, emitted++) {
        size_t e = order[oi];
        EddyFeature ef = found[e];
        ef.vOff = (int)m_set.arena.size();
        ef.vCnt = (int)(foundRings[e].size() / 2);
        for (size_t k = 0; k < foundRings[e].size(); k += 2) {
            m_set.arena.push_back((float)foundRings[e][k]);
            m_set.arena.push_back((float)foundRings[e][k + 1]);
        }
        m_set.eddies.push_back(ef);
        m_set.detected[FK_EDDY]++;
        ncdfLog("[feat] eddy %s (%.2f,%.2f) R=%.0fkm amp=%.3fm conf=%.2f dT=%.2f dS=%.2f vort=%.2e\n",
            ef.polarity > 0 ? "anticy" : "cyclone", ef.clat, ef.clon,
            ef.radius_m / 1000.0, ef.amplitude_m, ef.confidence,
            ef.coreTAnom, ef.coreSAnom, ef.meanVort);
    }
}

// ---------------------------------------------------------------------------
// Hover hit test (event-driven, not per frame).
// Body hit test shared by hover and click (kind: 0 eddy, 1 jet, 2 front,
// 3 zone, -1 none).  Whole feature body is the hit zone (P3-prep UX fix:
// clicking anywhere on a feature works, not only its 12px anchor icon).
int FeatureEngine::hitTestAt(double lon, double lat, int &idx) const
{
    idx = -1;
    const double latRad = lat * M_PI / 180.0;
    const double kx = kKmPerDeg * std::cos(latRad), ky = kKmPerDeg;

    // eddies: within claimed radius of the center
    for (size_t e = 0; e < m_set.eddies.size(); e++) {
        const EddyFeature &ef = m_set.eddies[e];
        for (int shift = -360; shift <= 360; shift += 360) {
            double dLon = (lon + shift) - ef.clon;
            double lx = dLon * kx, ly = (lat - ef.clat) * ky;
            double dist = std::sqrt(lx * lx + ly * ly) * 1000.0;
            if (dist < wxMax(10000.0, ef.radius_m)) { idx = (int)e; return 0; }
        }
    }

    // jets: distance to axis polyline < 25 km  (cap + bbox: performance)
    {
        double bestD = 25.0;
        int bestJ = -1;
        size_t jetLim = (m_set.jets.size() < 40) ? m_set.jets.size() : 40;
        for (size_t t = 0; t < jetLim; t++) {
            const JetFeature &jf = m_set.jets[t];
            for (int shift = -360; shift <= 360; shift += 360) {
                double lons0 = lon + shift;
                if (lons0 < jf.bbox[0] - 0.5 || lons0 > jf.bbox[2] + 0.5 ||
                    lat < jf.bbox[1] - 0.5 || lat > jf.bbox[3] + 0.5) continue;
                double lons = lon + shift;
                for (int k = 0; k + 1 < jf.vCnt; k++) {
                    double x1 = m_set.arena[(size_t)(jf.vOff + 2 * k)];
                    double y1 = m_set.arena[(size_t)(jf.vOff + 2 * k + 1)];
                    double x2 = m_set.arena[(size_t)(jf.vOff + 2 * k + 2)];
                    double y2 = m_set.arena[(size_t)(jf.vOff + 2 * k + 3)];
                    double dx = (x2 - x1) * kx, dy = (y2 - y1) * ky;
                    double px = (lons - x1) * kx, py = (lat - y1) * ky;
                    double len2 = dx * dx + dy * dy;
                    double tt = (len2 > 1e-12) ? (px * dx + py * dy) / len2 : 0.0;
                    if (tt < 0) tt = 0; else if (tt > 1) tt = 1;
                    double ex = px - tt * dx, ey = py - tt * dy;
                    double d = std::sqrt(ex * ex + ey * ey);
                    if (d < bestD) { bestD = d; bestJ = (int)t; }
                }
            }
        }
        if (bestJ >= 0) { idx = bestJ; return 1; }
    }

    // fronts: within 15 km of polyline  (cap + bbox: performance)
    {
        double bestD = 15.0;
        int bestF = -1;
        size_t frontLim = (m_set.fronts.size() < 150) ? m_set.fronts.size() : 150;
        for (size_t f = 0; f < frontLim; f++) {
            const FrontFeature &ff = m_set.fronts[f];
            for (int shift = -360; shift <= 360; shift += 360) {
                double lons = lon + shift;
                if (lons < ff.bbox[0] - 0.5 || lons > ff.bbox[2] + 0.5 ||
                    lat < ff.bbox[1] - 0.5 || lat > ff.bbox[3] + 0.5) continue;
                for (int k = 0; k + 1 < ff.vCnt; k++) {
                    double x1 = m_set.arena[(size_t)(ff.vOff + 2 * k)];
                    double y1 = m_set.arena[(size_t)(ff.vOff + 2 * k + 1)];
                    double x2 = m_set.arena[(size_t)(ff.vOff + 2 * k + 2)];
                    double y2 = m_set.arena[(size_t)(ff.vOff + 2 * k + 3)];
                    double dx = (x2 - x1) * kx, dy = (y2 - y1) * ky;
                    double px = (lons - x1) * kx, py = (lat - y1) * ky;
                    double len2 = dx * dx + dy * dy;
                    double tt = (len2 > 1e-12) ? (px * dx + py * dy) / len2 : 0.0;
                    if (tt < 0) tt = 0; else if (tt > 1) tt = 1;
                    double ex = px - tt * dx, ey = py - tt * dy;
                    double d = std::sqrt(ex * ex + ey * ey);
                    if (d < bestD) { bestD = d; bestF = (int)f; }
                }
            }
        }
        if (bestF >= 0) { idx = bestF; return 2; }
    }

    // zones (areal): point-in-polygon
    for (size_t z = 0; z < m_set.zones.size(); z++) {
        const ZoneFeature &zf = m_set.zones[z];
        for (int shift = -360; shift <= 360; shift += 360) {
            double lo = lon + shift;
            if (lo < zf.bbox[0] || lo > zf.bbox[2] || lat < zf.bbox[1] || lat > zf.bbox[3]) continue;
            bool inside = false;
            int n = zf.vCnt;
            for (int a = 0, b = n - 1; a < n; b = a++) {
                double xa = m_set.arena[(size_t)(zf.vOff + 2 * a)];
                double ya = m_set.arena[(size_t)(zf.vOff + 2 * a + 1)];
                double xb = m_set.arena[(size_t)(zf.vOff + 2 * b)];
                double yb = m_set.arena[(size_t)(zf.vOff + 2 * b + 1)];
                if ((ya > lat) != (yb > lat) &&
                    lo < (xb - xa) * (lat - ya) / (yb - ya + 1e-30) + xa)
                    inside = !inside;
            }
            if (inside) { idx = (int)z; return 3; }
        }
    }
    return -1;
}

bool FeatureEngine::hitTest(double lon, double lat, wxString &out) const
{
    int idx = -1;
    int kind = hitTestAt(lon, lat, idx);
    if (kind == 0) {
        const EddyFeature &ef = m_set.eddies[(size_t)idx];
        const char *nm = (ef.polarity > 0) ? "反气旋·暖芯" : "气旋·冷芯";
        out = wxString::Format(_T("%s R=%.0fkm amp=%.0fcm conf=%.0f%% dT=%+.2f"),
            wxString(nm), ef.radius_m / 1000.0,
            ef.amplitude_m * 100.0, ef.confidence * 100, ef.coreTAnom);
        return true;
    }
    if (kind == 1) {
        const JetFeature &jf = m_set.jets[(size_t)idx];
        out = wxString::Format(_T("%s 均速=%.2f 最大=%.2f m/s 宽=%.0fkm"),
            wxString("强流轴"), jf.meanSpeed, jf.maxSpeed, jf.width_m / 1000.0);
        return true;
    }
    if (kind == 2) {
        const FrontFeature &ff = m_set.fronts[(size_t)idx];
        const char *clsName = (ff.cls == 0) ? "密度锋" : (ff.cls == 1) ? "温度锋(降级)" : "盐度锋(降级)";
        out = wxString::Format(_T("%s 强度=%.0f%% ΔT=%.2f°C Δσ=%.2f 梯度=%.4f/km 长=%.0fkm 跨锋流速差=%.2fm/s"),
            wxString(clsName), ff.strength * 100,
            ff.dTemp, ff.dSigma, ff.gradPerKm, ff.lengthKm, ff.shearAcross);
        return true;
    }
    if (kind == 3) {
        const ZoneFeature &zf = m_set.zones[(size_t)idx];
        const char *nm = (zf.kind == FK_CONVERG) ? "辐聚区" : (zf.kind == FK_DIVERG) ? "辐散区" : "剪切区";
        const char *valName = (zf.kind == FK_SHEAR) ? "剪切" : "散度";
        out = wxString::Format(_T("%s %s=%.2f 峰值=%.2f 面积=%.0fkm² 强度=%.0f%%"),
            wxString(nm), wxString::FromUTF8(valName),
            zf.meanVal, zf.peakVal, zf.areaKm2, zf.strength * 100);
        return true;
    }
    return false;
}
