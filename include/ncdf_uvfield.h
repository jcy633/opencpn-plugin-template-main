/*********************************************************************
 * ncdf_uvfield.h — 双线性速度插值场 + 球面距离（两个主轴算法共用）
 *
 * 从 src/ncdf_kuroshio.cpp 原样抽出，行为未改。
 *
 * 提供：
 *   UVField              任意单调 / 均匀 / 非均匀 lat-lon 网格上的 U/V 双线性插值 + 陆海判定
 *   haversineKm          球面大圆距离（km）
 *   nanD / isNanD / finiteSpeed   有效性判定（填充值 / NaN / 超物理量级）
 *
 * 共用方：
 *   src/ncdf_kuroshio.cpp   Wang2026 流线约束最大流速法
 *   src/ncdf_seamt.cpp      Udalov 2026 SEAMT 最小 T 参数法
 *
 * 本头文件只提供“取速度”与“量距离”，不含任何积分步长语义。
 * 两种算法的步长语义不同（Wang2026 = 固定空间步长，SEAMT = 固定时间步），
 * 各自在自己的 .cpp 里实现，不要往这里加。
 *********************************************************************/
#ifndef NCDF_UVFIELD_H
#define NCDF_UVFIELD_H

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

#include "ncdf_reader.h"
#include "ncdf_landmask.h"

static const double kKmPerDeg = 111.0;        // 与官方代码一致
static const double kREarth   = 6371.0;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline double nanD() { return std::numeric_limits<double>::quiet_NaN(); }
static inline bool isNanD(double v) { return !std::isfinite(v); }

// 流速有效性：填充值 / NaN / 超物理量级一律视为陆地
static inline bool finiteSpeed(double v) {
    return std::isfinite(v) && v != (double)ncdf_NOTDEF && std::fabs(v) < 1.0e3;
}

static double haversineKm(double lon1, double lat1, double lon2, double lat2)
{
    double rlat1 = lat1 * M_PI / 180.0, rlat2 = lat2 * M_PI / 180.0;
    double dlat = (lat2 - lat1) * M_PI / 180.0, dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2)
             + std::cos(rlat1) * std::cos(rlat2) * std::sin(dlon / 2) * std::sin(dlon / 2);
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return 2.0 * kREarth * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// ===========================================================================
// U/V 场：双线性插值 + 陆海掩膜
// ===========================================================================
// 求覆盖 [lo,hi] 的最小下标区间，并各外扩 1 格（双线性插值要取 i0/i1 两个格点）。
// 数组单调即可（升/降序都行）。找不到交集时返回 false。
static bool uvfCoverRange(const double *a, size_t n,
                          double lo, double hi, size_t &o0, size_t &o1) {
    if (lo > hi) { double t = lo; lo = hi; hi = t; }
    size_t first = n, last = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i] >= lo - 1e-9 && a[i] <= hi + 1e-9) {
            if (first > i) first = i;
            last = i;
        }
    }
    if (first > last) return false;
    o0 = (first == 0) ? 0 : first - 1;
    o1 = (last == n - 1) ? n - 1 : last + 1;
    return true;
}

struct UVField {
    const double *u, *v;
    const double *lat, *lon;
    const NcdfLandMask *lm;     // 高分辨率掩膜；可为 NULL
    size_t nlat, nlon;
    // 行跨度：u/v 是二维数组，第 j 行第 i 列在 u[j*stride + i]。
    // 不裁剪时 stride == nlon；window() 裁出子矩形后 stride 保持原行宽，
    // 而 nlon 只表示窗口宽度 —— 子矩形在内存里不是连续的。
    size_t stride;
    bool latAsc, lonAsc;
    bool latUni, lonUni;
    double dLat, dLon;
    double cellDeg;

    // 复刻 MATLAB 语义用（Wang2026 移植验证）。默认 false，SEAMT 侧不受影响。
    // 见 sampleZeroFill 的说明。
    bool zeroFillInvalid = false;

