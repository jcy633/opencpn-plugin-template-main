// ncdf_seamt.cpp — SEAMT 急流核心提取（流线集合分析求最小 T 参数）
//
// 忠实实现 Udalov (2026) Ocean Modelling 202, 102730 的四阶段算法：
//   [1] Γ_in 上取流速模最大点 P₀，布撒起点集合
//   [2] RK4 + 固定时间步 Δt 积分 dr/dt = u（冻结场）
//   [3] 筛出到达 Γ_out 的成功流线集合 G
//   [4] G 中 T 最小者即急流核心
//
// 与 ncdf_kuroshio（Wang2026，黑潮上游 115–132°E）完全独立，仅共用
// include/ncdf_uvfield.h 的速度插值与球面距离（控制变量，见 docs/SEAMT… §0.2）。
// 两者适用域不重叠，不要混用。
//
// 本文件不碰 GL，输出走 SeamtResult，由 UI 导出 / 渲染。
//
// 论文没写、但不写就没法实现的四处（docs/SEAMT… §2.6）：
//   * 出口只认东向跨越（再循环里的西向流会被误判为"到达出口"）
//   * 只认第一次东向穿越，出口点线性内插到 λ = λ_out
//   * RK4 中间采样点无效时退化为用端点速度的低阶步（不伪造流线）
//   * 不设 |u| 下限终止（慢的地方 T 大本来就是要区分的信号）

#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <vector>

#include "ncdf_reader.h"
#include "ncdf_seamt.h"
#include "ncdf_uvfield.h"

extern void ncdfLog(const char* format, ...);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 纬度 1 度对应的米数（与 kREarth 一致）
static const double kMPerDegLat = kREarth * 1000.0 * M_PI / 180.0;

// ===========================================================================
// 右端项：dr/dt = u，用经纬度自变量 + 球面度量
//   dλ/dt = u / (R·cos φ)   dφ/dt = v / R        [rad/s]
// 换算到 deg/s 后，RK4 的增量直接是经纬度增量，每步重算 cos φ。
// 研究域 25–41°N，cos φ 远离 0，无除零风险。
// ===========================================================================
static void rhsDeg(const UVField &F, double lon, double lat,
                   double &dlon, double &dlat)
{
    dlon = dlat = nanD();
    double u = 0.0, v = 0.0;
    if (!F.sample(lon, lat, u, v)) return;
    double cLat = std::cos(lat * M_PI / 180.0);
    if (!(std::fabs(cLat) > 1.0e-6)) return;
    dlon = u / (kMPerDegLat * cLat);   // deg/s
    dlat = v / kMPerDegLat;            // deg/s
}

// ===========================================================================
// RK4 走一步（固定时间步）
// 起点取不到速度 → 返回 false（搁浅）
// 中间采样点（k2/k3/k4）取不到 → 退化为端点速度的低阶步，只降精度不伪造流线
// ===========================================================================
static bool rk4Advance(const UVField &F, double lon0, double lat0, double dtSec,
                       double &lon1, double &lat1, int &nDegraded)
{
    double k1x, k1y, k2x, k2y, k3x, k3y, k4x, k4y;
    rhsDeg(F, lon0, lat0, k1x, k1y);
    if (isNanD(k1x) || isNanD(k1y)) return false;

    rhsDeg(F, lon0 + 0.5 * dtSec * k1x, lat0 + 0.5 * dtSec * k1y, k2x, k2y);
    if (isNanD(k2x) || isNanD(k2y)) { k2x = k1x; k2y = k1y; nDegraded++; }

    rhsDeg(F, lon0 + 0.5 * dtSec * k2x, lat0 + 0.5 * dtSec * k2y, k3x, k3y);
    if (isNanD(k3x) || isNanD(k3y)) { k3x = k1x; k3y = k1y; nDegraded++; }

    rhsDeg(F, lon0 + dtSec * k3x, lat0 + dtSec * k3y, k4x, k4y);
    if (isNanD(k4x) || isNanD(k4y)) { k4x = k1x; k4y = k1y; nDegraded++; }

    lon1 = lon0 + (dtSec / 6.0) * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
    lat1 = lat0 + (dtSec / 6.0) * (k1y + 2.0 * k2y + 2.0 * k3y + k4y);
    return true;
}

// ===========================================================================
// 单条流线
// ===========================================================================
struct SeamtTrace {
    std::vector<double> pts;   // lon,lat,...
    double T;                  // 天 = Σ Δt_i
    double tHav;               // 天 = Σ Δs_i / |u_i|
    int    nSteps;
    int    reason;
    double lonOut, latOut;
    int    nDegraded;
};

static void traceOne(const UVField &F,
                     double lonIn, double lonOut,
                     double latDomMin, double latDomMax,
                     double lonDomMin, double lonDomMax,
                     double lon0, double lat0,
                     double dtSec, double dtDays, int nMax,
                     SeamtTrace &t)
{
    t.pts.clear();
    t.pts.reserve((size_t)nMax * 2);
    t.pts.push_back(lon0);
    t.pts.push_back(lat0);
    t.T = 0.0; t.tHav = 0.0;
    t.nSteps = 0; t.nDegraded = 0;
    t.reason = SEAMT_MAXSTEP;
    t.lonOut = t.latOut = nanD();

    double lon = lon0, lat = lat0;

    for (int n = 0; n < nMax; n++) {
        double lon1, lat1;
        if (!rk4Advance(F, lon, lat, dtSec, lon1, lat1, t.nDegraded)) {
            t.reason = SEAMT_AGROUND;
            break;
        }

        // ---- 出口判定：本步东向跨越 Γ_out ----
        //
        // 【必须在域判定之前】Γ_out 就是研究域的东边界，跨过它的那一步落点必然
        // lon1 >= lonOut，会同时满足"越过 Γ_out"与"出域"两个条件。先判域的话
        // 每一条成功流线都会被记成 OUTDOMAIN，EXIT 恒为 0（2026-10-28 实测踩到）。
        //
        // 论文未写方向判定，但再循环里的西向流会被误判为"到达出口"，必须加。
        if (lon < lonOut && lon1 >= lonOut && lon1 > lon) {
            double f = (lonOut - lon) / (lon1 - lon);
            t.latOut = lat + f * (lat1 - lat);
            t.lonOut = lonOut;
            t.pts.push_back(lonOut);
            t.pts.push_back(t.latOut);

            // T = ∫dt。末步是部分步，按比例计——否则会引入最多 Δt 的假噪声，
            // 而我们正是要在 141 条之间比 T 的大小（论文的 Δt×N 是它的固定步长退化形）。
            t.T += f * dtDays;
            double dsKm = haversineKm(lon, lat, lonOut, t.latOut);
            double u0, v0;
            if (F.sample(lon, lat, u0, v0)) {
                double sp = std::hypot(u0, v0);
                if (sp > 1.0e-9) t.tHav += (dsKm * 1000.0) / sp / 86400.0;
            }
            t.nSteps = n + 1;
            t.reason = SEAMT_EXIT;
            return;
        }

        // 落点必须在研究域内且是海
        if (lon1 < lonDomMin || lon1 > lonDomMax ||
            lat1 < latDomMin || lat1 > latDomMax) {
            t.reason = SEAMT_OUTDOMAIN;
            break;
        }
        if (!F.isSea(lon1, lat1)) {
            t.reason = SEAMT_AGROUND;
            break;
        }

        // ---- T 累加 ----
        double dsKm = haversineKm(lon, lat, lon1, lat1);
        double u0, v0;
        if (F.sample(lon, lat, u0, v0)) {
            double sp = std::hypot(u0, v0);
            if (sp > 1.0e-9) t.tHav += (dsKm * 1000.0) / sp / 86400.0;
        }
        t.T += dtDays;

        lon = lon1; lat = lat1;
        t.pts.push_back(lon);
        t.pts.push_back(lat);
        t.nSteps = n + 1;
    }
}