    void bind(const ncdfDataMessage &msg) {
        u = msg.ucurr.empty() ? NULL : &msg.ucurr[0];
        v = msg.vcurr.empty() ? NULL : &msg.vcurr[0];
        lat = msg.latValues; lon = msg.lonValues;
        lm = NULL;
        nlat = msg.latLength; nlon = msg.lonLength;
        stride = nlon;
        latAsc = lonAsc = true;
        latUni = lonUni = false;
        dLat = dLon = 0.0; cellDeg = 0.1;
        if (nlat >= 2 && lat) {
            latAsc = (lat[nlat - 1] >= lat[0]);
            double sMin = 1e30, sMax = -1e30;
            for (size_t j = 1; j < nlat; j++) {
                double s = std::fabs(lat[j] - lat[j - 1]);
                if (s < sMin) sMin = s;
                if (s > sMax) sMax = s;
            }
            dLat = (sMin > 0 && sMin < 1e30) ? sMin : 0.1;
            latUni = (sMin > 0 && sMin < 1e30) && (sMax - sMin) <= 1e-6 * sMax;
        }
        if (nlon >= 2 && lon) {
            lonAsc = (lon[nlon - 1] >= lon[0]);
            double sMin = 1e30, sMax = -1e30;
            for (size_t i = 1; i < nlon; i++) {
                double s = std::fabs(lon[i] - lon[i - 1]);
                if (s < sMin) sMin = s;
                if (s > sMax) sMax = s;
            }
            dLon = (sMin > 0 && sMin < 1e30) ? sMin : 0.1;
            lonUni = (sMin > 0 && sMin < 1e30) && (sMax - sMin) <= 1e-6 * sMax;
        }
        if (dLat <= 0) dLat = 0.1;
        if (dLon <= 0) dLon = 0.1;
        cellDeg = std::min(dLat, dLon);
    }

    // 均匀网格 O(1) 定位；否则二分
    bool locate(const double *arr, size_t n, bool asc, bool uni, double step,
                double x, size_t &i0, size_t &i1, double &t) const {
        if (!arr || n == 0) return false;
        if (n == 1) {
            if (std::fabs(x - arr[0]) > 0.5 * cellDeg) return false;
            i0 = i1 = 0; t = 0; return true;
        }
        double lo = asc ? arr[0] : arr[n - 1];
        double hi = asc ? arr[n - 1] : arr[0];
        const double eps = 1e-9;
        if (x < lo - eps || x > hi + eps) return false;

        if (uni && step > 0) {
            double f = (x - arr[0]) / (asc ? step : -step);
            if (f < 0) f = 0;
            if (f > (double)(n - 1)) f = (double)(n - 1);
            size_t k = (size_t)f;
            if (k >= n - 1) { i0 = i1 = n - 1; t = 0; return true; }
            i0 = k; i1 = k + 1;
            t = f - (double)k;
            return true;
        }

        if (asc) {
            if (x <= arr[0])   { i0 = i1 = 0;     t = 0; return true; }
            if (x >= arr[n-1]) { i0 = i1 = n - 1; t = 0; return true; }
            size_t a = 0, b = n - 1;
            while (b - a > 1) { size_t m = a + (b - a) / 2; if (arr[m] <= x) a = m; else b = m; }
            i0 = a; i1 = a + 1;
        } else {
            if (x >= arr[0])   { i0 = i1 = 0;     t = 0; return true; }
            if (x <= arr[n-1]) { i0 = i1 = n - 1; t = 0; return true; }
            size_t a = 0, b = n - 1;
            while (b - a > 1) { size_t m = a + (b - a) / 2; if (arr[m] >= x) a = m; else b = m; }
            i0 = a; i1 = a + 1;
        }
        double d = arr[i1] - arr[i0];
        t = (d != 0.0) ? (x - arr[i0]) / d : 0.0;
        return true;
    }

    bool sample(double lonq, double latq, double &uo, double &vo) const {
        uo = vo = nanD();
        if (!u || !v || !lat || !lon || nlat < 1 || nlon < 1) return false;
        size_t j0, j1, i0, i1;
        double tj, ti;
        if (!locate(lat, nlat, latAsc, latUni, dLat, latq, j0, j1, tj)) return false;
        if (!locate(lon, nlon, lonAsc, lonUni, dLon, lonq, i0, i1, ti)) return false;

        double u00 = u[j0 * stride + i0], u01 = u[j0 * stride + i1];
        double u10 = u[j1 * stride + i0], u11 = u[j1 * stride + i1];
        double v00 = v[j0 * stride + i0], v01 = v[j0 * stride + i1];
        double v10 = v[j1 * stride + i0], v11 = v[j1 * stride + i1];
        if (!finiteSpeed(u00) || !finiteSpeed(u01) || !finiteSpeed(u10) || !finiteSpeed(u11)) return false;
        if (!finiteSpeed(v00) || !finiteSpeed(v01) || !finiteSpeed(v10) || !finiteSpeed(v11)) return false;

        double u0 = u00 + (u01 - u00) * ti, u1 = u10 + (u11 - u10) * ti;
        double v0 = v00 + (v01 - v00) * ti, v1 = v10 + (v11 - v10) * ti;
        uo = u0 + (u1 - u0) * tj;
        vo = v0 + (v1 - v0) * tj;
        return finiteSpeed(uo) && finiteSpeed(vo);
    }