// ===========================================================================
// 研究域内 |u| 的 99.5 分位（避免单点异常把 Δt 压得过小）
// ===========================================================================
static double estimateUMax(const UVField &F,
                           double lonMin, double lonMax,
                           double latMin, double latMax)
{
    if (!F.u || !F.v || !F.lat || !F.lon || F.nlat < 1 || F.nlon < 1) return 2.0;

    std::vector<double> sp;
    sp.reserve(8192);
    size_t nInDomain = 0;
    double bestSp = -1.0, bestLon = 0.0, bestLat = 0.0, bestU = 0.0, bestV = 0.0;
    size_t stepJ = (F.nlat > 400) ? (F.nlat / 400) : 1;
    size_t stepI = (F.nlon > 400) ? (F.nlon / 400) : 1;

    for (size_t j = 0; j < F.nlat; j += stepJ) {
        double la = F.lat[j];
        if (la < latMin || la > latMax) continue;
        for (size_t i = 0; i < F.nlon; i += stepI) {
            double lo = F.lon[i];
            if (lo < lonMin || lo > lonMax) continue;
            nInDomain++;
            double u = F.u[j * F.nlon + i], v = F.v[j * F.nlon + i];
            if (!finiteSpeed(u) || !finiteSpeed(v)) continue;
            double s = std::hypot(u, v);
            if (s <= 1.0e-6) continue;
            sp.push_back(s);
            if (s > bestSp) {
                bestSp = s; bestLon = lo; bestLat = la; bestU = u; bestV = v;
            }
        }
    }
    if (sp.empty()) return 2.0;

    size_t k = (size_t)(0.995 * (double)(sp.size() - 1));
    std::nth_element(sp.begin(), sp.begin() + (long)k, sp.end());
    double p = sp[k];

    // 域内有效格点比例：能区分"数据大面积缺测"与"流速真的弱"
    ncdfLog("[seamt] uMax scan: %u valid samples in domain (of %u cells), p99.5=%.3f m/s\n",
            (unsigned)sp.size(), (unsigned)nInDomain, p);
    // 域内最强流在哪里、往哪流 —— 用来判断 Γ_in 是不是压在轴上
    if (bestSp > 0.0) {
        ncdfLog("[seamt] uMax at (%.3f, %.3f) |u|=%.3f u=%.3f v=%.3f  bearing=%.0f deg\n",
                bestLon, bestLat, bestSp, bestU, bestV,
                std::atan2(bestU, bestV) * 180.0 / M_PI);
    }

    return (p > 1.0e-3) ? p : 2.0;
}

// ===========================================================================
// P₀：Γ_in 上的"射流"所在位置。剖面做 3 点高斯平滑后取最大。
//
// 平滑只影响种子布撒中心，不改变任何流线的积分与 T 比选。
//
// 【必须带下游方向过滤】论文原文是 "the maximum velocity magnitude **of the jet
// flow**" —— 是**射流**的最大流速，不是随便哪个强流。本插件用 1/12° 模式数据，
// 比论文的 AVISO（1/8° 且平滑）能分辨岛屿与水道；Γ_in 若穿过岛链（130°E 就穿过
// 土噶喇列岛），岛屿尾流 / 水道流的 |u| 可能比黑潮本体还大，纯取 |u| 最大会选错。
// 实测（2026-10-28）：P₀ 落在 29.46°N 的南向尾流（|u|=0.59, 航向 164°），
// 而轴在 30.25°N 的东向流上（|u|=0.47, 航向 87°）。
// 射流必须流向 Γ_out 才构成贯通路径，故要求下游分量 > 0；全无下游向点时退回 |u| 最大。
// ===========================================================================
static double findP0(const UVField &F, double lonIn,
                     double latMin, double latMax, double lonOut,
                     double &latOut)
{
    latOut = nanD();
    double dLat = F.dLat;
    if (!(dLat > 0.0)) dLat = 0.01;
    double step = dLat * 0.5;
    const double downStreamSign = (lonOut > lonIn) ? 1.0 : -1.0;

    std::vector<double> lats, sp, ds;
    for (double la = latMin; la <= latMax + 1.0e-9; la += step) {
        double u, v;
        if (!F.sample(lonIn, la, u, v)) continue;
        lats.push_back(la);
        sp.push_back(std::hypot(u, v));
        ds.push_back(downStreamSign * u);      // 下游方向的流速分量
    }
    if (sp.empty()) return -1.0;

    // 3 点高斯 [0.25, 0.5, 0.25]
    std::vector<double> sm(sp.size()), dm(sp.size());
    for (size_t i = 0; i < sp.size(); i++) {
        size_t im = (i > 0) ? i - 1 : i;
        size_t ip = (i + 1 < sp.size()) ? i + 1 : i;
        sm[i] = 0.25 * sp[im] + 0.5 * sp[i] + 0.25 * sp[ip];
        dm[i] = 0.25 * ds[im] + 0.5 * ds[i] + 0.25 * ds[ip];
    }

    // 优先：下游分量最大的点（即射流）
    size_t best = sp.size();
    for (size_t i = 0; i < dm.size(); i++) {
        if (dm[i] <= 0.0) continue;
        if (best == sp.size() || dm[i] > dm[best]) best = i;
    }
    // 退回：纯 |u| 最大
    if (best == sp.size()) {
        best = 0;
        for (size_t i = 1; i < sm.size(); i++) if (sm[i] > sm[best]) best = i;
        ncdfLog("[seamt] P0: no downstream-directed point on gamma_in, "
                "falling back to max |u|\n");
    }

    latOut = lats[best];
    ncdfLog("[seamt] P0: lat=%.4f |u|=%.3f down=%.3f (jet criterion)\n",
            latOut, sp[best], ds[best]);
    return sp[best];
}

// ===========================================================================
// 流速平面图（仅 G 为空时打印）：一眼看出射流在哪、为什么够不着。
// 输出行 = 纬度（北在上），列 = 经度，值 = |u|（m/s）。
// ===========================================================================
static void logSpeedMap(const UVField &F,
                        double lonMin, double lonMax,
                        double latMin, double latMax)
{
    const double dLonStep = 0.5, dLatStep = 0.5;
    ncdfLog("[seamt] speed map |u| m/s, rows=lat (N up), cols=lon %.1f..%.1f step %.1f\n",
            lonMin, lonMax, dLonStep);
    for (double la = latMax; la >= latMin - 1e-9; la -= dLatStep) {
        char buf[512];
        int pos = snprintf(buf, sizeof(buf), "[seamt]   %5.1f |", la);
        for (double lo = lonMin; lo <= lonMax + 1e-9 && pos < (int)sizeof(buf) - 8; lo += dLonStep) {
            double u = 0.0, v = 0.0;
            if (!F.sample(lo, la, u, v)) pos += snprintf(buf + pos, sizeof(buf) - pos, "  --");
            else                         pos += snprintf(buf + pos, sizeof(buf) - pos, "%4.1f", std::hypot(u, v));
        }
        ncdfLog("%s\n", buf);
    }
    // 经度刻度：每 0.5° 一列，但只在偶数列打标签，避免 %4.0f 把 130.5 舍成 130
    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "[seamt]         ");
    int col = 0;
    for (double lo = lonMin; lo <= lonMax + 1e-9 && pos < (int)sizeof(buf) - 8; lo += dLonStep, col++) {
        if ((col % 2) == 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "%4.0f", lo);
        else                pos += snprintf(buf + pos, sizeof(buf) - pos, "    ");
    }
    ncdfLog("%s\n", buf);
}


// ===========================================================================
// 轴贴合度诊断：核心线上的流速 vs 同一垂断面上的最大流速。
//
// SEAMT 的 min-T 路径**本来就不是**最大流速线（论文 Fig 6a：SEAMT 比 KLSM 更平滑、
// 路径更短、均速更低），所以核心略偏离速度脊是方法的正常表现。但偏离多少必须量化：
// 比值接近 1 = 贴着轴；明显偏低 = 有问题（例如集合被陆地挤成单侧、离散误差）。
//
// 只做诊断，**不拿它去改核心线** —— 那会破坏 min-T 的变分意义（见 docs §0.1）。
// ===========================================================================
static void computeAxisFit(const UVField &F, const std::vector<double> &core,
                           SeamtAxisFit &out)
{
    out.clear();
    size_t n = core.size() / 2;
    if (n < 8) return;

    const double halfKm = 60.0, stepKm = 2.0;
    const int nSamp = (int)(2.0 * halfKm / stepKm) + 1;   // 61 点，±60 km 每 2 km
    size_t stride = n / 20; if (stride < 1) stride = 1;

    double sumRatio = 0.0, sumSigned = 0.0, sumAbs = 0.0;
    std::vector<double> tmpLat, tmpRatio, tmpOff, tmpCoreTurn, tmpRidgeLon, tmpRidgeLat;
    tmpLat.reserve(32); tmpRatio.reserve(32); tmpOff.reserve(32);
    tmpCoreTurn.reserve(32); tmpRidgeLon.reserve(32); tmpRidgeLat.reserve(32);

    for (size_t i = 3; i + 3 < n; i += stride) {
        double lon = core[2 * i], lat = core[2 * i + 1];
        double mPerDegLon = kMPerDegLat * std::cos(lat * M_PI / 180.0);

        // 局部切向（米制单位向量）
        double ex = (core[2 * (i + 1)] - core[2 * (i - 1)]) * mPerDegLon;
        double ey = (core[2 * (i + 1) + 1] - core[2 * (i - 1) + 1]) * kMPerDegLat;
        double len = std::hypot(ex, ey);
        if (len < 1.0) continue;
        ex /= len; ey /= len;
        double px = -ey, py = ex;          // 法向

        // 局部曲率代理：i-2->i 与 i->i+2 两段的转角（度）。
        // 用来区分"弯道抄近路（方法特性）"与"系统性偏差（缺陷）"：
        // 偏移随转角增长 = 前者；与转角无关 = 后者。
        double turnDeg = 0.0;
        if (i >= 2 && i + 2 < n) {
            double ax = (core[2 * i] - core[2 * (i - 2)]) * mPerDegLon;
            double ay = (core[2 * i + 1] - core[2 * (i - 2) + 1]) * kMPerDegLat;
            double bx = (core[2 * (i + 2)] - core[2 * i]) * mPerDegLon;
            double by = (core[2 * (i + 2) + 1] - core[2 * i + 1]) * kMPerDegLat;
            double la2 = std::hypot(ax, ay), lb2 = std::hypot(bx, by);
            if (la2 > 1.0 && lb2 > 1.0) {
                double c = (ax * bx + ay * by) / (la2 * lb2);
                if (c > 1.0) c = 1.0;
                if (c < -1.0) c = -1.0;
                turnDeg = std::acos(c) * 180.0 / M_PI;
            }
        }

        double u0, v0;
        if (!F.sample(lon, lat, u0, v0)) continue;
        double spCore = std::hypot(u0, v0);

        double spMax = -1.0, offKm = 0.0, rLon = lon, rLat = lat;
        for (int k = 0; k < nSamp; k++) {
            double dKm = -halfKm + (double)k * stepKm;
            // dKm 是公里，px/py 是单位向量 -> 先换成米再除以米每度
            double qlon = lon + (px * dKm * 1000.0) / mPerDegLon;
            double qlat = lat + (py * dKm * 1000.0) / kMPerDegLat;
            double uq, vq;
            if (!F.sample(qlon, qlat, uq, vq)) continue;
            double s = std::hypot(uq, vq);
            if (s > spMax) { spMax = s; offKm = dKm; rLon = qlon; rLat = qlat; }
        }
        if (spMax <= 0.0) continue;

        double r = spCore / spMax;
        out.checks++;
        if (out.checks == 1 || r < out.ratioMin) out.ratioMin = r;
        sumRatio += r;
        sumSigned += offKm;
        sumAbs += std::fabs(offKm);

        tmpLat.push_back(lat);
        tmpRatio.push_back(r);
        tmpOff.push_back(offKm);
        tmpCoreTurn.push_back(turnDeg);
        tmpRidgeLon.push_back(rLon);
        tmpRidgeLat.push_back(rLat);
    }
    if (!out.checks) return;

    out.ratioAvg = sumRatio / (double)out.checks;
    out.offsetSignedKm = sumSigned / (double)out.checks;
    out.offsetAbsKm = sumAbs / (double)out.checks;

    // 速度脊转角：相邻脊点连线的夹角。**必须用脊自己的曲率**，不能用核心线的 ——
    // 核心已被 min-T 拉得很光滑，它剪掉的弯道在自身曲率上体现不出来（2026-10-29 踩过）。
    for (size_t i = 0; i < out.checks; i++) {
        double rt = 0.0;
        if (i >= 1 && i + 1 < (size_t)out.checks) {
            double ax = (tmpRidgeLon[i] - tmpRidgeLon[i - 1]) * kMPerDegLat
                      * std::cos(tmpRidgeLat[i] * M_PI / 180.0);
            double ay = (tmpRidgeLat[i] - tmpRidgeLat[i - 1]) * kMPerDegLat;
            double bx = (tmpRidgeLon[i + 1] - tmpRidgeLon[i]) * kMPerDegLat
                      * std::cos(tmpRidgeLat[i] * M_PI / 180.0);
            double by = (tmpRidgeLat[i + 1] - tmpRidgeLat[i]) * kMPerDegLat;
            double la2 = std::hypot(ax, ay), lb2 = std::hypot(bx, by);
            if (la2 > 1.0 && lb2 > 1.0) {
                double c = (ax * bx + ay * by) / (la2 * lb2);
                if (c > 1.0) c = 1.0;
                if (c < -1.0) c = -1.0;
                rt = std::acos(c) * 180.0 / M_PI;
            }
        }
        out.profile.push_back(tmpLat[i]);
        out.profile.push_back(tmpRatio[i]);
        out.profile.push_back(tmpOff[i]);
        out.profile.push_back(tmpCoreTurn[i]);
        out.profile.push_back(rt);
        out.ridge.push_back(tmpRidgeLon[i]);
        out.ridge.push_back(tmpRidgeLat[i]);
    }

    ncdfLog("[seamt] axis fit: %d checks, core/max ratio avg=%.3f min=%.3f, "
            "offset signed=%.1fkm abs=%.1fkm\n",
            out.checks, out.ratioAvg, out.ratioMin,
            out.offsetSignedKm, out.offsetAbsKm);
}