    // 【复刻 MATLAB 语义】无效格点填 0 后再双线性插值。
    //
    // MATLAB 原版（main_Kuroshio_axis_and_boundaries.m）：
    //     sub_u(isnan(sub_u) | mask_grid < 1) = 0;
    //     Fu = griddedInterpolant(LatGrid, LonGrid, sub_u, 'linear');
    // 所以近岸点的插值是「真实值与 0 的加权」，而不是"四角有一个无效就失败"。
    //
    // 与 sample() 并列提供，**不动 sample() 的语义**（它与 SEAMT 共用，属控制变量）。
    // 流线因此可以穿过零速格（以零速继续走），不会像 sample() 那样在那里断掉。
    // 这是 Wang2026 移植验证用的复刻手段，不代表认可其物理合理性。
    bool sampleZeroFill(double lonq, double latq, double &uo, double &vo) const {
        uo = vo = 0.0;
        if (!u || !v || !lat || !lon || nlat < 1 || nlon < 1) return false;
        size_t j0, j1, i0, i1;
        double tj, ti;
        if (!locate(lat, nlat, latAsc, latUni, dLat, latq, j0, j1, tj)) return false;
        if (!locate(lon, nlon, lonAsc, lonUni, dLon, lonq, i0, i1, ti)) return false;

        // 无效格点按 0 参与插值（对应 MATLAB 的零填充）
        auto cell = [](const double *a, size_t j, size_t i, size_t stride) -> double {
            double x = a[j * stride + i];
            return finiteSpeed(x) ? x : 0.0;
        };
        double u00 = cell(u, j0, i0, stride), u01 = cell(u, j0, i1, stride);
        double u10 = cell(u, j1, i0, stride), u11 = cell(u, j1, i1, stride);
        double v00 = cell(v, j0, i0, stride), v01 = cell(v, j0, i1, stride);
        double v10 = cell(v, j1, i0, stride), v11 = cell(v, j1, i1, stride);

        double u0 = u00 + (u01 - u00) * ti, u1 = u10 + (u11 - u10) * ti;
        double v0 = v00 + (v01 - v00) * ti, v1 = v10 + (v11 - v10) * ti;
        uo = u0 + (u1 - u0) * tj;
        vo = v0 + (v1 - v0) * tj;
        return true;      // 恒成功：无效处返回 0 而不是失败
    }

    // 把视野裁到 [latMin,latMax] x [lonMin,lonMax]。
    // 【纯性能，不改语义】全球 2041x4320 网格的 u/v 各 70 MB，流线每步插值要读
    // 8 个格点，全在 cache 外；而提取全程只碰研究域那一小块（±4° 追踪边界）。
    // 裁完工作集降到 1 MB 量级，插值才跑得进 L2 —— 同样的算法能快好几倍。
    // 只改指针和计数，不做任何数值变换，结果与不裁时逐位一致。
    // 注意：窗口是原数组的子矩形，各行之间仍隔 stride 个元素，
    // 所以 stride 保持原行宽不变，nlon 只表示窗口宽度。
    void window(double latMin, double latMax, double lonMin, double lonMax) {
        if (!u || !v || !lat || !lon || nlat < 2 || nlon < 2) return;
        size_t j0 = 0, j1 = nlat - 1, i0 = 0, i1 = nlon - 1;
        if (!uvfCoverRange(lat, nlat, latMin, latMax, j0, j1)) return;
        if (!uvfCoverRange(lon, nlon, lonMin, lonMax, i0, i1)) return;
        u   += j0 * stride + i0;
        v   += j0 * stride + i0;
        lat += j0;
        lon += i0;
        nlat = j1 - j0 + 1;
        nlon = i1 - i0 + 1;
        // dLat/dLon/latAsc/lonAsc/latUni/lonUni 都是整列性质，子区间不变
    }

    // 陆海判定 = 有数据 且 高分辨率掩膜认为是海。
    // 掩膜覆盖不到的地方退回数据自身有效性（NaN / ncdf_NOTDEF）。
    bool isSea(double lonq, double latq) const {
        if (lm && lm->covers(latq, lonq) && lm->isLand(latq, lonq)) return false;
        double a, b;
        return sample(lonq, latq, a, b);
    }

    // 最近格点下标（均匀网格 O(1)，否则线性扫描）
    size_t nearest(const double *arr, size_t n, bool asc, bool uni, double step, double x) const {
        if (!arr || n == 0) return 0;
        if (n == 1) return 0;
        if (uni && step > 0) {
            double f = (x - arr[0]) / (asc ? step : -step);
            double r = std::floor(f + 0.5);
            if (r < 0) r = 0;
            if (r > (double)(n - 1)) r = (double)(n - 1);
            return (size_t)r;
        }
        size_t best = 0;
        double bestD = std::fabs(arr[0] - x);
        for (size_t i = 1; i < n; i++) {
            double d = std::fabs(arr[i] - x);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }
};

#endif // NCDF_UVFIELD_H