// ===========================================================================
// SeamtParams
// ===========================================================================
SeamtParams::SeamtParams()
{
    presetJapanSouth(*this);
    // 集合规模。论文用 141（间距 0.01°），那是为 AVISO（1/8°、平滑场）验证的。
    // 本插件 1/12° 模式数据上 T(起始纬度) 不平滑，0.01° 会错过窄的最优带：
    //   0.010 -> T=11.682   0.005 -> T=11.467   0.0033 -> T=11.304   0.0025 -> T=11.386
    // 取 281（间距 0.005°，是 141 的超集，min-T 只会更优），耗时约 2 倍。
    nPts          = 281;
    halfWidthDeg  = 0.70;
    dtDays        = 0.0;     // auto
    nMaxSteps     = 0;       // auto
    cflTarget     = 0.50;
    minSepDeg     = 0.05;
    // 子射流显著度阈值（天）。
    // 论文原判据是"低于左右邻居即为极小"，不做任何筛选；作者自述会检出噪声假支流。
    // 但实测（2026-10-29，281 点集合）T(φ_out) 曲线只有 16 点、只有 2 个极小，非常平滑 ——
    // 加密集合后噪声本来就低。而 0.5 d 的阈值会把真正的分叉也杀掉：
    //   35.818°N T=11.47 与 36.013°N T=11.68 是效率只差 1.8% 的两条分支，
    //   却因"与邻居差 0.21 d"被判为不显著。故降到 0.1 d。
    minProminence = 0.10;
    wantSubjetLines = false;
}

void SeamtParams::presetJapanSouth(SeamtParams &p)
{
    // 黑潮下游·日本以南。Γ_out 放在脱离点附近（房总半岛以东），
    // T(φ_out) 的极小值正好对应 Kawabe 的近岸 / 离岸 / 大弯曲三种路径态。
    //
    // 【与论文的两处差异，都是数据适配】论文用 AVISO L4（1/8°、平滑过），
    // Γ_in = 130°E 时黑潮干净地穿过、且在 28–31°N 内。本插件用 1/12° 模式数据，
    // 岛屿被分辨出来：130°E 正好从土噶喇列岛中间穿过，那一列最强只有 0.8 m/s；
    // 实测黑潮是从 (131.0°E, 31.0°N) 进入本域（1.4 m/s），且 131→133°E 段位于
    // 31–32.5°N —— 已经压到论文 28–31°N 搜索带的边缘甚至带外。
    // 所以：Γ_in 取 131.0°E，P₀ 搜索带放宽到整个研究域纬度范围。
    p.lonIn  = 131.0;
    p.lonOut = 142.0;
    // 域纬度必须给出口留足余量。实测（2026-10-29，海流1.nc）：核心在
    // (141.96°E, 35.999°N) 就被北墙 latDomMax=36.0 剪断 —— 离 142°E 只差 0.04°。
    // 粗步长下末步 5 km 恰好跨过墙角判成 EXIT（侥幸），细步长下规规矩矩撞墙判
    // OUTDOMAIN，两个步长给出完全不同的答案。取 38° 给黑潮延伸体留出余量。
    p.latDomMin = 24.0;  p.latDomMax = 38.0;
    p.latP0Min  = p.latDomMin;
    p.latP0Max  = p.latDomMax;
}

void SeamtParams::presetPaperDomain(SeamtParams &p)
{
    // Udalov 2026 原域：覆盖黑潮延伸体，可做分叉 / 出口纬度方差 / 北移趋势。
    // Γ_in 与 P₀ 带的取值同上，理由见 presetJapanSouth 的注释。
    p.lonIn  = 131.0;
    p.lonOut = 160.0;
    p.latDomMin = 27.0;  p.latDomMax = 41.0;
    p.latP0Min  = p.latDomMin;
    p.latP0Max  = p.latDomMax;
}

void SeamtParams::normalize()
{
    if (nPts < 3) nPts = 3;
    if ((nPts % 2) == 0) nPts += 1;              // 取奇数，保证关于 P₀ 对称
    if (!(halfWidthDeg > 0.0)) halfWidthDeg = 0.70;
    if (!(cflTarget > 0.0)) cflTarget = 0.50;

    if (latP0Min > latP0Max) std::swap(latP0Min, latP0Max);
    if (latDomMin > latDomMax) std::swap(latDomMin, latDomMax);
    if (latP0Min < latDomMin) latP0Min = latDomMin;
    if (latP0Max > latDomMax) latP0Max = latDomMax;
    if (latP0Min > latP0Max) std::swap(latP0Min, latP0Max);

    if (minSepDeg < 0.0) minSepDeg = 0.0;
    if (minProminence < 0.0) minProminence = 0.0;
}

// ===========================================================================
// SeamtResult
// ===========================================================================
void SeamtResult::clear()
{
    ok = false;
    message.clear();
    core.clear();
    subjetLines.clear();
    subjetLineLen.clear();
    tCurve.clear();
    traceTable.clear();
    subjets.clear();
    minT = meanSpeed = lengthKm = 0.0;
    latIn = latOut = varLatOut = 0.0;
    tHavCheck = 0.0;
    dtDays = uMaxEst = dxMinKm = cfl = 0.0;
    nSuccess = nTotal = nDegraded = nNearMiss = 0;
    for (int i = 0; i < SEAMT_STOP_COUNT; i++) reasonCount[i] = 0;
    elapsedMs = 0.0;
}

// ===========================================================================
// SeamtExtractor
// ===========================================================================
SeamtExtractor::SeamtExtractor() : m_landMask(NULL) {}

bool SeamtExtractor::extract(const ncdfDataMessage &msg, SeamtResult &out)
{
    typedef std::chrono::steady_clock clk;
    clk::time_point t0 = clk::now();

    out.clear();
    params.normalize();

    // ---------------- 前置检查 ----------------
    if (!msg.hasCurrent()) {
        out.message = _T("无 U/V 流场数据，无法提取");
        ncdfLog("[seamt] FAIL: no U/V data\n");
        return false;
    }
    if (!msg.latValues || !msg.lonValues || msg.latLength < 2 || msg.lonLength < 2) {
        out.message = _T("坐标数组缺失，无法提取");
        ncdfLog("[seamt] FAIL: missing lat/lon arrays\n");
        return false;
    }

    UVField F;
    F.bind(msg);
    F.lm = m_landMask;

    // Γ_in 必须落在数据经度范围内
    double lonDataMin = std::min(F.lon[0], F.lon[F.nlon - 1]);
    double lonDataMax = std::max(F.lon[0], F.lon[F.nlon - 1]);
    if (params.lonIn < lonDataMin - 1.0e-6 || params.lonIn > lonDataMax + 1.0e-6) {
        out.message = wxString::Format(
            _T("Γ_in = %.2f°E 不在数据经度范围 %.2f~%.2f°E 内"),
            params.lonIn, lonDataMin, lonDataMax);
        ncdfLog("[seamt] FAIL: lonIn %.3f outside data lon range %.3f..%.3f\n",
                params.lonIn, lonDataMin, lonDataMax);
        return false;
    }

    // ---------------- Δt / N_max ----------------
    double dxLatKm = F.dLat * kMPerDegLat / 1000.0;
    double latMid  = 0.5 * (params.latDomMin + params.latDomMax);
    double dxLonKm = F.dLon * kMPerDegLat * std::cos(latMid * M_PI / 180.0) / 1000.0;
    double dxMinKm = std::min(dxLatKm, dxLonKm);
    if (!(dxMinKm > 1.0e-6)) dxMinKm = 9.0;

    double lonDomMin = std::min(params.lonIn, params.lonOut);
    double lonDomMax = std::max(params.lonIn, params.lonOut);

    double uMaxEst = estimateUMax(F, lonDomMin, lonDomMax,
                                  params.latDomMin, params.latDomMax);

    double dtDays = params.dtDays;
    if (!(dtDays > 0.0)) {
        dtDays = params.cflTarget * (dxMinKm * 1000.0) / uMaxEst / 86400.0;
        if (dtDays < 0.01) dtDays = 0.01;
        if (dtDays > 0.05) dtDays = 0.05;
    }
    int nMax = params.nMaxSteps;
    if (nMax <= 0) {
        nMax = (int)std::ceil(300.0 / dtDays);   // 形式积分量上限 300 天，同论文
        if (nMax < 100) nMax = 100;
        if (nMax > 200000) nMax = 200000;
    }
    double dtSec = dtDays * 86400.0;
    double cfl = uMaxEst * dtSec / (dxMinKm * 1000.0);
    out.dtDays = dtDays;
    out.uMaxEst = uMaxEst;
    out.dxMinKm = dxMinKm;
    out.cfl = cfl;

    ncdfLog("[seamt] grid lat=%u lon=%u dLat=%.5f dLon=%.5f dxMin=%.2fkm\n",
            (unsigned)F.nlat, (unsigned)F.nlon, F.dLat, F.dLon, dxMinKm);
    ncdfLog("[seamt] uMax99.5=%.3f m/s  dt=%.5f d (%.1f s)  CFL=%.3f  nMax=%d\n",
            uMaxEst, dtDays, dtSec, cfl, nMax);
    if (dxMinKm > 30.0) {
        ncdfLog("[seamt] WARN: dxMin=%.1fkm too coarse to resolve jet cross-structure\n",
                dxMinKm);
        out.message = _T("分辨率偏粗，射流横结构可能分辨不出");
    }

    // ---------------- [1] P₀ 与起点集合 ----------------
    double latP0 = nanD();
    double spP0 = findP0(F, params.lonIn, params.latP0Min, params.latP0Max,
                         params.lonOut, latP0);
    if (isNanD(latP0) || spP0 < 0.0) {
        out.message = wxString::Format(
            _T("Γ_in 上 %.1f~%.1f°N 取不到有效流速，无法布撒种子"),
            params.latP0Min, params.latP0Max);
        ncdfLog("[seamt] FAIL: no valid velocity along lonIn=%.3f\n", params.lonIn);
        return false;
    }
    // Γ_in 没切到射流时早说。论文 32 年逐日 G 非空，所以"取不到轴"基本都是
    // 入口经线放错，不是流场里没有贯通急流。
    if (spP0 < 0.5) {
        ncdfLog("[seamt] WARN: best jet at gamma_in is only %.2f m/s — "
                "gamma_in probably does not cut the Kuroshio; "
                "move it toward the strongest flow shown by 'uMax at' above\n", spP0);
    }

    int K = (params.nPts - 1) / 2;
    double stepDeg = params.halfWidthDeg / (double)K;
    ncdfLog("[seamt] P0 = %.4f N (|u|=%.3f m/s), ensemble %d pts +-%.2f deg step %.4f\n",
            latP0, spP0, params.nPts, params.halfWidthDeg, stepDeg);

    // 集合布撒：由内向外交替向两侧取点，跳过无效点（陆地 / 缺测），凑满 nPts 个
    // 有效起点为止，向外最多扩到 maxExtDeg。
    //
    // 【为什么不用论文的对称 ±halfWidth 窗口】论文的 ±0.7° 是按"核心最大经向位移 1.4°"
    // 定的，前提是**两侧都是水**。本插件 1/12° 数据里 Γ_in 常紧贴岛屿（131°E 北边就是九州），
    // 对称窗口会被陆地吃掉一半，而且**全落在同一侧**：实测 141 个种子只剩 33 个有效、
    // 全在离岸侧，min-T 搜索只能从离岸侧起跳，核心被系统性推向离岸 —— axis fit
    // 有符号偏移 -7.5 km，21 个检查点 20 个同号。
    // 由内向外按序取有效点，一侧被挡住时自动向有水的一侧补足，间距仍是 stepDeg。
    const double maxExtDeg = params.halfWidthDeg * 3.0;
    int kMax = (int)std::ceil(maxExtDeg / stepDeg);

    std::vector<double> seedLat;
    seedLat.reserve((size_t)params.nPts);
    int seedOutOfRange = 0, seedNoData = 0;
    {
        double u, v;
        if (F.sample(params.lonIn, latP0, u, v)) seedLat.push_back(latP0);
        else seedNoData++;
    }
    for (int k = 1; k <= kMax && (int)seedLat.size() < params.nPts; k++) {
        for (int s = -1; s <= 1; s += 2) {
            if ((int)seedLat.size() >= params.nPts) break;
            double la = latP0 + (double)(s * k) * stepDeg;
            if (la < params.latDomMin || la > params.latDomMax) { seedOutOfRange++; continue; }
            double u, v;
            if (!F.sample(params.lonIn, la, u, v)) { seedNoData++; continue; }   // 陆地 / 缺测
            seedLat.push_back(la);
        }
    }
    std::sort(seedLat.begin(), seedLat.end());

    out.nTotal = (int)seedLat.size();
    if (out.nTotal > 0) {
        ncdfLog("[seamt] seeds: %d requested, %d kept (rejected outOfDomain=%d noData=%d), "
                "lat span %.4f..%.4f\n",
                params.nPts, out.nTotal, seedOutOfRange, seedNoData,
                seedLat.front(), seedLat.back());
    } else {
        ncdfLog("[seamt] seeds: %d requested, 0 kept (rejected outOfDomain=%d noData=%d)\n",
                params.nPts, seedOutOfRange, seedNoData);
    }
    if (out.nTotal < 3) {
        out.message = wxString::Format(
            _T("有效起点仅 %d 个（应有 %d 个），集合不足以比选"),
            out.nTotal, params.nPts);
        ncdfLog("[seamt] FAIL: only %d valid seeds\n", out.nTotal);
        return false;
    }

    // ---------------- [2] 积分流线族 ----------------
    //
    // 决定性诊断：从 P0 试走 20 步，把每步的速度与位移直接打出来。
    // G 为空时最容易误判成"物理上无贯通急流"，其实是数值问题——
    // 速度正常而位移离谱是积分/度量的问题；速度本身≈0 是数据的问题。
    {
        double lo = params.lonIn, la = latP0;
        ncdfLog("[seamt] probe: P0=(%.4f,%.4f) dtSec=%.1f\n", lo, la, dtSec);

        // Γ_in 上的流速剖面（每 0.25°）：直接看出轴在哪、往哪流
        ncdfLog("[seamt] gamma_in profile (lon=%.3f), lat / |u| / u / v / bearing\n", lo);
        for (double lp = params.latP0Min - 1.0; lp <= params.latP0Max + 1.0 + 1e-9; lp += 0.25) {
            double u = 0.0, v = 0.0;
            bool ok = F.sample(lo, lp, u, v);
            ncdfLog("[seamt]   lat %6.2f %s|u|=%7.4f u=%8.4f v=%8.4f brg=%6.1f\n",
                    lp, ok ? "" : "FAIL ", ok ? std::hypot(u, v) : -1.0,
                    ok ? u : 0.0, ok ? v : 0.0,
                    ok ? std::atan2(u, v) * 180.0 / M_PI : 0.0);
        }

        for (int s = 0; s < 20; s++) {
            double u = 0.0, v = 0.0;
            bool ok = F.sample(lo, la, u, v);
            double lo1 = lo, la1 = la;
            int deg = 0;
            bool adv = rk4Advance(F, lo, la, dtSec, lo1, la1, deg);
            double ddeg = adv ? std::hypot(lo1 - lo, la1 - la) : 0.0;
            ncdfLog("[seamt]   step %2d (%.5f,%.5f) %s|u|=%8.4f u=%8.4f v=%8.4f -> (%.5f,%.5f) d=%.5fdeg%s\n",
                    s, lo, la, ok ? "" : "FAIL ", ok ? std::hypot(u, v) : -1.0,
                    ok ? u : 0.0, ok ? v : 0.0, lo1, la1, ddeg,
                    adv ? "" : "  ADV-FAIL");
            if (!adv) break;
            lo = lo1; la = la1;
        }
        ncdfLog("[seamt] probe: after 20 steps -> (%.5f, %.5f)\n", lo, la);
    }

    std::vector<SeamtTrace> traces(seedLat.size());
    for (size_t i = 0; i < seedLat.size(); i++) {
        traceOne(F, params.lonIn, params.lonOut,
                 params.latDomMin, params.latDomMax, lonDomMin, lonDomMax,
                 params.lonIn, seedLat[i], dtSec, dtDays, nMax, traces[i]);
        out.reasonCount[traces[i].reason]++;
        out.nDegraded += traces[i].nDegraded;
        // 出域但离 Γ_out 很近 = 研究域边界在剪流。这类流线本该成功，
        // 被边界吃掉后 min-T 会被次优路径顶掉（2026-10-29 实测踩到）。
        if (traces[i].reason == SEAMT_OUTDOMAIN && traces[i].pts.size() >= 2) {
            double endLon = traces[i].pts[traces[i].pts.size() - 2];
            if (std::fabs(endLon - params.lonOut) < 0.2) out.nNearMiss++;
        }
    }
    // 逐条流线记录（诊断用）。必须在这里填，后面会 swap 释放 traces。
    out.traceTable.reserve(traces.size() * 7);
    for (size_t i = 0; i < traces.size(); i++) {
        out.traceTable.push_back(seedLat[i]);
        out.traceTable.push_back((double)traces[i].reason);
        out.traceTable.push_back((double)traces[i].nSteps);
        out.traceTable.push_back(traces[i].T);
        out.traceTable.push_back(traces[i].latOut);
        const std::vector<double> &pp = traces[i].pts;
        out.traceTable.push_back(pp.size() >= 2 ? pp[pp.size() - 2] : 0.0);
        out.traceTable.push_back(pp.size() >= 1 ? pp[pp.size() - 1] : 0.0);
    }

    // ---------------- [3] 成功集合 G ----------------
    std::vector<size_t> G;
    G.reserve(traces.size());
    for (size_t i = 0; i < traces.size(); i++) {
        if (traces[i].reason == SEAMT_EXIT) G.push_back(i);
    }
    out.nSuccess = (int)G.size();

    ncdfLog("[seamt] traces=%u  G=%d  reasons EXIT=%d OUTDOMAIN=%d AGROUND=%d MAXSTEP=%d  degraded=%d nearMiss=%d\n",
            (unsigned)traces.size(), out.nSuccess,
            out.reasonCount[SEAMT_EXIT], out.reasonCount[SEAMT_OUTDOMAIN],
            out.reasonCount[SEAMT_AGROUND], out.reasonCount[SEAMT_MAXSTEP],
            out.nDegraded, out.nNearMiss);
    if (out.nNearMiss > 0) {
        ncdfLog("[seamt] WARN: %d streamlines left the domain within 0.2deg of gamma_out — "
                "the domain boundary is clipping the flow; widen latDomMin/latDomMax\n",
                out.nNearMiss);
    }

    // 诊断：各终止原因的步数与终点分布。G 为空时靠这个定位，不要靠猜。
    {
        double stSum[SEAMT_STOP_COUNT] = {0.0}, loSum[SEAMT_STOP_COUNT] = {0.0},
               laSum[SEAMT_STOP_COUNT] = {0.0};
        int cnt[SEAMT_STOP_COUNT] = {0};
        for (size_t i = 0; i < traces.size(); i++) {
            int r = traces[i].reason;
            if (r < 0 || r >= SEAMT_STOP_COUNT) continue;
            cnt[r]++;
            stSum[r] += (double)traces[i].nSteps;
            const std::vector<double> &p = traces[i].pts;
            if (p.size() >= 2) {
                loSum[r] += p[p.size() - 2];
                laSum[r] += p[p.size() - 1];
            }
        }
        static const char *kReasonName[SEAMT_STOP_COUNT] =
            { "EXIT", "OUTDOMAIN", "AGROUND", "MAXSTEP" };
        for (int r = 0; r < SEAMT_STOP_COUNT; r++) {
            if (!cnt[r]) continue;
            ncdfLog("[seamt]   %-9s n=%-4d avgSteps=%-6.0f avgEnd=(%.3f, %.3f)\n",
                    kReasonName[r], cnt[r], stSum[r] / (double)cnt[r],
                    loSum[r] / (double)cnt[r], laSum[r] / (double)cnt[r]);
        }
    }
    // 前 3 条未成功流线的完整终点，直接看出卡在哪
    {
        int shown = 0;
        for (size_t i = 0; i < traces.size() && shown < 3; i++) {
            if (traces[i].reason == SEAMT_EXIT) continue;
            const std::vector<double> &p = traces[i].pts;
            ncdfLog("[seamt]   fail#%d seed=%.4fN reason=%d steps=%d end=(%.3f,%.3f) pts=%u\n",
                    shown, seedLat[i], traces[i].reason, traces[i].nSteps,
                    (p.size() >= 2) ? p[p.size() - 2] : 0.0,
                    (p.size() >= 1) ? p[p.size() - 1] : 0.0,
                    (unsigned)(p.size() / 2));
            shown++;
        }
    }

    if (G.empty()) {
        // 论文把 G 为空定义为"无贯通急流"的物理诊断。但要注意：作者 1993–2024
        // 逐日跑了 32 年，**每一天都拿到非空 G**。所以在这个海域出现 G 为空，
        // 几乎总是数据 / 研究域 / 参数的问题，不是"黑潮断了"。
        logSpeedMap(F, lonDomMin, lonDomMax, params.latDomMin, params.latDomMax);
        out.ok = false;
        out.message = _T("未找到贯通急流（G 为空）。注意：论文 32 年逐日均非空，"
                         "此情况通常是研究域或数据问题，请查看调试日志的 probe 段");
        out.elapsedMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        ncdfLog("[seamt] G empty — 32-year reference is always non-empty; "
                "treat as data/domain problem, not physics\n");
        return false;
    }

    // ---------------- [4] 最小 T 即核心 ----------------
    size_t best = G[0];
    for (size_t i = 1; i < G.size(); i++) {
        if (traces[G[i]].T < traces[best].T) best = G[i];
    }
    out.minT   = traces[best].T;
    out.core   = traces[best].pts;
    out.latIn  = seedLat[best];
    out.latOut = traces[best].latOut;

    out.lengthKm = 0.0;
    size_t nCore = out.core.size() / 2;
    for (size_t i = 1; i < nCore; i++) {
        out.lengthKm += haversineKm(out.core[2 * i - 2], out.core[2 * i - 1],
                                    out.core[2 * i], out.core[2 * i + 1]);
    }
    out.meanSpeed = (out.minT > 1.0e-9)
                  ? (out.lengthKm * 1000.0) / (out.minT * 86400.0) : 0.0;

    // T 累加自检：T_hav = Σ Δs/|u| 应与 T = Σ Δt 一致
    double tHav = traces[best].tHav;
    out.tHavCheck = (out.minT > 1.0e-9) ? std::fabs(tHav - out.minT) / out.minT : 0.0;
    if (out.tHavCheck > 0.01) {
        ncdfLog("[seamt] WARN: T mismatch T=%.4fd T_hav=%.4fd rel=%.2f%%\n",
                out.minT, tHav, out.tHavCheck * 100.0);
    }

    // 出口纬度方差（分叉 / 空间稳定性指数）
    {
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 0; i < G.size(); i++) {
            double la = traces[G[i]].latOut;
            s1 += la; s2 += la * la;
        }
        double n = (double)G.size();
        double mean = s1 / n;
        out.varLatOut = (n > 1.0) ? (s2 / n - mean * mean) : 0.0;
        if (out.varLatOut < 0.0) out.varLatOut = 0.0;
    }

    // ---------------- T(φ_out) 表 + 子射流 ----------------
    std::vector<size_t> ord = G;
    std::sort(ord.begin(), ord.end(), [&traces](size_t a, size_t b) {
        return traces[a].latOut < traces[b].latOut;
    });

    out.tCurve.reserve(ord.size() * 4);
    std::vector<int> rowOf(traces.size(), -1);
    for (size_t i = 0; i < ord.size(); i++) {
        const SeamtTrace &t = traces[ord[i]];
        rowOf[ord[i]] = (int)i;
        out.tCurve.push_back(t.latOut);
        out.tCurve.push_back(t.T);
        out.tCurve.push_back((double)t.nSteps);
        out.tCurve.push_back((double)t.reason);
    }

    // 论文判据：T 低于左右两个最近邻点即为局部极小。
    //
    // 【首尾点必须放行】论文原话要求"低于左右两个最近邻点"，但 T(φ_out) 的排序端点
    // 只有一侧邻居，按原判据永远检不出。实测（海流1.nc）主导分支恰好落在端点：
    // T=11.69d 在 φ_out=36.01°N，次优 33.5d，差 3 倍，是明确的分叉 —— 却被判为非极小。
    // 端点只要低于它唯一的邻居就算极小；只有一条流线时它本身就是极小。
    std::vector<SeamtSubjet> cand;
    for (size_t i = 0; i < ord.size(); i++) {
        const SeamtTrace &t = traces[ord[i]];
        bool hasL = (i > 0), hasR = (i + 1 < ord.size());
        double l = hasL ? traces[ord[i - 1]].T : 1.0e30;
        double r = hasR ? traces[ord[i + 1]].T : 1.0e30;
        if (!(t.T < l && t.T < r)) continue;

        // 最小显著度（论文自述判据过简、会检出噪声假支流）
        if (params.minProminence > 0.0 &&
            (std::min(l, r) - t.T) < params.minProminence) continue;

        SeamtSubjet s;
        s.lonOut = t.lonOut; s.latOut = t.latOut;
        s.TDays = t.T; s.nSteps = t.nSteps; s.row = (int)i;
        cand.push_back(s);
    }
    // 最小出口纬度间隔：按 T 升序贪心保留
    std::sort(cand.begin(), cand.end(),
              [](const SeamtSubjet &a, const SeamtSubjet &b) { return a.TDays < b.TDays; });
    out.subjets.clear();
    for (size_t i = 0; i < cand.size(); i++) {
        bool clash = false;
        for (size_t j = 0; j < out.subjets.size(); j++) {
            if (params.minSepDeg <= 0.0) break;
            if (std::fabs(out.subjets[j].latOut - cand[i].latOut) < params.minSepDeg) {
                clash = true;
                break;
            }
        }
        if (!clash) out.subjets.push_back(cand[i]);
    }
    std::sort(out.subjets.begin(), out.subjets.end(),
              [](const SeamtSubjet &a, const SeamtSubjet &b) { return a.latOut < b.latOut; });

    ncdfLog("[seamt] core: T=%.4fd L=%.1fkm ubar=%.3fm/s latIn=%.4f latOut=%.4f  "
            "tHav_rel=%.3f%%  subjets %u(raw) %u(filtered)\n",
            out.minT, out.lengthKm, out.meanSpeed, out.latIn, out.latOut,
            out.tHavCheck * 100.0,
            (unsigned)cand.size(), (unsigned)out.subjets.size());
    computeAxisFit(F, out.core, out.axisFit);

    // 子射流线坐标（可选）。逐条打包，并记每条的点数，渲染时才能逐条着色。
    if (params.wantSubjetLines) {
        out.subjetLines.clear();
        out.subjetLineLen.clear();
        for (size_t i = 0; i < out.subjets.size(); i++) {
            int row = out.subjets[i].row;
            if (row < 0 || (size_t)row >= ord.size()) continue;
            const std::vector<double> &p = traces[ord[(size_t)row]].pts;
            out.subjetLineLen.push_back((int)(p.size() / 2));
            out.subjetLines.insert(out.subjetLines.end(), p.begin(), p.end());
        }
    }

    // 集合只用来比选：核心（与可选的子射流线）取出后立刻归还容量
    std::vector<SeamtTrace>().swap(traces);

    out.ok = true;
    if (out.message.empty()) {
        out.message = wxString::Format(
            _T("T=%.2f d | L=%.0f km | 均速=%.2f m/s | G=%d/%d | 入口纬度=%.2f 出口纬度=%.2f | 子射流 %u"),
            out.minT, out.lengthKm, out.meanSpeed, out.nSuccess, out.nTotal,
            out.latIn, out.latOut, (unsigned)out.subjets.size());
    }
    out.elapsedMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    ncdfLog("[seamt] extract OK: %.1f ms\n", out.elapsedMs);
    return true;
}

// ===========================================================================
// 自检工具（docs/SEAMT… §8.1）。只读诊断，不改结果。
// ===========================================================================

// [1] 切向一致性：核心线上每点的流向角与线切向的夹角，取中位数（度）。
//     核心线**必须是流线**（SEAMT 的 min-T 是在流线集合上比的），所以这个角应当很小。
//     一旦有人对核心线做过平滑 / 样条拟合，这里会立刻露馅 —— 这是"输出确实是流线"
//     的直接证据，也是 SEAMT 区别于一切断面追踪法的本质特征。
double seamtTangentCheck(const ncdfDataMessage &msg, const std::vector<double> &core)
{
    size_t n = core.size() / 2;
    if (n < 4) return -1.0;

    UVField F;
    F.bind(msg);

    std::vector<double> dev;
    dev.reserve(n);
    for (size_t i = 1; i + 1 < n; i++) {
        double lat = core[2 * i + 1];
        double mPerDegLon = kMPerDegLat * std::cos(lat * M_PI / 180.0);

        // 线切向（局部米制）
        double ex = (core[2 * (i + 1)] - core[2 * (i - 1)]) * mPerDegLon;
        double ey = (core[2 * (i + 1) + 1] - core[2 * (i - 1) + 1]) * kMPerDegLat;

        double u = 0.0, v = 0.0;
        if (!F.sample(core[2 * i], lat, u, v)) continue;

        double l1 = std::hypot(ex, ey), l2 = std::hypot(u, v);
        if (l1 < 1.0e-9 || l2 < 1.0e-9) continue;

        double c = (ex * u + ey * v) / (l1 * l2);
        if (c > 1.0) c = 1.0;
        if (c < -1.0) c = -1.0;
        dev.push_back(std::acos(c) * 180.0 / M_PI);
    }
    if (dev.empty()) return -1.0;

    std::sort(dev.begin(), dev.end());
    return dev[dev.size() / 2];
}

// [3][4][5] 两条核心线的横向距离中位数（km）：a 上每个采样点到 b 的最近距离。
// 用于步长收敛 / 积分格式对照 / 集合收敛的量化比较。
double seamtCoreDistance(const std::vector<double> &a, const std::vector<double> &b)
{
    size_t na = a.size() / 2, nb = b.size() / 2;
    if (na < 2 || nb < 2) return -1.0;

    std::vector<double> d;
    size_t stride = na / 200;
    if (stride < 1) stride = 1;

    for (size_t i = 0; i < na; i += stride) {
        double alat = a[2 * i + 1];
        double mPerDegLon = kMPerDegLat * std::cos(alat * M_PI / 180.0);
        double best = 1.0e30;
        for (size_t j = 0; j < nb; j++) {
            double dx = (a[2 * i] - b[2 * j]) * mPerDegLon;
            double dy = (a[2 * i + 1] - b[2 * j + 1]) * kMPerDegLat;
            double s = dx * dx + dy * dy;
            if (s < best) best = s;
        }
        d.push_back(std::sqrt(best) / 1000.0);   // 米 -> 公里
    }
    if (d.empty()) return -1.0;

    std::sort(d.begin(), d.end());
    return d[d.size() / 2];
}
