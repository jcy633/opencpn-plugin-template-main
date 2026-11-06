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
#include <utility>
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

// ---------------------------------------------------------------------------
// 取速度 / 陆海判定的统一入口（2026-11-02）。
//
// 【2026-11-05 起由 SeamtParams::landMode 决定，不再是写死的开关】两个模式：
//   SEAMT_LAND_TRUNCATE     严格四角规则 + GSHHG 判陆即截断（老域默认，行为不变）
//   SEAMT_LAND_SKIP_ISLANDS sampleCoast / isSeaCoast，2×2 有一个有效角就取值，
//                           流线能穿过四角规则啃掉的窄条、绕过 1–2 格的小岛
//
// 【实测结论：容错取样不是"轴偏离"的解药，但它是"过不去小岛"的解药】
//   下游（全3.nc，2026-11-02）—— 轴没变好，故下游仍用 TRUNCATE：
//       Γ_in=130   严格 ū=0.371 offset=35.0km ratio=0.748
//                 容错 ū=0.369 offset=36.9km ratio=0.717
//       Γ_in=131   严格 ū=0.438 offset=26.4km ratio=0.819
//                 容错 ū=0.424 offset=35.0km ratio=0.570
//   上游（glorys_20210101，2026-11-05）—— 严格模式下 281 条全在吕宋北部岛链
//   (121.4E, 18.8N) 判 AGROUND，G=0；那里 GLORYS 有 1.1–2.4 m/s 的 NNW 强流，
//   只是被 1–2 格的小岛挡死。故上游用 SKIP_ISLANDS。
//
// ncdf_flags.txt 写 seamtCoastTolerant=1 是**诊断覆盖**，优先级高于 landMode，
// 做 A/B 不必改设置。每进程只读一次文件 —— 绝不在热路径做 I/O（kuroFlagInt 教训）。
// ---------------------------------------------------------------------------
// 当前这一轮 extract() 的模式。POD、只在 extract() 入口写一次：
// 所有取样/判陆都发生在一次 extract() 调用内部，不存在并发。
// 【不能用非平凡的函数内/文件作用域 static】插件加载期会执行到这里的代码
// （CLAUDE.md：那套 guard+TLS 线程安全初始化在加载期尚未就绪，会挂死）。
static volatile LONG s_landMode = SEAMT_LAND_TRUNCATE;

static bool seamtCoastTolerant()
{
    // 诊断覆盖：写了 seamtCoastTolerant 就强制开容差，与 landMode 无关
    static int cached = -1;              // -1=未读 0=无覆盖 1=强制容错
    if (cached < 0) {
        cached = 0;
        FILE *fp = fopen("C:\\ProgramData\\opencpn\\ncdf_flags.txt", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp))
                if (strstr(line, "seamtCoastTolerant")) { cached = 1; break; }
            fclose(fp);
        }
    }
    if (cached == 1) return true;
    return InterlockedCompareExchange(&s_landMode, 0, 0) == SEAMT_LAND_SKIP_ISLANDS;
}
static inline bool seamtSample(const UVField &fld, double lo, double la, double &u, double &v)
{
    return seamtCoastTolerant() ? fld.sampleCoast(lo, la, u, v) : fld.sample(lo, la, u, v);
}
static inline bool seamtIsSea(const UVField &fld, double lo, double la)
{
    return seamtCoastTolerant() ? fld.isSeaCoast(lo, la) : fld.isSea(lo, la);
}

// 【2026-11-02】真陆地 = GSHHG 掩膜判陆。与"数据缺测"区分开：
//   真陆地（掩膜说陆）→ **截断**，流线不该跨过陆地，跨过去是造出来的假路径
//   数据缺测（NaN，但掩膜说海）→ **桥接**，那是数据缺口不是地理障碍
// 原先两者混在一起：bridgeGap 只用 F.sample 找有效点、不查掩膜，于是跳过真陆地
// 也照样走，会画出穿陆地的流线。掩膜不可用时（lm==NULL）退回旧行为（都桥接）。
static inline bool seamtRealLand(const UVField &fld, double lo, double la)
{
    return fld.lm && fld.lm->covers(la, lo) && fld.lm->isLand(la, lo);
}

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
    if (!seamtSample(F, lon, lat, u, v)) return;
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
// 逐条流线诊断开关：C:\ProgramData\opencpn\ncdf_flags.txt 里写 seamtTrace=1。
// 每轮读一次（不在热路径），避免给 281 条种子各 fopen 一遍文件。
static bool seamtTraceEnabled()
{
    static int cached = -1;              // -1=未读 0=关 1=开；只读一次，改完重启才生效
    if (cached >= 0) return cached != 0;
    cached = 0;
    FILE *fp = fopen("C:\\ProgramData\\opencpn\\ncdf_flags.txt", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp))
            if (strstr(line, "seamtTrace")) { cached = 1; break; }
        fclose(fp);
    }
    return cached != 0;
}

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
    int    nBridged;           // 跨过小岛/缺测缺口的次数（2026-10-30 加）
    // 【2026-11-02 涡外缘绕行】流线在涡心处停滞时记录捕获点。
    // stalled=true 表示"走到这儿就动不了了"（连续若干步位移过小），不是正常跑满 nMax。
    // 捕获点用于就地做 Okubo-Weiss 分类、找涡外缘、生成绕行续段。
    bool   stalled;
    double stallLon, stallLat;
    double uStall, vStall;     // 捕获点流速（用来定绕行的进入方向）
};

// ---------------------------------------------------------------------------
// 小缺口桥接（2026-10-30）
//
// 【为什么需要】GLORYS 1/12° 分辨得出小岛，那些格点是 NaN。急流贴岸走，
// 一步（|u|·Δt ≈ 3 km）就可能踩进一个 NaN 格点，整条流线被判 AGROUND 丢弃 ——
// 实测 Γ_in=130°E 时 degraded=116、AGROUND=63、种子 noData=200，三处都在被
// 小岛打散，而 AVISO 是平滑地转流场、没有这个问题。
//
// 【为什么不是零填充】MATLAB 的 sub_u(isnan|mask<1)=0 只是让场连续，
// 零速会让流线停住（Δt 固定、位移 0）最终撞 nMax 判 MAXSTEP。我们要的是「跨过去」。
//
// 【语义】沿上一步的流速方向外推，逐格前进寻找最近的有效点；超过 maxBridgeKm
// 仍找不到才判 AGROUND。桥接段每前进一步按一个 Δt 计入 T，与正常积分同口径。
// 大陆/大岛（尺度 > maxBridgeKm）依然会正常终止。
// ---------------------------------------------------------------------------
static bool bridgeGap(const UVField &F,
                      double lon0, double lat0,
                      double uDir, double vDir,      // 上一步流速（m/s），定方向
                      double maxBridgeKm, double stepKm,
                      double &lon1, double &lat1, int &nSub)
{
    nSub = 0;
    lon1 = lon0; lat1 = lat0;
    double sp = std::hypot(uDir, vDir);
    if (!(sp > 1.0e-9)) return false;                 // 没有方向可循
    if (!(maxBridgeKm > 0.0)) return false;
    if (!(stepKm > 0.0)) stepKm = 1.0;
    double nx = uDir / sp, ny = vDir / sp;             // 单位方向（物理空间）
    int maxSub = (int)std::ceil(maxBridgeKm / stepKm);
    if (maxSub < 1) maxSub = 1;
    if (maxSub > 64) maxSub = 64;

    double lo = lon0, la = lat0;
    for (int k = 1; k <= maxSub; k++) {
        la += (ny * stepKm * 1000.0) / kMPerDegLat;
        double cLat = std::cos(la * M_PI / 180.0);
        if (!(std::fabs(cLat) > 1.0e-6)) return false;
        lo += (nx * stepKm * 1000.0) / (kMPerDegLat * cLat);
        double u, v;
        // 【2026-11-02】桥接只跨"数据缺测"。前方是真陆地（GSHHG 判陆）就放弃桥接 ——
        // 流线不该跨过陆地，跳过去是造出来的假路径。
        if (seamtRealLand(F, lo, la)) return false;
        if (seamtSample(F, lo, la, u, v)) {                 // 找到有效点
            lon1 = lo; lat1 = la; nSub = k;
            return true;
        }
    }
    return false;
}

// 断面几何的两个实现（见下面的 namespace 块）：traceOne 要用 crossGout，而实现
// 排在 findP0 之前（和 findP0 的几何依赖放一起），所以这里先声明。
namespace {
    void ginPoint(const SeamtParams &P, double s, double &lon, double &lat);
    void ginP0Range(const SeamtParams &P, double &sMin, double &sMax);
    void ginSeedRange(const SeamtParams &P, double &sMin, double &sMax);
    void goutDir(const SeamtParams &P, double &tx, double &ty);
    bool crossGout(const SeamtParams &P,
                   double lon, double lat, double lon1, double lat1,
                   double &xLon, double &xLat, double &f);
}

static void traceOne(const UVField &F,
                     const SeamtParams &P,
                     double lonDomMin, double lonDomMax,
                     double lon0, double lat0,
                     double dtSec, double dtDays, int nMax,
                     double maxBridgeKm, double dirSign,
                     double stallMeters, int stallSteps,
                     SeamtTrace &t)
{
    t.pts.clear();
    t.pts.reserve((size_t)nMax * 2);
    t.pts.push_back(lon0);
    t.pts.push_back(lat0);
    t.T = 0.0; t.tHav = 0.0;
    t.nSteps = 0; t.nDegraded = 0; t.nBridged = 0;
    t.reason = SEAMT_MAXSTEP;
    t.lonOut = t.latOut = nanD();
    t.stalled = false;
    t.stallLon = t.stallLat = nanD();
    t.uStall = t.vStall = 0.0;
    int nSlow = 0;             // 连续小位移步数（停滞检测）

    double lon = lon0, lat = lat0;
    double lastU = 0.0, lastV = 0.0;   // 上一步流速，供桥接定向（0 = 还没走过）

    // 小缺口桥接：沿上一步流速方向外推找最近有效点（见 bridgeGap 注释）
    auto tryBridge = [&](double fromLon, double fromLat, double &outLon, double &outLat) -> int {
        int nSub = 0;
        double sp = std::hypot(lastU, lastV);
        if (!(sp > 1.0e-9) || !(maxBridgeKm > 0.0)) return 0;
        double stepKm = sp * std::fabs(dtSec) / 1000.0;
        if (!(stepKm > 0.0)) stepKm = 1.0;
        // 桥接方向 = 积分方向（dirSign=-1 时逆着流向走）
        return bridgeGap(F, fromLon, fromLat, lastU * dirSign, lastV * dirSign,
                         maxBridgeKm, stepKm, outLon, outLat, nSub) ? nSub : 0;
    };

    for (int n = 0; n < nMax; n++) {
        {   // 记录当前点流速供桥接定向；取不到就沿用上一步的方向
            double su, sv;
            if (seamtSample(F, lon, lat, su, sv) && std::hypot(su, sv) > 1.0e-9) { lastU = su; lastV = sv; }
        }
        double lon1, lat1;
        // dirSign = -1 时按 (-u,-v) 积分，即逆着流向反推（用于从 P* 向西补出上游段）。
        // 路径点的物理量仍取真实 (u,v)，T 照常累加正的 dtDays。
        if (!rk4Advance(F, lon, lat, dtSec * dirSign, lon1, lat1, t.nDegraded)) {
            // 当前点取不到速度。**真陆地直接截断**（流线不该跨陆地）；
            // 只有"数据缺测"才桥接，跨不过才判 AGROUND。
            if (seamtRealLand(F, lon, lat)) { t.reason = SEAMT_AGROUND; break; }
            int sub = tryBridge(lon, lat, lon1, lat1);
            if (sub <= 0) { t.reason = SEAMT_AGROUND; break; }
            t.nBridged++;
            t.T += (double)sub * dtDays;
            t.pts.push_back(lon1); t.pts.push_back(lat1);
            lon = lon1; lat = lat1;
            continue;
        }

        // ---- 出口判定：本步跨越 Γ_out ----
        //
        // 【必须在域判定之前】Γ_out 就是研究域的边界，跨过它的那一步落点必然
        // 越界，会同时满足"越过 Γ_out"与"出域"两个条件。先判域的话
        // 每一条成功流线都会被记成 OUTDOMAIN，EXIT 恒为 0（2026-10-28 实测踩到）。
        //
        // 论文未写方向判定，但再循环里的西向流会被误判为"到达出口"，必须加。
        //
        // 几何本身在 crossGout() 里，经向 / 线段两套公式各写各的：
        //   经向 = 跨过经线 lonOut（支持双向，2026-10-30）；
        //   线段 = 步段与 Γ_out 线段求交，交点还必须落在该线段内
        //           （Wang 域的 E2 是 28.25–31.25°N 的一段，不是整条 130.38°E 经线）。
        double xLon = 0.0, xLat = 0.0, f = 0.0;
        if (crossGout(P, lon, lat, lon1, lat1, xLon, xLat, f)) {
            t.latOut = xLat;
            t.lonOut = xLon;
            t.pts.push_back(xLon);
            t.pts.push_back(t.latOut);

            // T = ∫dt。末步是部分步，按比例计——否则会引入最多 Δt 的假噪声，
            // 而我们正是要在 141 条之间比 T 的大小（论文的 Δt×N 是它的固定步长退化形）。
            t.T += f * dtDays;
            double dsKm = haversineKm(lon, lat, xLon, t.latOut);
            double u0, v0;
            if (seamtSample(F, lon, lat, u0, v0)) {
                double sp = std::hypot(u0, v0);
                if (sp > 1.0e-9) t.tHav += (dsKm * 1000.0) / sp / 86400.0;
            }
            t.nSteps = n + 1;
            t.reason = SEAMT_EXIT;
            return;
        }

        // 落点必须在研究域内且是海
        if (lon1 < lonDomMin || lon1 > lonDomMax ||
            lat1 < P.latDomMin || lat1 > P.latDomMax) {
            t.reason = SEAMT_OUTDOMAIN;
            break;
        }
        if (!seamtIsSea(F, lon1, lat1)) {
            // 落点无效。**真陆地直接截断**；只有"数据缺测"才桥接跨过去。
            if (seamtRealLand(F, lon1, lat1)) { t.reason = SEAMT_AGROUND; break; }
            int sub = tryBridge(lon1, lat1, lon1, lat1);
            if (sub <= 0) { t.reason = SEAMT_AGROUND; break; }
            t.nBridged++;
            t.T += (double)sub * dtDays;
            t.pts.push_back(lon1); t.pts.push_back(lat1);
            lon = lon1; lat = lat1;
            continue;
        }

        // ---- T 累加 ----
        double dsKm = haversineKm(lon, lat, lon1, lat1);
        double u0, v0;
        if (seamtSample(F, lon, lat, u0, v0)) {
            double sp = std::hypot(u0, v0);
            if (sp > 1.0e-9) t.tHav += (dsKm * 1000.0) / sp / 86400.0;
        }
        t.T += dtDays;

        lon = lon1; lat = lat1;
        t.pts.push_back(lon);
        t.pts.push_back(lat);
        t.nSteps = n + 1;

        // 【2026-11-02 停滞检测】连续 kStallSteps 步位移都小于阈值 = 在临界点处
        // 真的动不了了（螺旋汇把流线吸住），不是"还没走到"。此时就地记下捕获点，
        // 后面用它做 Okubo-Weiss 分类、找涡外缘、生成绕行续段。
        // 早停也省掉无谓的 nMax 次空转（实测 135→160 有 191 条卡在同一个点）。
        if (dsKm * 1000.0 < stallMeters) {
            if (++nSlow >= stallSteps) {
                t.stalled = true;
                t.stallLon = lon; t.stallLat = lat;
                t.uStall = u0; t.vStall = v0;
                t.reason = SEAMT_MAXSTEP;
                break;
            }
        } else {
            nSlow = 0;
        }
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
// P₀ 候选：Γ_in 上"可能是射流核"的点。
//
// 论文原文是 "the maximum velocity magnitude **of the jet flow**" —— 是**射流**的
// 最大流速，不是随便哪个强流。但"断面上哪一支是射流"并不总能靠速度判出来：
//
// 【实测反例，2026-11-02 全3.nc】Γ_in=130°E 断面上最强的东向流在 30.25°N
// （|u|=0.455），而黑潮在 31.4°N（|u|≈0.30，**更弱**）。按"模长最大且东向"挑
// 必然挑到 30.25°N 那支别的流；种子带只到 31.12°N，够不到黑潮，min-T 只能在
// 离岸弱水里挑 → 轴偏 100–500 km、ū=0.37（正确值 0.72，由分段拼接验证）。
// 132°E 更极端：挑到 28.00°N，而黑潮在 32°N，差 4 个纬度。
//
// 【为什么交给 min-T 去选】算法自己的判据就是"T 最小的贯通流线"，它天然认得射流：
// 黑潮那支 T≈22 天，30°N 那支 T≈60 天，差 3 倍。与其用启发式猜哪支是射流，
// 不如把断面上每一支"朝 Γ_out 的流速局部极大"都布上种子，让 min-T 在并集里选。
// 只有一个候选时完全退化成论文原算法（P₀ ± halfWidth 一个集合），行为不变。
//
// 【候选判据】
//   * sm[]（3 点高斯平滑后的模长）在 ±kCandMinSep/2 窗口内的极大
//   * 下游分量 > 0（射流必须流向 Γ_out 才构成贯通路径）
//   * 模长不低于主候选的 kMinRel（挡掉噪声小凸起）
// 按模长降序排；间距 < kCandMinSep 的只留最强的；最多 kMaxCand 个。
//
// 【为什么用"窗口极大"而不是"三点局部极大 + 邻居相邻"】贴岸急流常常紧挨着缺测
// 缺口（130°E 上黑潮在 31.4°N，31.75°N 起就是 NaN），三点极大没问题但"邻居必须
// 是相邻纬度"会把它当缺口假峰滤掉 —— 2026-11-02 实测就是这么漏掉黑潮的。
// 窗口极大只看"它是不是这一段里最高的"，对缺口不敏感。
//
// 返回值 = 主候选（模长最大的 u>0 点）的模长，供"Γ_in 没切到射流"告警用。
//
// 【2026-11-05】原来的多候选枚举（kCandMinRel / kCandMinSep / kCandMax，最多 16 个
// 「窗口极大」各铺一条 P₀±halfWidth 带）已随「带式撒点」一起删掉 —— 它只为
// "每个候选各铺一条带"服务。撒点改成全断面均匀后没有带子，P₀ 只用来报位置与 |u|。
// ===========================================================================
// 候选上限。【2026-11-02】4 太小：全3.nc 130°E 上黑潮在 31.4°N、模长约 0.29，
// 排在 37.17(0.77)/30.17(0.50)/29.08(0.38)/34.54(0.35) 之后第五位，被 4 的上限
// 截掉，结果一条没变。放宽到 8 后 130°E 能选中 31.45 那支。
// 【同日第二轮】8 又不够了：纬度域从 24–38 放宽到 24–41 后，断面上北边多出几支
// 局部极大，黑潮那支（31.42°N）又被挤出前 8 —— 种子里根本没有 31.3–31.6°N 的，
// min-T 于是退回 30.34°N 那条离岸路径（ū 0.434→0.368）。再放到 16。
// ===========================================================================
// 断面几何：**经向（老域）与线段（新域）两套公式各写各的**，只在下面这几个小
// 函数里分派。RK4 / 桥接 / 平滑 / 候选窗口这些与断面形状无关的部分照旧共用，
// 但凡几何判据都不写成「一条公式里塞 if」—— 免得一个应用域的修法波及另一个。
//
// 沿断面的坐标 s 的含义：
//   经向（inSec.set=false，默认）：s = 纬度        —— 老域，公式与最初版本逐位相同
//   线段（inSec.set=true）        ：s = 自端点 1 起算的弧长（度）
//
// 【向后兼容是可证的】经向下 goutDir 给 (±1,0)，于是
//   dot = u·tx+v·ty = ±u                        —— 与旧的 downStreamSign*u 同值
//   atan2(cross,dot) = atan2(∓v, ±u) = ∓atan2(v,u)
//   |·| 之后与旧的 |atan2(v,u)| **逐位相同**（atan2 对第一参数是奇函数）。
// ===========================================================================
namespace {

// 线段的弧长（度）。局部等距近似：经度差乘 cos(纬度中点)。
static double sectionArcDeg(const SeamtSection &g, double *cosMidOut = NULL)
{
    const double latMid = 0.5 * (g.lat1 + g.lat2);
    const double cosMid = std::cos(latMid * M_PI / 180.0);
    if (cosMidOut) *cosMidOut = cosMid;
    const double dx = (g.lon2 - g.lon1) * cosMid;
    const double dy = (g.lat2 - g.lat1);
    return std::sqrt(dx * dx + dy * dy);
}

// s -> (lon, lat)
void ginPoint(const SeamtParams &P, double s, double &lon, double &lat)
{
    if (!P.inSec.set) {                     // 【经向】老域，公式不动
        lon = P.lonIn;
        lat = s;
        return;
    }
    // 【线段】按弧长比例线性内插
    const SeamtSection &g = P.inSec;
    const double arc = sectionArcDeg(g);
    double t = (arc > 1.0e-12) ? (s / arc) : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    lon = g.lon1 + t * (g.lon2 - g.lon1);
    lat = g.lat1 + t * (g.lat2 - g.lat1);
}

// P₀ 搜索区间（s 空间）。经向下**必须**还是 latP0Min..latP0Max ——
// 换成整条断面会改变高斯平滑在边界处的邻域，P₀ 就变了。
void ginP0Range(const SeamtParams &P, double &sMin, double &sMax)
{
    if (!P.inSec.set) { sMin = P.latP0Min; sMax = P.latP0Max; return; }
    sMin = 0.0;
    sMax = sectionArcDeg(P.inSec);
}

// 种子铺开区间（s 空间）
void ginSeedRange(const SeamtParams &P, double &sMin, double &sMax)
{
    if (!P.inSec.set) { sMin = P.latDomMin; sMax = P.latDomMax; return; }
    sMin = 0.0;
    sMax = sectionArcDeg(P.inSec);
}

// 「朝 Γ_out」的单位向量（局部平面，经度差乘 cos 纬度）
void goutDir(const SeamtParams &P, double &tx, double &ty)
{
    if (!P.inSec.set) {                     // 【经向】老域
        tx = (P.lonOut > P.lonIn) ? 1.0 : -1.0;
        ty = 0.0;
        return;
    }
    // 【线段】Γ_in 中点 -> Γ_out 中点
    const double latMid = 0.25 * (P.inSec.lat1 + P.inSec.lat2 + P.outSec.lat1 + P.outSec.lat2);
    const double cosMid = std::cos(latMid * M_PI / 180.0);
    double dx = (0.5 * (P.outSec.lon1 + P.outSec.lon2) - 0.5 * (P.inSec.lon1 + P.inSec.lon2)) * cosMid;
    double dy = (0.5 * (P.outSec.lat1 + P.outSec.lat2) - 0.5 * (P.inSec.lat1 + P.inSec.lat2));
    const double L = std::sqrt(dx * dx + dy * dy);
    if (L < 1.0e-12) { tx = 1.0; ty = 0.0; return; }
    tx = dx / L; ty = dy / L;
}

// 本步 (lon,lat) -> (lon1,lat1) 是否跨越 Γ_out。是则给出交点与步内比例 f。
bool crossGout(const SeamtParams &P,
                      double lon, double lat, double lon1, double lat1,
                      double &xLon, double &xLat, double &f)
{
    if (!P.inSec.set) {
        // 【经向】老域：跨过经线 lonOut。公式与最初版本逐位相同。
        const bool eastward = (P.lonOut > P.lonIn);
        const bool crossed = eastward ? (lon < P.lonOut && lon1 >= P.lonOut && lon1 > lon)
                                      : (lon > P.lonOut && lon1 <= P.lonOut && lon1 < lon);
        if (!crossed) return false;
        f = (P.lonOut - lon) / (lon1 - lon);   // 西向时分子分母同号，仍在 [0,1]
        xLon = P.lonOut;
        xLat = lat + f * (lat1 - lat);
        return true;
    }
    // 【线段】步段与 Γ_out 线段求交（局部平面：经度差乘 cos 纬度）
    const double latMid = 0.25 * (lat + lat1 + P.outSec.lat1 + P.outSec.lat2);
    const double cosMid = std::cos(latMid * M_PI / 180.0);
    const double px = lon * cosMid,  py = lat;
    const double rx = (lon1 - lon) * cosMid, ry = (lat1 - lat);
    const double qx = P.outSec.lon1 * cosMid, qy = P.outSec.lat1;
    const double sx = (P.outSec.lon2 - P.outSec.lon1) * cosMid, sy = (P.outSec.lat2 - P.outSec.lat1);
    const double denom = rx * sy - ry * sx;
    if (std::fabs(denom) < 1.0e-15) return false;          // 平行
    const double qpx = qx - px, qpy = qy - py;
    f = (qpx * sy - qpy * sx) / denom;                     // 步段上的参数
    const double t = (qpx * ry - qpy * rx) / denom;        // 断面上的参数
    if (!(f > 0.0) || f > 1.0) return false;
    if (t < -1.0e-9 || t > 1.0 + 1.0e-9) return false;     // 交点不在 Γ_out 线段内
    xLon = lon + f * (lon1 - lon);
    xLat = lat + f * (lat1 - lat);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 【打结 = 原地打转，2026-11-05】起点到终点的直线距离远小于路径长 = 流线绕圈
// 出不去。实测：
//   全3.nc 的 P* 西延 738 点  path=180.1 km  net=33.6 km  winding=5.4  <- 打结
//   海流1.nc 的 P* 西延 174 点 path≈500  km  net≈291 km  winding≈1.7 <- 正常
// 阈值取 3.0，两边都有余量。比值越大越"绕"，调小更严、调大更松。
// ---------------------------------------------------------------------------
static bool polyKnotted(const std::vector<double> &xy, double *windingOut = NULL)
{
    const size_t n = xy.size() / 2;
    if (n < 2) { if (windingOut) *windingOut = -1.0; return true; }
    double path = 0.0;
    for (size_t i = 1; i < n; i++)
        path += haversineKm(xy[2 * i - 2], xy[2 * i - 1], xy[2 * i], xy[2 * i + 1]);
    const double net = haversineKm(xy[0], xy[1], xy[2 * (n - 1)], xy[2 * (n - 1) + 1]);
    if (windingOut) *windingOut = (net > 0.0) ? (path / net) : -1.0;
    if (!(path > 0.0)) return true;
    return (path > 3.0 * net);
}

static double findP0(const UVField &F, const SeamtParams &P,
                     std::vector<double> &latCand, std::vector<double> &spCand)
{
    latCand.clear();
    spCand.clear();
    double dLat = F.dLat;
    if (!(dLat > 0.0)) dLat = 0.01;
    double step = dLat * 0.5;
    double tx = 0.0, ty = 0.0;
    goutDir(P, tx, ty);                 // 经向下 = (±1, 0)
    const double angMaxDeg = P.jetAngMaxDeg;

    double sMin = 0.0, sMax = 0.0;
    ginP0Range(P, sMin, sMax);

    std::vector<double> lats, sp, ds, us, vs;
    for (double s = sMin; s <= sMax + 1.0e-9; s += step) {
        double gLon = 0.0, gLat = 0.0;
        ginPoint(P, s, gLon, gLat);
        double u, v;
        if (!seamtSample(F, gLon, gLat, u, v)) continue;
        lats.push_back(s);                  // 沿断面坐标 s（经向下就是纬度）
        sp.push_back(std::hypot(u, v));
        // 朝 Γ_out 的流速分量：两套公式各写各的
        if (!P.inSec.set) ds.push_back(u);                                       // 【经向】东向分量（=象限判据）
        else              ds.push_back(u * tx + v * ty);                          // 【线段】
        us.push_back(u);
        vs.push_back(v);
    }
    if (sp.empty()) return -1.0;

    // 3 点高斯 [0.25, 0.5, 0.25]
    std::vector<double> sm(sp.size());
    for (size_t i = 0; i < sp.size(); i++) {
        size_t im = (i > 0) ? i - 1 : i;
        size_t ip = (i + 1 < sp.size()) ? i + 1 : i;
        sm[i] = 0.25 * sp[im] + 0.5 * sp[i] + 0.25 * sp[ip];
    }

    // ---- 主候选（旧判据，保证有干净射流时行为与论文一致）----
    // 按模长从大到小挑，取第一个"大致朝 Γ_out"的点：
    // 判据 u>0 且 |atan2(v,u)| <= 45°，即 |v| <= u。
    // 【为什么不用"下游分量最大"】那会偏向"纯东向但弱"的点，而且挡不住朝北的假峰值。
    const double kJetAngMaxDeg = 45.0;
    size_t best = sp.size();
    {
        std::vector<size_t> order(sm.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return sm[a] > sm[b]; });
        int nSkipped = 0;
        for (size_t k = 0; k < order.size(); k++) {
            size_t i = order[k];
            // 「朝 Γ_out」判据：两套公式各写各的，不做统一抽象
            bool toward = false;
            double ang = 0.0;
            if (!P.inSec.set) {
                // 【经向】象限过滤：只留流向坐标轴**第一、四象限**的点（u>0），
                // 排除第二、三象限（西向）的流。2026-11-05 由「朝东 45° 内」改来
                // （实测 133°E 上两者选出同一批点，是空操作，但判据更直观）。
                ang = std::atan2(vs[i], us[i]) * 180.0 / M_PI;
                toward = (us[i] > 0.0);
            } else {
                // 【线段】朝 Γ_out 在 jetAngMaxDeg 内。
                // ★ 这是移植到上游的第一堵墙：上游黑潮在 A1 附近是东北—北向，
                //   atan2(v,u) 在 60~90°，用「朝东 45°」会把它**整条拒掉**，
                //   退到下面的「取模长最大」兜底，P₀ 必然挑错。
                const double dot   = us[i] * tx + vs[i] * ty;
                const double cross = us[i] * (-ty) + vs[i] * tx;
                ang = std::atan2(cross, dot) * 180.0 / M_PI;
                toward = (dot > 0.0 && std::fabs(ang) <= P.jetAngMaxDeg);
            }
            if (toward) { best = i; break; }
            nSkipped++;
            if (nSkipped <= 5)
                ncdfLog("[seamt] P0: skip s=%.3f |u|=%.3f ang=%.1f deg (not toward gamma_out)\n",
                        lats[i], sp[i], ang);
        }
        if (nSkipped > 0)
            ncdfLog("[seamt] P0: skipped %d larger-|u| points for wrong direction\n", nSkipped);
    }
    if (best == sp.size()) {                    // 退回：整条断面没有一个东向点
        best = 0;
        for (size_t i = 1; i < sm.size(); i++) if (sm[i] > sm[best]) best = i;
        ncdfLog("[seamt] P0: no eastward point on gamma_in, falling back to max |u|\n");
    }
    const double spPrimary = sp[best];

    // 【2026-11-05】原来的「其余候选」枚举（±kCandMinSep 窗口极大、最多 kCandMax 个）
    // 已删 —— 它只为「每个 P₀ 候选各铺一条 ±halfWidth 带」服务。撒点改成全断面均匀后
    // 一条带都没有了，P₀ 只用来报位置与 |u|（以及「Γ_in 没切到射流」那条告警）。
    latCand.clear(); spCand.clear();
    latCand.push_back(lats[best]);
    spCand.push_back(sm[best]);
    ncdfLog("[seamt] P0: s=%.4f |u|=%.3f (single candidate; bands removed)\n",
            lats[best], sp[best]);
    return spPrimary;
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
            if (!seamtSample(F, lo, la, u, v)) pos += snprintf(buf + pos, sizeof(buf) - pos, "  --");
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
        if (!seamtSample(F, lon, lat, u0, v0)) continue;
        double spCore = std::hypot(u0, v0);

        double spMax = -1.0, offKm = 0.0, rLon = lon, rLat = lat;
        for (int k = 0; k < nSamp; k++) {
            double dKm = -halfKm + (double)k * stepKm;
            // dKm 是公里，px/py 是单位向量 -> 先换成米再除以米每度
            double qlon = lon + (px * dKm * 1000.0) / mPerDegLon;
            double qlat = lat + (py * dKm * 1000.0) / kMPerDegLat;
            double uq, vq;
            if (!seamtSample(F, qlon, qlat, uq, vq)) continue;
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
// 速度场雅可比（米制，中心差分 2 个格距）。d(u,v)/d(x,y)，x 东 y 北，单位 1/s。
// ===========================================================================
static bool uvJacobian(const UVField &F, double lon0, double lat0,
                       double &dudx, double &dudy, double &dvdx, double &dvdy)
{
    const double hDeg = 2.0 * std::max(F.dLat, F.dLon);
    if (!(hDeg > 0.0)) return false;
    const double cLat = std::cos(lat0 * M_PI / 180.0);
    if (!(std::fabs(cLat) > 1.0e-6)) return false;
    const double hy = hDeg * kMPerDegLat;
    const double hx = hDeg * kMPerDegLat * cLat;

    double uE, vE, uW, vW, uN, vN, uS, vS;
    if (!seamtSample(F, lon0 + hDeg, lat0, uE, vE)) return false;
    if (!seamtSample(F, lon0 - hDeg, lat0, uW, vW)) return false;
    if (!seamtSample(F, lon0, lat0 + hDeg, uN, vN)) return false;
    if (!seamtSample(F, lon0, lat0 - hDeg, uS, vS)) return false;

    dudx = (uE - uW) / (2.0 * hx);  dudy = (uN - uS) / (2.0 * hy);
    dvdx = (vE - vW) / (2.0 * hx);  dvdy = (vN - vS) / (2.0 * hy);
    return true;
}

// Okubo-Weiss 参数 W = S² − ω²（1/s²）。
//   W < 0  涡度主导 → 涡核
//   W > 0  应变主导 → 外围（射流、变形区）
//   W = 0  → 涡边界
// 比"速度阈值"客观：不依赖主观阈值，物理上是涡核/外围的分界，且与临界点分类
// 共用同一套二阶量。全3.nc (153.0°E, 36.3°N) 实测 W = −2.2e-10 < 0，确系涡核。
static bool okuboWeiss(const UVField &F, double lon0, double lat0,
                       double &W, double &speed)
{
    double a, b, c, d;
    if (!uvJacobian(F, lon0, lat0, a, b, c, d)) return false;
    const double Sn = a - d, Ss = c + b, om = c - b;
    W = Sn * Sn + Ss * Ss - om * om;
    double u, v;
    if (!seamtSample(F, lon0, lat0, u, v)) { speed = 0.0; return true; }
    speed = std::hypot(u, v);
    return true;
}

// 涡外缘。**不是 W=0 等值线本身**，而是「越过 W=0 之后，|u| 的径向极大」那一圈。
//
// 【2026-11-02 实测教训】直接拿 W=0 当绕行路径会失败：全3.nc (153.0°E, 36.3°N)
// 的 W=0 半径 37 km，可那圈上平均速度只有 0.35 m/s（穿的正是涡内的慢水），
// 弧段 T_arc 高达 4.5 天，绕行流线在 min-T 里必输。而真正绕涡的那条流线在 **88 km**
// （2.4 倍半径）——`152.51/36.12 → 153.19/35.47`，落在射流上。
// W=0 是「涡度主导 / 应变主导」的动力学分界，**不是绕行路径**；绕行走的是速度环。
//
// 所以：Okubo-Weiss 只用来**确认这是涡心**（W<0）与给出涡核尺度 rW；
// 边界点取 r ≥ rW 上 |u| 的径向极大。取不到极大（单调衰减）就退回 rW 处。
struct VortexEdge {
    double lonC, latC;
    std::vector<double> angDeg;      // 方位角，0=东 90=北
    std::vector<double> lon, lat, u, v, sp;
    std::vector<double> rKm;         // 边界半径
    double rCoreKm;                  // W=0 处的平均半径（涡核尺度）
    size_t nearestIndex(double lo, double la) const {
        size_t best = 0; double bd = 1.0e30;
        for (size_t i = 0; i < lon.size(); i++) {
            double dd = (lon[i] - lo) * (lon[i] - lo) + (lat[i] - la) * (lat[i] - la);
            if (dd < bd) { bd = dd; best = i; }
        }
        return best;
    }
};

static bool findVortexEdge(const UVField &F, double lonC, double latC,
                           double maxKm, int nDir, VortexEdge &out)
{
    out.lonC = lonC; out.latC = latC;
    out.angDeg.clear(); out.lon.clear(); out.lat.clear();
    out.u.clear(); out.v.clear(); out.sp.clear(); out.rKm.clear();
    out.rCoreKm = 0.0;

    double Wc, sc;
    if (!okuboWeiss(F, lonC, latC, Wc, sc)) return false;
    if (!(Wc < 0.0)) return false;                       // 不是涡核

    const double cLat = std::cos(latC * M_PI / 180.0);
    if (!(std::fabs(cLat) > 1.0e-6)) return false;
    const double mPerDegLon = kMPerDegLat * cLat;
    const double gridKm = std::max(F.dLat, F.dLon) * kMPerDegLat / 1000.0;
    const double dStepKm = std::max(0.5, 0.5 * gridKm);  // 半格步进
    const int maxSub = (int)std::ceil(maxKm / dStepKm);
    double rCoreSum = 0.0; int nCore = 0;
    int nTruncated = 0;      // |u| 一路涨到搜索上限的方位数（没有径向极大）

    for (int k = 0; k < nDir; k++) {
        const double ang = 360.0 * (double)k / (double)nDir;
        const double a = ang * M_PI / 180.0;
        const double nx = std::cos(a), ny = std::sin(a);
        double rW = -1.0;            // W=0 处半径（涡核尺度）
        double bestR = -1.0, bestSp = -1.0, bestU = 0.0, bestV = 0.0,
               bestLon = 0.0, bestLat = 0.0;
        int bestS = -1;
        for (int s = 1; s <= maxSub; s++) {
            const double dKm = (double)s * dStepKm;
            const double lo = lonC + (nx * dKm * 1000.0) / mPerDegLon;
            const double la = latC + (ny * dKm * 1000.0) / kMPerDegLat;
            double W, sp;
            if (!okuboWeiss(F, lo, la, W, sp)) break;
            if (rW < 0.0) {
                if (W < 0.0) continue;      // 还在涡核里
                rW = dKm;                   // 第一次越过 W=0
                rCoreSum += dKm; nCore++;
            }
            double u, v;
            if (!seamtSample(F, lo, la, u, v)) break;
            // 只在涡核之外找 |u| 径向极大
            if (sp > bestSp) {
                bestSp = sp; bestR = dKm; bestS = s;
                bestU = u; bestV = v; bestLon = lo; bestLat = la;
            }
        }
        if (rW < 0.0) continue;             // 这条射线没穿出涡核（数据缺测等）
        // 【★ 径向极大必须落在搜索范围**之内**】bestS == maxSub 说明 |u| 一路涨到
        // wrapMaxKm 还没回头 —— 那不是"速度环"，是搜索上限。实测全3.nc 153°E 那个涡
        // 的速度环报 r=51..152.9 km，而 dStepKm=4.64、maxSub=33 → 152.9 km **正好是上限**：
        // 半圈的"环点"其实是 153 km 处的截断，画出来的弧是假的。这种方位整条丢掉。
        if (bestS >= maxSub) {
            nTruncated++;
            continue;
        }
        if (bestR < 0.0) {                  // 退化：用 W=0 那一点
            bestR = rW;
            bestLon = lonC + (nx * rW * 1000.0) / mPerDegLon;
            bestLat = latC + (ny * rW * 1000.0) / kMPerDegLat;
            if (!seamtSample(F, bestLon, bestLat, bestU, bestV)) continue;
            bestSp = std::hypot(bestU, bestV);
        }
        out.angDeg.push_back(ang);
        out.lon.push_back(bestLon); out.lat.push_back(bestLat);
        out.u.push_back(bestU); out.v.push_back(bestV); out.sp.push_back(bestSp);
        out.rKm.push_back(bestR);
    }
    if (nTruncated > 0)
        ncdfLog("[seamt][wrap] findVortexEdge: %d/%d rays had NO radial max within "
                "%.0f km (|u| still rising) — dropped, they are search-limit artefacts\n",
                nTruncated, nDir, maxKm);
    if (nCore > 0) out.rCoreKm = rCoreSum / (double)nCore;
    return out.lon.size() >= 4;
}

// ===========================================================================
// 段2（跨涡）路径的 T = ∫ ds/|u|
//
// 【2026-11-05 修订·优化③】原来段2 的 T 用「出口点的 |u| 代表整段」，而跨涡那段
// 往往是慢水，用出口速度代表会把 T₂ 低估一半以上，绕行流线于是凭空占优。
// 现在沿折线**逐段取两端速度平均**积分 —— 与 traceOne 的 tHav 同一口径。
// 这样段2 与段3 的 T 才是同一把尺子，min-T 比的 T₁+T₂+T₃ 才有意义。
// ===========================================================================
static double polylineTravelDays(const UVField &F, const std::vector<double> &xy,
                                 double dtSec, double dtDays, size_t nPtsLimit = (size_t)-1)
{
    if (xy.size() < 4 || !(dtSec > 0.0) || !(dtDays > 0.0)) return 0.0;
    size_t nUse = xy.size() / 2;
    if (nPtsLimit < nUse) nUse = nPtsLimit;
    double nEq = 0.0;
    for (size_t p = 0; p + 3 < 2 * nUse; p += 2) {
        const double dsM = haversineKm(xy[p], xy[p + 1], xy[p + 2], xy[p + 3]) * 1000.0;
        double sp = 0.0;
        int n = 0;
        double u, v;
        if (seamtSample(F, xy[p],     xy[p + 1], u, v)) { sp += std::hypot(u, v); n++; }
        if (seamtSample(F, xy[p + 2], xy[p + 3], u, v)) { sp += std::hypot(u, v); n++; }
        if (n == 0) continue;
        sp /= (double)n;
        if (sp > 1.0e-9 && dtSec > 0.0) nEq += dsM / (sp * dtSec);
    }
    return nEq * dtDays;
}

// 沿速度环从 iIn 绕到 iOut 的**min-T 弧**（优化①）：两个方向各积一次 T，取小的。
//
// 【与旧 seamtWrapArc=1 的区别】旧实现按「弧的切向与流向的对齐度」选方向，是启发式；
// 现在两个方向都算 T = ∫ds/|u|，用同一把尺子（min-T）选 —— 与整个 SEAMT 的
// 判据一致。旧标志 seamtWrapArc 已废（路径选择交给下面的 min-T 比较）。
static void ringArcMinT(const VortexEdge &edge, const UVField &F, size_t iIn, size_t iOut,
                        double dtSec, double dtDays,
                        std::vector<double> &arcOut, double &tOut)
{
    const size_t nE = edge.lon.size();
    arcOut.clear();
    tOut = 1.0e30;
    if (nE < 2 || iIn >= nE || iOut >= nE || iIn == iOut) return;
    for (int d = 0; d < 2; d++) {
        std::vector<double> arc;
        for (size_t s = 0; s <= nE; s++) {
            const size_t i = (d == 0) ? (iIn + s) % nE : (iIn + nE - s) % nE;
            arc.push_back(edge.lon[i]);
            arc.push_back(edge.lat[i]);
            if (s > 0 && i == iOut) break;
        }
        if (arc.size() < 4) continue;
        const double t = polylineTravelDays(F, arc, dtSec, dtDays);
        if (t < tOut) { tOut = t; arcOut.swap(arc); }
    }
}

// ===========================================================================
// 临界点分类（HEPTA 式：Udalov & Uleysky 2026；Udalov 2026 §2.3）
//
// 在流线停住的位置就地做线性化，看雅可比矩阵 J = d(u,v)/d(x,y)（**米制**）：
//   det < 0                    → 双曲点（鞍点）：最大变形区，不稳定流形**有两个分支**
//   det > 0, disc < 0, tr≈0    → 椭圆点（中心）：涡心
//   det > 0, disc < 0, tr < 0  → 螺旋汇：涡心 + 净辐合，流线螺旋卷入出不来
//   det > 0, disc < 0, tr > 0  → 螺旋源：涡心 + 净辐散
//   det > 0, disc > 0          → 节点（tr<0 汇 / tr>0 源）
//
// 【实测，全3.nc (153.0°E, 36.3°N)，2026-11-02】是**螺旋汇**不是鞍点：
//   tr = -6.07e-6   det = +6.47e-11   disc < 0
//   λ = -3.03e-6 ± 7.43e-6 j   散度 -6.07e-6（辐合）  涡度 +2.00e-5（气旋）
//   → 带净辐合的气旋涡心，流线螺旋卷入出不来，135→142 的贴脊种子 33.4361°N 死在这。
//   **没有不稳定流形可循**（两个特征值实部都为负）。
//
// 鞍点那条分支（det<0）会给出不稳定方向（度，0=东 90=北），供"沿不稳定流形续积分"用。
// ===========================================================================
static const char *kCritName[7] = {
    "saddle(hyperbolic)", "centre(elliptic)", "spiral-sink(vortex+convergent)",
    "spiral-source(vortex+divergent)", "node-sink", "node-source", "degenerate"
};

static void classifyCriticalPoint(const UVField &F, double lon0, double lat0,
                                  int nCaptured, std::vector<double> &out)
{
    double dudx, dudy, dvdx, dvdy;
    if (!uvJacobian(F, lon0, lat0, dudx, dudy, dvdx, dvdy)) return;
    const double tr   = dudx + dvdy;
    const double det  = dudx * dvdy - dudy * dvdx;
    const double disc = tr * tr - 4.0 * det;
    const double vort = dvdx - dudy;                       // 涡度，气旋式为正

    int type = 6;
    if (det < 0.0)                          type = 0;
    else if (disc < 0.0) {
        double scale = std::sqrt(std::fabs(disc));
        if (std::fabs(tr) <= 0.05 * scale)  type = 1;
        else if (tr < 0.0)                  type = 2;
        else                                type = 3;
    } else {
        type = (tr < 0.0) ? 4 : 5;
    }

    // 特征值。鞍点取正根（= 不稳定方向的增长率）；焦点取实部 + 虚部。
    double lRe, lIm = 0.0;
    if (disc < 0.0) {
        lRe = 0.5 * tr;
        lIm = 0.5 * std::sqrt(-disc);
    } else {
        double s = std::sqrt(disc);
        lRe = 0.5 * (tr + (det < 0.0 ? s : std::max(s, -s)));   // 鞍点时即正根
        if (det >= 0.0) lRe = 0.5 * tr;                          // 节点只报实部中点
    }

    // 不稳定方向（仅鞍点有）。J·(x,y)=λ(x,y) → 取 (b, λ-a)，退化时取 (λ-d, c)
    double unAng = nanD();
    if (type == 0) {
        double lam = 0.5 * (tr + std::sqrt(disc));               // 正根
        double ex = dudy, ey = lam - dudx;
        if (std::fabs(ex) + std::fabs(ey) < 1.0e-30) { ex = lam - dvdy; ey = dvdx; }
        if (std::fabs(ex) + std::fabs(ey) > 1.0e-30)
            unAng = std::atan2(ey, ex) * 180.0 / M_PI;
    }

    ncdfLog("[seamt] critical point (%.4f, %.4f): %s  captured=%d\n",
            lon0, lat0, kCritName[type], nCaptured);
    ncdfLog("[seamt]     J=[[%+.3e,%+.3e],[%+.3e,%+.3e]] 1/s  det=%+.3e tr=%+.3e disc=%+.3e\n",
            dudx, dudy, dvdx, dvdy, det, tr, disc);
    ncdfLog("[seamt]     eigenvalue %+.3e %+.3e j   div=%+.3e  vort=%+.3e\n",
            lRe, lIm, tr, vort);
    if (!isNanD(unAng))
        ncdfLog("[seamt]     unstable direction = %.1f deg (0=E, 90=N) — "
                "follow BOTH branches; min-T picks between the resulting channels\n",
                unAng);
    if (type == 2)
        ncdfLog("[seamt]     spiral-sink: streamlines spiral in and never leave. "
                "There is NO unstable manifold; they are vortex water, not jet water.\n");

    out.push_back(lon0); out.push_back(lat0);
    out.push_back((double)type);
    out.push_back(lRe); out.push_back(lIm);
    out.push_back(det); out.push_back(tr); out.push_back(disc);
    out.push_back(tr); out.push_back(vort);
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
    latP0Forced   = nanD();  // NaN = 自动找 P₀（findP0）
    splitLon      = nanD();  // NaN = 不分段
    // 研究域经度外扩量。Γ_in 是域内的切割断面，不是墙（见头文件注释）。
    // 1.5° ≈ 150 km：够断面附近带向西分量的流线转过弯，又不至于让流线跑远绕路。
    lonPadDeg     = 1.5;
    dtDays        = 0.03;    // 论文原始参数（固定步长）。<=0 才走 CFL 自动
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
    wantSubjetLines = true;   // 保留子射流线坐标，供渲染时按次级射流分色着色（2026-11-02）
    // 断面几何默认**经向**（inSec/outSec 的 set 默认 false），方向容差 45°
    // = 旧的「朝东 45° 内」判据。这两个默认就是老域的全部行为，不要动。
    jetAngMaxDeg  = 45.0;
    // 陆地容差默认**跳岛容差**（2026-11-05 用户定：SEAMT 一律越过小岛）。
    // 见 include/ncdf_seamt.h 的 SeamtLandMode。UI 上的调节已删，
    // 要换模式走请求文件 landmode=0|1 或改这里。
    landMode      = SEAMT_LAND_SKIP_ISLANDS;
    // 默认走完整的 min-T 集合选优。只有分段拼接的左段会把它打开（单条流线）。
    singleSeed    = false;
    // 分段拼接左段同样跳岛
    leftLandMode  = SEAMT_LAND_SKIP_ISLANDS;
}

void SeamtParams::presetJapanSouth(SeamtParams &p)
{
    // 黑潮下游·日本以南。Γ_out 放在脱离点附近（房总半岛以东），
    // T(φ_out) 的极小值正好对应 Kawabe 的近岸 / 离岸 / 大弯曲三种路径态。
    //
    // 【与论文的差异，是数据适配】论文用 AVISO L4（1/8°、平滑过），
    // Γ_in = 130°E 时黑潮干净地穿过、且在 28–31°N 内。本插件用 1/12° 模式数据，
    // 岛屿被分辨出来，130°E 正好从土噶喇列岛中间穿过。
    //
    // 【2026-10-30 定量选点】入口断面被岛屿堵死是"轴跑偏"的主因：种子在 Γ_in 上
    // 取不到速度就直接判 AGROUND 丢弃，急流核心那条流线根本不进 G 集合，min-T
    // 只能在剩下的里挑 → 轴拐进慢水。实测各断面（28–33°N 带 / 最长无效缺口）：
    //     130.0E : 88/101   gap 0.25°     G=133/281（86 出域 + 62 触底）
    //     131.0E : 52/101   gap 1.85°     G=244/281（37 触底）  ← 原值，最差之一
    //     132.0E : 101/101  gap 0.00°     G=273/281  P0=27.9N（!不是黑潮核）
    //     133.0E : 101/101  gap 0.00°     G=246/281  P0=32.4N  ubar=0.80
    //     135.0E : 101/101  gap 0.00°     G=175/281  P0=33.0N  ubar=0.87
    //     136.0E : 101/101  gap 0.00°     G=200/281  P0=33.3N  ubar=0.87
    //
    // 【2026-10-30 第二轮：光"无缺口"还不够】132°E 虽然断面最干净，但那里
    // 的断面最大流速在 28°N（是别的流，不是黑潮核），findP0 于是把 P0 定在
    // 27.92，而种子只布在 P0±halfWidthDeg=±0.70°（27.2–28.6°N）——
    // **够不到 32–33°N 的沿岸急流**，min-T 只能在种子带边缘挑（lat_in=28.6164
    // 正好是 27.9164+0.70）。所以选点必须同时看「断面无缺口」和「急流核在
    // 断面上的哪一纬度」。135/136°E 两条件都满足（急流核 33.0–33.3°N、无缺口）。
    //
    // 取 135.0°E：与论文的 130°E 最近，L=3227 km 最接近论文的 3314，
    // ubar 在两个域上都最好（Γ_out=142 时 1.107，与论文 1.10 一致）。
    // P₀ 搜索带仍放宽到整个研究域纬度范围。
    // 【2026-11-02 用户定：Γ_in = 135.0°E】135°E 以西（130–133°E 的土噶喇—大隅—
    // 屋久岛链 + 南海侧弱流）黑潮只有 0.4–0.6 m/s、弥散不成射流，那条流线反推不回
    // 130°E（见 docs/SEAMT…md §18.5 的三条证据）；从 135°E 起算轴才贴岸。
    // 135°E 的断面质量也最好（见下），ubar 与论文 1.10 一致。
    p.lonIn  = 130.0;   // 2026-11-02 用户定：起始位置改回 130（论文取值）
    p.lonOut = 142.0;
    // 域纬度必须给出口留足余量。实测（2026-10-29，海流1.nc）：核心在
    // (141.96°E, 35.999°N) 就被北墙 latDomMax=36.0 剪断 —— 离 142°E 只差 0.04°。
    // 粗步长下末步 5 km 恰好跨过墙角判成 EXIT（侥幸），细步长下规规矩矩撞墙判
    // OUTDOMAIN，两个步长给出完全不同的答案。
    //
    // 【2026-11-02 38→41】起始位置改回 130°E 后，130→142 实测 OUTDOMAIN=667/1907
    // （35%）：北墙 lat>38 剪掉 425 条、南墙 lat<24 剪掉 242 条，min-T 只能在剩下的
    // 里挑、轴偏南（817 条 EXIT 的 latOut 全是 34.218，一条到 142E/36.17 的都没有）。
    // 24–38 这个宽度是给 135→142 定的，130 起算时域不够，故与 PaperDomain 一致取 41。
    p.latDomMin = 24.0;  p.latDomMax = 41.0;
    p.latP0Min  = p.latDomMin;
    p.latP0Max  = p.latDomMax;
    // 【2026-11-02 默认分段 133°E】单段 130→142 追不到黑潮核：实测 130→133 的出口
    // 只落在 31.8–32.3N 与 36.3–38.7N 两带，**32.4–36.2N 一条都没有**，而黑潮核在
    // 32.58N 正好落在空白里（差 30 km）。130–133°E 之间流线被分成两族，核夹在中间，
    // 从 130°E 的种子出发够不着。分段拼接（右段 133→142 定 P*，左段用 P* 反推补
    // 130→133）给出 lat_out=36.174（黑潮真位置）、ū=0.722（单段只有 0.434）。
    // 分段点取 133°E：黑潮在那儿刚过岛链、核清晰（见 §4 的断面表）。
    p.splitLon = 133.0;
}

void SeamtParams::presetPaperDomain(SeamtParams &p)
{
    // Udalov 2026 原域：覆盖黑潮延伸体，可做分叉 / 出口纬度方差 / 北移趋势。
    // Γ_in 与 P₀ 带的取值同上，理由见 presetJapanSouth 的注释
    // （2026-10-30：Γ_in 131→135，两轮适配：先避开岛屿带，再让 findP0
    //   落在沿岸急流核上，见 presetJapanSouth 的注释）。
    p.lonIn  = 130.0;   // 2026-11-02 用户定：起始位置改回 130（论文取值）
    p.lonOut = 160.0;
    // 【必须显式设】SeamtParams() 构造函数先调 presetJapanSouth（它设了 splitLon=133），
    // presetPaperDomain 不覆盖的话就带着 133 跑，会静默走分段路径。
    p.splitLon = 133.0;
    p.latDomMin = 27.0;  p.latDomMax = 41.0;
    p.latP0Min  = p.latDomMin;
    p.latP0Max  = p.latDomMax;
}

// ===========================================================================
// 上游域（Wang2026 域：吕宋海峡—台湾以东—东海）
//
// 【与上面两个预设的关系：完全独立】SEAMT 用在两个地方，设置必然不同。
// 这里**逐项显式赋值**，不继承 SeamtParams() 的公共默认，也不去改上面两个的取值；
// 反之 presetJapanSouth / presetPaperDomain 也不会因为本函数而变。两域参数互不套用。
//
// Γ_in / Γ_out 的取法：
//   Γ_out = Wang 算法的终止断面 E.north (E2)  (130.38, 28.25) -> (130.38, 31.25)
//   Γ_in  = Wang **发表轴真正的起点**所在的纬度 16.5°N 纬向断面，122→124°E
//           —— 不是 A.south 的 16.00°N（那一条量到的是西向 NEC，见函数内实测注释）。
// Wang 发表轴首尾（kuroshio_2021.nc day001）：16.4954N,122.3101E → 30.9751N,129.9221E。
//
// 【为什么要开线段断面】A1 是纬向的，而老域的 Γ_in 只能是经线；
// 【为什么要改方向判据】老判据是「朝东 45° 内」，上游黑潮在 A1 附近是东北—北向，
//   会被整条拒掉（详见 findP0 的注释）。
// ===========================================================================
void SeamtParams::presetWangUpstream(SeamtParams &p)
{
    // ---- 断面几何 ----
    // Γ_in 取 **Wang 发表轴真正的起点所在的纬度**，不是 A.south 的 16.00°N。
    // 2026-11-05 实测 kuroshio_2021/2022 共 80 天的轴起点：
    //   纬度 16.1725..16.7959（中位 16.5049）  经度 122.310..123.833
    // 也就是轴的南端齐刷刷落在 ~16.5°N 上，经度随日子在 122.3–123.8°E 之间挪。
    // 而 16.00°N 那一条（A1 原值）量到的全是**西向 NEC**：
    //   16.00N/122.0–124.0E  u = -0.00 .. -0.25 m/s（向西）
    //   16.50N/122.20E       u = +0.104 v = +0.275（东北，= 轴起点处的流）
    // 种子撒在 16.00°N 上只会顺着 NEC 漂向西南，永远到不了 Γ_out。
    // 故 Γ_in = 16.5°N 的纬向断面，经度取 122–124 覆盖所有观测到的起点经度。
    p.inSec.lon1 = 122.00;  p.inSec.lat1 = 16.50;
    p.inSec.lon2 = 124.00;  p.inSec.lat2 = 16.50;
    p.inSec.set  = true;
    p.outSec.lon1 = 130.38;  p.outSec.lat1 = 28.25;
    p.outSec.lon2 = 130.38;  p.outSec.lat2 = 31.25;
    p.outSec.set  = true;                      // E.north，纬度窗严格生效
    // 这两个在**线段**断面下只是日志/兼容字段，不参与几何
    p.lonIn  = 122.00;
    p.lonOut = 130.38;
    // A1 处流向与 A1→E2 的总方向本来就有夹角，45° 会偏紧
    p.jetAngMaxDeg = 60.0;

    // ---- 研究域 ----
    // Wang 域 15–32°N，上下各留 1° 余量（同下游的做法：域必须给出口留足余量）
    p.latDomMin = 14.0;  p.latDomMax = 33.0;
    p.latP0Min  = p.latDomMin;
    p.latP0Max  = p.latDomMax;
    p.latP0Forced = nanD();      // NaN = 自动找 P₀
    p.lonPadDeg = 1.5;
    p.splitLon  = nanD();        // 上游不用下游那套 133°E 分段拼接，单段跑

    // ---- 集合与积分 ----
    // 本轮不调参：先沿用论文量级。真要调也只改这里，绝不碰下游两个预设。
    p.nPts         = 281;
    p.halfWidthDeg = 0.70;
    p.dtDays       = 0.03;
    p.nMaxSteps    = 0;          // auto
    p.cflTarget    = 0.50;

    // ---- 子射流 / 诊断 ----
    p.minSepDeg        = 0.05;
    p.minProminence    = 0.10;
    p.wantSubjetLines  = true;

    // ---- 陆地容差 ----
    // 上游实测：严格模式下 281 条流线全在吕宋北部岛链 (121.4E, 18.8N) 判 AGROUND、
    // G=0 —— 那里 GLORYS 有 1.1–2.4 m/s 的 NNW 强流，只是被 1–2 格的小岛挡死。
    // 跳岛容差让流线能从四角规则啃掉的窄条里穿过去。（2026-11-05 起 SEAMT 全域默认。）
    p.landMode         = SEAMT_LAND_SKIP_ISLANDS;
    p.leftLandMode     = SEAMT_LAND_SKIP_ISLANDS;
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
    if (lonPadDeg < 0.0) lonPadDeg = 0.0;      // 0 = 旧行为，域贴死 [lonIn, lonOut]

    // ---- 线段断面（新域专用）----
    // set=false（默认）时下面这些完全不参与，老域（日本以南 / 延伸体）走经向实现、
    // 行为逐位不变。两域的设置互不套用。
    if (!(jetAngMaxDeg > 0.0) || jetAngMaxDeg > 90.0) jetAngMaxDeg = 45.0;
    if (landMode != SEAMT_LAND_SKIP_ISLANDS) landMode = SEAMT_LAND_TRUNCATE;
    if (leftLandMode != SEAMT_LAND_SKIP_ISLANDS) leftLandMode = SEAMT_LAND_TRUNCATE;

    if (inSec.set != outSec.set) {
        // 只给一条会得到"线段进、经线出"的混搭几何，直接退回经向
        ncdfLog("[seamt] WARN: inSec/outSec must be set together -> meridional sections\n");
        inSec.set = outSec.set = false;
    }
    if (inSec.set) {
        const double dIn  = std::fabs(inSec.lon1 - inSec.lon2) + std::fabs(inSec.lat1 - inSec.lat2);
        const double dOut = std::fabs(outSec.lon1 - outSec.lon2) + std::fabs(outSec.lat1 - outSec.lat2);
        if (dIn < 1.0e-9 || dOut < 1.0e-9) {
            ncdfLog("[seamt] WARN: degenerate section segment -> meridional sections\n");
            inSec.set = outSec.set = false;
        }
    }
    if (inSec.set && !isNanD(splitLon)) {
        // 分段拼接的 runSeg() 只会按 lonIn/lonOut 造经线子段，不认识线段断面。
        // 这个组合本轮不支持 —— 显式关掉并告警，绝不静默走错分支。
        ncdfLog("[seamt] WARN: splitLon is unsupported with segment sections -> splitLon cleared\n");
        splitLon = nanD();
    }
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
    critPoints.clear();
    vortexCentres.clear();
    vortexCircles.clear();
    vortexCircleLen.clear();
    subjetSummary.clear();
    tCurve.clear();
    traceTable.clear();
    subjets.clear();
    minT = meanSpeed = lengthKm = 0.0;
    latIn = latOut = varLatOut = 0.0;
    tHavCheck = 0.0;
    seamSmoothIdx = -1;
    dtDays = uMaxEst = dxMinKm = cfl = 0.0;
    nSuccess = nTotal = nDegraded = nNearMiss = nBridged = nWrapped = 0;
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
    // 陆地容差模式取本轮设置（所有取样/判陆都在这一次 extract() 内发生）。
    // POD 写入，不用非平凡 static（CLAUDE.md：插件加载期会执行到的代码里不要用）。
    InterlockedExchange(&s_landMode, (LONG)params.landMode);
    ncdfLog("[seamt] land mode: %s\n",
            (params.landMode == SEAMT_LAND_SKIP_ISLANDS)
                ? "SKIP_ISLANDS (sampleCoast, 绕过 1-2 格小岛)"
                : "TRUNCATE (四角规则 + GSHHG 判陆即截断)");

    // ================= 分段拼接（2026-10-30）=================
    // splitLon 有效时把 [lonIn, lonOut] 拆成两段，**共用右段轴的起始点 P\***：
    //   右段 splitLon → lonOut：正常流程自动找 P₀，得轴；P* = 该轴在 splitLon 上的起点
    //   左段 lonIn → splitLon：以 P*（或其集合里能到达 lonIn 者）为起点，**逆流向反推**
    //
    // 【为什么共用 P* 而不是共用 P₀】P₀ 是断面上"东向最强"的点，而 P* 是
    // 右段最终选中的那条流线的起点，两者不等（实测 135°E 上 P₀=33.04、P*=33.21）。
    // 两段要无缝对接，公共点必须是 P*。
    //
    // 【P* 不行就用它的集合】P* 若反推不到 lonIn（被岛屿/弱流挡住），
    // 就在 P* ± halfWidth 的集合里找能到达的最近点 P'，取 **(P*+P')/2** 作公共点。
    //
    // 【★ 流速方向要翻转】左段是逆流向反推（dirSign=-1，用 (-u,-v) 积分），
    // 因为黑潮向东流，不取反根本走不到 lonIn。反推段的**点顺序要反转**后再接，
    // 且后续若在这些点上插值/取流速，必须用真实的 (u,v)，不能用积分时的 (-u,-v)。
    if (!isNanD(params.splitLon)) {
        SeamtParams full = params;
        const double lonA = params.lonIn, lonB = params.splitLon, lonC = params.lonOut;

        auto runSeg = [&](double lo0, double lo1, double forcedP0, bool oneSeed,
                          int landModeSeg, SeamtResult &r) -> bool {
            params = full;
            params.splitLon = nanD();
            params.lonIn = lo0;
            params.lonOut = lo1;
            params.latP0Forced = forcedP0;   // NaN = 自动找
            params.singleSeed = oneSeed;     // 左段单条反推，见 SeamtParams::singleSeed
            params.landMode = landModeSeg;   // 右段用 full.landMode，左段用 leftLandMode
            return extract(msg, r);
        };

        // ---- [1] 右段：splitLon → lonOut，自动找 P₀ ----
        SeamtResult right;
        if (!runSeg(lonB, lonC, nanD(), false, full.landMode, right) || right.core.size() < 4) {
            params = full;
            out.message = wxString::FromUTF8("分段拼接：右段（分段点→终止面）提取失败");
            return false;
        }
        const double Pstar = right.core[1];      // 右段轴在 splitLon 上的起点纬度
        ncdfLog("[seamt][split] right OK, P* = %.4f N at lon=%.3f  (n=%zu)\n",
                Pstar, lonB, right.core.size() / 2);

        // ---- [2] 左段 = P* 那条流线的逆向延拓（只用 P*，单条，不做 min-T）----
        //
        // 【2026-11-05 改】原先这里跑**完整的 min-T 集合选优**（nPts=281 个种子），
        // 于是"以 P* 为起点反推"变成了"以 P* 为**中心**再找一条更快的流"：
        // 实测左段起点落到 32.211°N，而 P* = 32.616°N —— 拼接处断开 44.66 km、
        // 弦转角 77°/90°（全轴中位 0.71°）。拼出来的是**两条不同流线的硬接**，
        // 核心不是一条流线，违反 SEAMT 的定义性性质。
        // 现在左段只从 P* **单条反推**（singleSeed），它就是右段那条流线的西延。
        //
        // 【能反推多长算多长】**不要求一定要过 lonIn 那个面**（用户定）。
        // 实测 P* 的西向轨迹一路走到 (130.281, 31.239) 撞上九州、离 130°E 还差
        // 31 km —— 就收在那里。收不收这段"半截"由 extract() 的 singleSeed 分支决定
        // （G 为空时把那条轨迹当核心收下），见 [4] 处。
        //
        // 【左段用跳岛容差 leftLandMode】130–133 段要绕得过 1–2 格的小岛，
        // 严格四角规则会把近岸海域啃掉一圈。右段（正向 min-T）仍用 landMode。
        double chosen = nanD();
        SeamtResult left;
        bool leftStartsAtPstar = false, seamNeedsSmoothing = false;
        const char *leftSrc = "none";
        {
            SeamtResult probe;
            double wind = -1.0;
            if (runSeg(lonB, lonA, Pstar, true, full.leftLandMode, probe) &&
                probe.core.size() >= 4 && !polyKnotted(probe.core, &wind)) {
                chosen = Pstar;
                left = probe;
                leftStartsAtPstar = true;
                leftSrc = "P* single backtrack";
                ncdfLog("[seamt][split] P* backtrack OK (reached lonIn): %zu pts, winding=%.2f\n",
                        probe.core.size() / 2, wind);
            } else {
                ncdfLog("[seamt][split] P* backtrack REJECTED (winding=%.2f, %s) -> scan neighbours\n",
                        wind, (probe.core.size() < 4) ? "too short"
                        : (polyKnotted(probe.core) ? "knotted" : "did not reach lonIn"));
            }
        }
        if (isNanD(chosen)) {
            // 【周围点的线】从 P* 沿断面两边按 stepDeg（默认 0.005°）撒种子反向积分，
            // **离 P* 近的优先**（k 由小到大、两边交替）。
            // 只认能到 130 的（singleSeed 现在只放行到 lonIn 的），且不许打结。
            const double stepDeg = full.halfWidthDeg / (double)std::max(1, (full.nPts - 1) / 2);
            const int kMaxBack = (full.nPts - 1) / 2;          // ±halfWidthDeg
            int nTried = 0, nKnot = 0, nShort = 0;
            for (int k = 1; isNanD(chosen) && k <= kMaxBack; k++) {
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const double cand = Pstar + (double)(sgn * k) * stepDeg;
                    if (cand < full.latDomMin || cand > full.latDomMax) continue;
                    SeamtResult probe;
                    nTried++;
                    if (!runSeg(lonB, lonA, cand, true, full.leftLandMode, probe)) { nShort++; continue; }
                    if (probe.core.size() < 4) { nShort++; continue; }
                    double wind = -1.0;
                    if (polyKnotted(probe.core, &wind)) { nKnot++; continue; }
                    chosen = cand;
                    left = probe;
                    leftSrc = "neighbour backtrack";
                    ncdfLog("[seamt][split] neighbour backtrack OK at %.4f N "
                            "(%d step(s) = %.4f deg off P*), %zu pts, winding=%.2f, reached lonIn\n",
                            cand, k, std::fabs(cand - Pstar), probe.core.size() / 2, wind);
                    break;
                }
            }
            if (isNanD(chosen))
                ncdfLog("[seamt][split] no neighbour reaches lonIn: %d tried (%d knotted, %d short)\n",
                        nTried, nKnot, nShort);
        }
        if (isNanD(chosen)) {
            // 【最后兜底】min-T 选优。同样只认能到 130 的、不打结的。
            //
            // 【★ 2026-11-05 修】这里的注释一直写着"只认能到 130 的"，但代码里
            // **根本没有这条判据** —— 只查了 core.size()>=4 和不打结。于是 P* 反推
            // 和邻近点都失败后，兜底会收下一条**没走到 130°E 的线**照画不误。
            // 用户 2026-05-09 那条 130–133 就是这么出来的。判据：core 末点的经度
            // 必须离 lonA 够近（0.05°≈5.5 km；实测"撞九州差 31 km"是 0.28°，够远）。
            // 到不了就整条放弃（用户定："到不了130的都放弃"）。
            const double kReachTolDeg = 0.05;
            SeamtResult probe;
            if (runSeg(lonB, lonA, Pstar, false, full.landMode, probe) &&
                probe.core.size() >= 4 && !polyKnotted(probe.core)) {
                const double endLon = probe.core[probe.core.size() - 2];
                if (std::fabs(endLon - lonA) <= kReachTolDeg) {
                    chosen = Pstar;
                    left = probe;
                    leftSrc = "min-T fallback";
                    ncdfLog("[seamt][split] fallback to min-T left segment, core=%zu pts, "
                            "start=%.4fN (reached lonIn at %.4f)\n",
                            probe.core.size() / 2,
                            (probe.core.size() >= 2) ? probe.core[1] : 0.0, endLon);
                } else {
                    ncdfLog("[seamt][split] min-T fallback REJECTED: core ends at lon=%.4f, "
                            "%.2f deg short of %.3f -> give up (user rule: lines that do not "
                            "reach lonIn are dropped)\n",
                            endLon, std::fabs(endLon - lonA), lonA);
                }
            }
        }
        if (isNanD(chosen)) {
            params = full;
            out.message = wxString::FromUTF8("分段拼接：找不到能反推到起始面的流线");
            ncdfLog("[seamt][split] FAIL: no backtrack reaches lonIn\n");
            return false;
        }
        // 【接缝平滑】只有左段**不是** P* 那条流线时才有接缝要磨
        //（P* 本身与右段起点是同一个点，天然连续）。用户定："找到了再对交接处平滑一下"。
        if (!leftStartsAtPstar) seamNeedsSmoothing = true;

        // ---- [3] 拼接 ----
        // 左段点序是 lonB→lonA（向西），反转成 lonA→lonB 再接右段。
        // ★ 这些点上若要取流速/插值，用真实 (u,v)（左段积分时用的是 -u,-v）。
        //
        // 【去重要看情况】左段确实从 P* 出发时，右段第 0 点与左段末点是**同一个点**，
        // 丢掉；兜底的 min-T 左段起点不是 P*，必须保留右段第 0 点。
        // （不能用 chosen==Pstar 判：兜底时 chosen 也是 Pstar，但左段起点不是它。）
        const bool sameStart = leftStartsAtPstar;
        params = full;
        out.core.clear();
        out.core.reserve(left.core.size() + right.core.size());
        for (size_t i = left.core.size(); i >= 2; i -= 2) {
            out.core.push_back(left.core[i - 2]);
            out.core.push_back(left.core[i - 1]);
        }
        for (size_t i = sameStart ? 2 : 0; i < right.core.size(); i++) out.core.push_back(right.core[i]);
        // 降级留下的接缝（左段不是 P* 那条流线）在**显示时**做局部平滑。
        // 数据不动 —— core 仍是真几何，只是画的时候把那一点磨平一点。
        out.seamSmoothIdx = seamNeedsSmoothing ? (int)(left.core.size() / 2 - 1) : -1;

        out.ok = true;
        // 轴的西端纬度 = 拼接后 core 的第 0 点（= 左段反推**停下**的地方）。
        // 不能用 left.latOut：左段现在"能反推多长算多长"，没到 lonIn 时它会是 NaN。
        out.latIn  = (out.core.size() >= 2) ? out.core[1] : left.latOut;
        out.latOut = right.latOut;
        out.lengthKm = left.lengthKm + right.lengthKm;
        out.minT   = left.minT + right.minT;
        out.meanSpeed = (out.minT > 0.0)
            ? (out.lengthKm * 1000.0) / (out.minT * 86400.0) : 0.0;
        out.nSuccess = left.nSuccess + right.nSuccess;
        out.nTotal   = left.nTotal + right.nTotal;
        out.nBridged = left.nBridged + right.nBridged;
        out.nDegraded = left.nDegraded + right.nDegraded;
        out.nNearMiss = left.nNearMiss + right.nNearMiss;
        for (int i = 0; i < SEAMT_STOP_COUNT; i++)
            out.reasonCount[i] = left.reasonCount[i] + right.reasonCount[i];
        // 【2026-11-02】诊断数据必须一起带出来 —— 否则分段拼接路径下
        // critPoints / vortexCentres / traceTable 全是空的，图上一个涡、一个鞍点都
        // 画不出来（实测日志只剩 "criticals: 0 saddle(s) 0 sink(s)"），
        // 看起来就跟"没做涡识别"一样。
        out.critPoints = right.critPoints;
        out.critPoints.insert(out.critPoints.end(), left.critPoints.begin(), left.critPoints.end());
        out.vortexCentres = right.vortexCentres;
        out.vortexCentres.insert(out.vortexCentres.end(),
                                 left.vortexCentres.begin(), left.vortexCentres.end());
        out.vortexCircles = right.vortexCircles;
        out.vortexCircles.insert(out.vortexCircles.end(),
                                 left.vortexCircles.begin(), left.vortexCircles.end());
        out.vortexCircleLen = right.vortexCircleLen;
        out.vortexCircleLen.insert(out.vortexCircleLen.end(),
                                   left.vortexCircleLen.begin(), left.vortexCircleLen.end());
        out.traceTable = right.traceTable;
        out.traceTable.insert(out.traceTable.end(), left.traceTable.begin(), left.traceTable.end());
        out.tCurve = right.tCurve;
        out.subjets = right.subjets;
        out.subjetSummary = right.subjetSummary;
        out.nWrapped = right.nWrapped + left.nWrapped;
        out.elapsedMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
        out.message = wxString::Format(
            wxString::FromUTF8("分段拼接：%.1f→%.1f→%.1f，左段起点 %.4fN（%s）"),
            lonA, lonB, lonC, chosen,
            leftSrc);
        ncdfLog("[seamt][split] joined: core=%zu  L=%.2fkm  T=%.4fd  start=%.4fN P*=%.4fN sameStart=%d (%s)\n",
                out.core.size() / 2, out.lengthKm, out.minT, chosen, Pstar, (int)sameStart, leftSrc);
        return true;
    }

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

    // Γ_in 必须落在数据范围内。两套判据各写各的。
    double lonDataMin = std::min(F.lon[0], F.lon[F.nlon - 1]);
    double lonDataMax = std::max(F.lon[0], F.lon[F.nlon - 1]);
    double latDataMin = std::min(F.lat[0], F.lat[F.nlat - 1]);
    double latDataMax = std::max(F.lat[0], F.lat[F.nlat - 1]);
    if (!params.inSec.set) {
        // 【经向】老域判据，公式不动
        if (params.lonIn < lonDataMin - 1.0e-6 || params.lonIn > lonDataMax + 1.0e-6) {
            out.message = wxString::Format(
                _T("Γ_in = %.2f°E 不在数据经度范围 %.2f~%.2f°E 内"),
                params.lonIn, lonDataMin, lonDataMax);
            ncdfLog("[seamt] FAIL: lonIn %.3f outside data lon range %.3f..%.3f\n",
                    params.lonIn, lonDataMin, lonDataMax);
            return false;
        }
    } else {
        // 【线段】断面两个端点都必须落在数据范围内
        const double los[2] = { params.inSec.lon1, params.inSec.lon2 };
        const double las[2] = { params.inSec.lat1, params.inSec.lat2 };
        for (int e = 0; e < 2; e++) {
            if (los[e] < lonDataMin - 1.0e-6 || los[e] > lonDataMax + 1.0e-6 ||
                las[e] < latDataMin - 1.0e-6 || las[e] > latDataMax + 1.0e-6) {
                out.message = wxString::Format(
                    _T("Γ_in 断面端点 (%.2f,%.2f) 不在数据范围内"), los[e], las[e]);
                ncdfLog("[seamt] FAIL: gamma_in endpoint (%.3f,%.3f) outside data range "
                        "lon %.3f..%.3f lat %.3f..%.3f\n",
                        los[e], las[e], lonDataMin, lonDataMax, latDataMin, latDataMax);
                return false;
            }
        }
    }

    // ---------------- Δt / N_max ----------------
    double dxLatKm = F.dLat * kMPerDegLat / 1000.0;
    double latMid  = 0.5 * (params.latDomMin + params.latDomMax);
    double dxLonKm = F.dLon * kMPerDegLat * std::cos(latMid * M_PI / 180.0) / 1000.0;
    double dxMinKm = std::min(dxLatKm, dxLonKm);
    if (!(dxMinKm > 1.0e-6)) dxMinKm = 9.0;

    // 研究域经度范围。
    // 【2026-11-02】原先直接取 [min(lonIn,lonOut), max(lonIn,lonOut)]，等于把 Γ_in/Γ_out
    // 当成研究域的两堵墙。Γ_in 在论文里是域**内部**的一条切割断面：种子撒在断面上，
    // 流线起步时带向西分量是正常的物理运动，不该被判"离开研究域"而丢弃。
    // 实测 全3.nc Γ_in=130→Γ_out=142：172/561 条流线平均只走 15 步就 OUTDOMAIN，
    // 终点 avgEnd=(130.000, 30.499) 就压在 Γ_in 上；纬度域却是 24–38°N（比轴线宽得多），
    // 两侧尺度完全不对称。加上 lonPadDeg 外扩后 Γ_in 才真正是"断面"而非"墙"。
    // lonPadDeg = 0 时退回旧行为，便于回归对照。
    //
    // 【两套算法各写各的】
    //   经向：跨度 = [min(lonIn,lonOut), max(lonIn,lonOut)]   —— 老域，公式不动
    //   线段：跨度 = 四个断面端点的经度 bbox（Γ_in 是 A1 那种纬向断面时，
    //          lonIn/lonOut 两个数根本描述不了它）
    double lonSpanMin = 0.0, lonSpanMax = 0.0;
    if (!params.inSec.set) {
        lonSpanMin = std::min(params.lonIn, params.lonOut);
        lonSpanMax = std::max(params.lonIn, params.lonOut);
    } else {
        lonSpanMin = std::min(std::min(params.inSec.lon1, params.inSec.lon2),
                              std::min(params.outSec.lon1, params.outSec.lon2));
        lonSpanMax = std::max(std::max(params.inSec.lon1, params.inSec.lon2),
                              std::max(params.outSec.lon1, params.outSec.lon2));
    }
    double lonDomMin = lonSpanMin - params.lonPadDeg;
    double lonDomMax = lonSpanMax + params.lonPadDeg;

    // uMax 仍按研究跨度 [lonIn,lonOut] 统计，不带 padding：
    // 它只用于 CFL 自动推 Δt，取值范围应当就是研究区本身。
    double uMaxEst = estimateUMax(F, lonSpanMin, lonSpanMax,
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
    ncdfLog("[seamt] domain lon=%.3f..%.3f (span %.3f..%.3f + pad %.2f)  lat=%.3f..%.3f\n",
            lonDomMin, lonDomMax, lonSpanMin, lonSpanMax, params.lonPadDeg,
            params.latDomMin, params.latDomMax);
    ncdfLog("[seamt] uMax99.5=%.3f m/s  dt=%.5f d (%.1f s)  CFL=%.3f  nMax=%d\n",
            uMaxEst, dtDays, dtSec, cfl, nMax);
    if (dxMinKm > 30.0) {
        ncdfLog("[seamt] WARN: dxMin=%.1fkm too coarse to resolve jet cross-structure\n",
                dxMinKm);
        out.message = _T("分辨率偏粗，射流横结构可能分辨不出");
    }

    // ---------------- [1] P₀ 候选与起点集合 ----------------
    //
    // 【2026-11-02 多候选 P₀】断面上可能不止一支强流，"哪支是射流"靠速度启发式
    // 会挑错（见 findP0 的注释：全3.nc 130°E 挑到 30.25°N 的别的流，黑潮在 31.4°N）。
    // 现在把每一支候选都布一个 P₀±halfWidth 的集合，**并集**交给 min-T 去选 ——
    // 这正是算法自己的判据。只有一个候选时与论文原算法完全一致。
    std::vector<double> p0Lat, p0Sp;
    double spP0 = nanD();
    if (!isNanD(params.latP0Forced)) {
        // 强制 P₀：分段拼接时两段必须共用同一个 P₀（否则反推段会选到黑潮逆流），
        // 这种情况下**只布这一个集合**，不启用多候选。
        // latP0Forced 是「沿断面坐标 s」（经向下就是纬度），先换算成 lon/lat。
        double fu, fv, fxLon = 0.0, fxLat = 0.0;
        ginPoint(params, params.latP0Forced, fxLon, fxLat);
        if (seamtSample(F, fxLon, fxLat, fu, fv))
            spP0 = std::hypot(fu, fv);
        p0Lat.push_back(params.latP0Forced);
        p0Sp.push_back(spP0);
        ncdfLog("[seamt] P0 FORCED to %.4f N (|u|=%.3f m/s)\n", params.latP0Forced, spP0);
    } else {
        spP0 = findP0(F, params, p0Lat, p0Sp);
    }
    if (p0Lat.empty() || spP0 < 0.0) {
        if (!params.inSec.set) {
            out.message = wxString::Format(
                _T("Γ_in 上 %.1f~%.1f°N 取不到有效流速，无法布撒种子"),
                params.latP0Min, params.latP0Max);
        } else {
            out.message = wxString::Format(
                _T("Γ_in 断面 (%.2f,%.2f)-(%.2f,%.2f) 上取不到有效流速，无法布撒种子"),
                params.inSec.lon1, params.inSec.lat1, params.inSec.lon2, params.inSec.lat2);
        }
        ncdfLog("[seamt] FAIL: no valid velocity along gamma_in (lonIn=%.3f)\n", params.lonIn);
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
    ncdfLog("[seamt] P0 = %.4f N (|u|=%.3f m/s), uniform seeding step %.5f deg, "
            "%u candidate(s)\n",
            p0Lat[0], spP0, stepDeg, (unsigned)p0Lat.size());

    // 【撒点：全断面均匀】间距 stepDeg = halfWidthDeg / ((nPts-1)/2)（默认 0.005°），
    // 沿断面从 sSeedMin 铺到 sSeedMax，只留 u>0 的点。
    // （旧的「P₀±halfWidth 带 + 由内向外补足」已删，2026-11-05。）
    std::vector<double> seedLat;   // 【语义】沿断面坐标 s（经向下就是纬度）
    double sSeedMin = 0.0, sSeedMax = 0.0;
    ginSeedRange(params, sSeedMin, sSeedMax);
    seedLat.reserve((size_t)((sSeedMax - sSeedMin) / stepDeg) + 8);
    int seedOutOfRange = 0, seedNoData = 0, seedDup = 0;
    if (params.singleSeed) {
        // 【优先级最高】只布 P₀ 一个种子、不做带子展开（分段拼接左段用）。
        // 必须排在 seedMode==2 之前 —— 否则均匀撒点会盖掉它，左段拿到整条断面
        // 的种子，best=0 取到最南端那条（2026-11-05 实测：起点跑到 24.0°N，
        // 拼接处凭空 952 km 跳变）。
        for (size_t c = 0; c < 1 && c < p0Lat.size(); c++) {
            double u, v, gLon = 0.0, gLat = 0.0;
            ginPoint(params, p0Lat[c], gLon, gLat);
            if (seamtSample(F, gLon, gLat, u, v)) seedLat.push_back(p0Lat[c]);
            else seedNoData++;
        }
        ncdfLog("[seamt] singleSeed: 1 seed at s=%.4f (kept=%u)\n",
                p0Lat.empty() ? 0.0 : p0Lat[0], (unsigned)seedLat.size());
    } else {
        // 【撒点：全断面均匀 stepDeg（默认 0.005°），只留 u>0】
        // 排除流向坐标轴**第二、三象限**（西向）的点，即只留第一、四象限。
        // 不以 P₀ 为中心、不设 halfWidth 带宽 —— 间距 stepDeg 仍由
        // halfWidthDeg / ((nPts-1)/2) 决定（默认 0.70/140 = 0.005°）。
        //
        // 【2026-11-05 由「P₀±halfWidth 带」改来，旧法已删】带宽只有 1.4°，
        // 换一年数据轴在断面上的交点挪远一点就够不着，而且靠 P₀ 落点对不对兜底；
        // 均匀撒点覆盖整条断面（24–41°N），轴交在哪一纬度都能被布到。
        // 实测三年 P*（133°E 交点）31.58–32.64°N，T 与旧法只差 0.35%（噪声量级）、
        // 种子还少 20%。采用它的真正理由是更简单、更少、不依赖 P₀。
        for (double s = sSeedMin; s <= sSeedMax + 1.0e-9; s += stepDeg) {
            double gLon = 0.0, gLat = 0.0, u = 0.0, v = 0.0;
            ginPoint(params, s, gLon, gLat);
            if (!seamtSample(F, gLon, gLat, u, v)) { seedNoData++; continue; }
            if (!(u > 0.0)) { seedOutOfRange++; continue; }
            seedLat.push_back(s);
        }
        ncdfLog("[seamt] uniform seeding: %u seeds @ %.5f deg over s=%.3f..%.3f "
                "(u>0 only; dropped noData=%d westward=%d)\n",
                (unsigned)seedLat.size(), stepDeg, sSeedMin, sSeedMax,
                seedNoData, seedOutOfRange);
    }
    std::sort(seedLat.begin(), seedLat.end());

    out.nTotal = (int)seedLat.size();
    if (out.nTotal > 0) {
        ncdfLog("[seamt] seeds: %d per candidate x %u candidate(s) -> %d kept "
                "(rejected outOfDomain=%d noData=%d duplicate=%d), lat span %.4f..%.4f\n",
                params.nPts, (unsigned)p0Lat.size(), out.nTotal,
                seedOutOfRange, seedNoData, seedDup,
                seedLat.front(), seedLat.back());
    } else {
        ncdfLog("[seamt] seeds: 0 kept (rejected outOfDomain=%d noData=%d)\n",
                seedOutOfRange, seedNoData);
    }
    if (out.nTotal < (params.singleSeed ? 1 : 3)) {
        out.message = params.singleSeed
            ? wxString::FromUTF8("指定起点取不到有效流速，无法反推")
            : wxString::Format(_T("有效起点仅 %d 个（应有 %d 个），集合不足以比选"),
                               out.nTotal, params.nPts);
        ncdfLog("[seamt] FAIL: only %d valid seeds (singleSeed=%d)\n",
                out.nTotal, (int)params.singleSeed);
        return false;
    }

    // ---------------- [2] 积分流线族 ----------------
    //
    // 决定性诊断：从 P0 试走 20 步，把每步的速度与位移直接打出来。
    // G 为空时最容易误判成"物理上无贯通急流"，其实是数值问题——
    // 速度正常而位移离谱是积分/度量的问题；速度本身≈0 是数据的问题。
    {
        double lo = 0.0, la = 0.0;
        ginPoint(params, p0Lat[0], lo, la);
        ncdfLog("[seamt] probe: P0=(%.4f,%.4f) dtSec=%.1f\n", lo, la, dtSec);

        // Γ_in 上的流速剖面（每 0.25°）：直接看出轴在哪、往哪流
        double sProfMin = 0.0, sProfMax = 0.0;
        ginP0Range(params, sProfMin, sProfMax);
        ncdfLog("[seamt] gamma_in profile (lon=%.3f), lat / |u| / u / v / bearing\n", lo);
        for (double s = sProfMin - 1.0; s <= sProfMax + 1.0 + 1e-9; s += 0.25) {
            double plon = 0.0, plat = 0.0;
            ginPoint(params, s, plon, plat);
            double u = 0.0, v = 0.0;
            bool ok = seamtSample(F, plon, plat, u, v);
            ncdfLog("[seamt]   lat %6.2f %s|u|=%7.4f u=%8.4f v=%8.4f brg=%6.1f\n",
                    plat, ok ? "" : "FAIL ", ok ? std::hypot(u, v) : -1.0,
                    ok ? u : 0.0, ok ? v : 0.0,
                    ok ? std::atan2(u, v) * 180.0 / M_PI : 0.0);
        }

        for (int s = 0; s < 20; s++) {
            double u = 0.0, v = 0.0;
            bool ok = seamtSample(F, lo, la, u, v);
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
    // 小缺口桥接的最大距离（km）。ncdf_flags.txt 写 seamtBridgeKm=N 可调，0=关闭。
    // 默认 25 km ≈ 3 个 1/12° 格距 —— 够跨小岛缺口，跨不过大陆/大岛。
    double maxBridgeKm = 25.0;
    // 【2026-11-02 涡外缘绕行】的开关与参数，同样从 ncdf_flags.txt 读（每轮一次，不在热路径）：
    //   seamtStallM=50        连续多少米以内的位移算"停滞"
    //   seamtStallSteps=20    连续多少步小位移才判定被捕获
    //   seamtVortexWrap=0     关掉绕行（默认开）
    //   seamtWrapMaxKm=150    从涡心找外缘的径向上限
    //   seamtWrapDirs=24      外缘采样的方位数
    //   seamtWrapBranches=4   每个涡生成几条候选续段
    //   seamtWrapEntryU=1.0   段1 截断阈值：倒着找最后一个 |u| >= 该值的点
    //                         （★ 全3.nc 153°E 实测：0.3 时拼接 T=61.4 输给 43.0；
    //                           1.0 时 T=35.5 胜出，轴脊垂距中位 30 → 8.5 km）
    //   seamtWrapEntrySpan=2  入口点在速度环上左右各试几个（优化②）
    //   seamtWrapWeakFrac=0.5 强/弱涡分界：环上最大切向速度 / 射流速度（优化⑤），
    //                         低于此值判弱涡、不做绕行拼接
    //   seamtWrapKeepPerExit=3 每个（涡, 出口）组最多留几条绕行流线（内存纪律，
    //                         见 (e) 末尾的注释；不影响 min-T 的胜者）
    //   【已废】seamtWrapArc    段2 现在按 min-T 在「速度环弧 / 径向直连」里选，
    //                         不再需要指定走哪条（优化①）
    double stallMeters = 50.0;
    int    stallSteps  = 20;
    int    wrapOn      = 1;
    double wrapMaxKm   = 150.0;
    int    wrapDirs    = 24;
    int    wrapBranches = 4;
    double wrapEntryU   = 1.00;
    int    wrapEntrySpan = 2;
    double wrapWeakFrac  = 0.50;
    int    wrapKeepPerExit = 3;
    {
        FILE *fp = fopen("C:\\ProgramData\\opencpn\\ncdf_flags.txt", "r");
        if (fp) {
            char lb[256];
            while (fgets(lb, sizeof(lb), fp)) {
                const char *eq = strchr(lb, '=');
                if (!eq) continue;
                if      (strncmp(lb, "seamtBridgeKm", 13) == 0)   maxBridgeKm  = atof(eq + 1);
                else if (strncmp(lb, "seamtStallM", 11) == 0)     stallMeters  = atof(eq + 1);
                else if (strncmp(lb, "seamtStallSteps", 15) == 0) stallSteps   = atoi(eq + 1);
                else if (strncmp(lb, "seamtVortexWrap", 15) == 0) wrapOn       = atoi(eq + 1);
                else if (strncmp(lb, "seamtWrapMaxKm", 14) == 0)  wrapMaxKm    = atof(eq + 1);
                else if (strncmp(lb, "seamtWrapDirs", 13) == 0)   wrapDirs     = atoi(eq + 1);
                else if (strncmp(lb, "seamtWrapBranches", 17) == 0) wrapBranches = atoi(eq + 1);
                else if (strncmp(lb, "seamtWrapEntryU", 15) == 0) wrapEntryU   = atof(eq + 1);
                else if (strncmp(lb, "seamtWrapEntrySpan", 18) == 0) wrapEntrySpan = atoi(eq + 1);
                else if (strncmp(lb, "seamtWrapWeakFrac", 17) == 0) wrapWeakFrac = atof(eq + 1);
                else if (strncmp(lb, "seamtWrapKeepPerExit", 20) == 0) wrapKeepPerExit = atoi(eq + 1);
            }
            fclose(fp);
        }
    }
    if (stallSteps < 3) stallSteps = 3;
    if (wrapDirs < 8) wrapDirs = 8;
    if (wrapDirs > 72) wrapDirs = 72;
    if (wrapBranches < 1) wrapBranches = 1;
    if (wrapBranches > 8) wrapBranches = 8;
    if (wrapEntrySpan < 0) wrapEntrySpan = 0;
    if (wrapEntrySpan > 8) wrapEntrySpan = 8;
    if (wrapKeepPerExit < 1) wrapKeepPerExit = 1;
    if (wrapKeepPerExit > 32) wrapKeepPerExit = 32;
    ncdfLog("[seamt] bridge: maxBridgeKm=%.1f (flags seamtBridgeKm=N, 0=off)\n", maxBridgeKm);
    ncdfLog("[seamt] stall: %.0f m x %d steps ; vortex wrap %s (seg2=min-T maxKm=%.0f dirs=%d "
            "branches=%d entrySpan=%d weakFrac=%.2f keepPerExit=%d)\n",
            stallMeters, stallSteps, wrapOn ? "ON" : "off",
            wrapMaxKm, wrapDirs, wrapBranches, wrapEntrySpan, wrapWeakFrac, wrapKeepPerExit);

    // 积分方向：Γ_out 在 Γ_in 东边 → 顺流向；西边 → 逆流向反推（用 (-u,-v)）。
    // 反推段的点顺序要反转后再拼接，且取流速时用真实 (u,v)（2026-10-30）。
    // 两套几何下这个判据都是「Γ_out 在 Γ_in 的哪一侧」，用 goutDir 的 x 分量判即可
    // （经向下 tx=±1，与原来的 (lonOut > lonIn) 等价）。
    double tx0 = 0.0, ty0 = 0.0;
    goutDir(params, tx0, ty0);
    const double dirSign = (tx0 >= 0.0) ? 1.0 : -1.0;
    ncdfLog("[seamt] trace direction: %s (dirSign=%+.0f)\n",
            dirSign > 0 ? "forward along flow" : "REVERSED against flow", dirSign);

    for (size_t i = 0; i < seedLat.size(); i++) {
        double sLon = 0.0, sLat = 0.0;
        ginPoint(params, seedLat[i], sLon, sLat);
        traceOne(F, params, lonDomMin, lonDomMax,
                 sLon, sLat, dtSec, dtDays, nMax, maxBridgeKm, dirSign,
                 stallMeters, stallSteps, traces[i]);
        out.reasonCount[traces[i].reason]++;
        out.nDegraded += traces[i].nDegraded;
        out.nBridged += traces[i].nBridged;
        // 【诊断】逐条记录起始纬度 / 终止原因 / T。每轮 281 行，
        // 所以只在 ncdf_flags.txt 里写 seamtTrace=1 时才打（每轮读一次文件，不在热路径）。
        // min-T 只在 reason==SEAMT_EXIT 的流线里比（下面 G 的收集条件），
        // 被打断（AGROUND）或出域的流线完全不参与 —— 2026-10-30 就是靠这份 T(φ)
        // 剖面定位到「入口断面被岛屿堵死、急流核心起不了步」的。
        if (seamtTraceEnabled()) {
            ncdfLog("[seamt][trace] lat0=%7.3f reason=%d nSteps=%d bridge=%d T=%.5f end=(%.3f,%.3f)\n",
                    seedLat[i], (int)traces[i].reason, traces[i].nSteps, traces[i].nBridged, traces[i].T,
                    traces[i].pts.size() >= 2 ? traces[i].pts[traces[i].pts.size() - 2] : 0.0,
                    traces[i].pts.size() >= 2 ? traces[i].pts[traces[i].pts.size() - 1] : 0.0);
        }
        // 出域但离 Γ_out 很近 = 研究域边界在剪流。这类流线本该成功，
        // 被边界吃掉后 min-T 会被次优路径顶掉（2026-10-29 实测踩到）。
        if (traces[i].reason == SEAMT_OUTDOMAIN && traces[i].pts.size() >= 2) {
            double endLon = traces[i].pts[traces[i].pts.size() - 2];
            if (std::fabs(endLon - params.lonOut) < 0.2) out.nNearMiss++;
        }
    }
    // ---------------- [2b] 涡外缘绕行（2026-11-02）----------------
    //
    // 被涡心（Okubo-Weiss W<0 的汇）捕获的流线原本只能判 MAXSTEP 丢掉，min-T 于是
    // 只能在幸存者里挑。实测全3.nc 135→160：贴脊那族（33.23–33.74°N）全被
    // (153.0°E, 36.3°N) 的螺旋汇吸走，min-T 只能选离脊的那条，轴在 139–142°E 下凹 180 km。
    //
    // 论文 §3.3 的物理是「急流绕过涡核」（核心环绕椭圆点 E1），所以正确处理不是丢掉
    // 而是**沿涡外缘绕过去**，三段拼接：
    //   段1 = 原流线到捕获点（真流线）
    //   段2 = 捕获点径向投到涡边界 + 沿 W=0 等值线绕到出口点（绕行弧）
    //   段3 = 从出口点正常续积分到 Γ_out（真流线）
    //
    // 【T 记账】段2 按**等效步数 × Δt**（用户定）：nEq = Σ Δs/(|u|·Δt)，T₂ = nEq·Δt。
    //
    // 【不只一条】出口点取边界上速度最大的 wrapBranches 个，**各积一条段3、全部进 G**
    // —— 呼应「鞍点分裂出多条通道，由 min-T / T(φ_out) 分主次」的处理，不挑一条硬掰。
    // 同一涡的段2/段3 只算一次，被吸的流线共享（否则 191 条 × 4 分支的积分代价太大）。
    int nWrapped = 0;
    // 每条绕行流线出自哪个涡（下标与 traces 里 append 的那段对齐）。
    // 用来做「只显示真的改变了轴的涡」——见 [5] 的因果过滤。
    std::vector<int> wrapVortexOf;
    size_t wrapBase = (size_t)-1;          // 绕行流线在 traces 里的起始下标
    if (wrapOn) {
        struct WrapBranch {
            size_t iIn, iOut;             // 速度环上的入口 / 出口下标
            double tRank;                 // 段2 的 min-T（选优用）
            SeamtTrace br;                // 段3
        };
        // (a) 捕获点聚类成涡
        std::vector<double> vLon, vLat, vSumLon, vSumLat;
        std::vector<int> vN;
        for (size_t i = 0; i < traces.size(); i++) {
            if (!traces[i].stalled) continue;
            double lo = std::floor(traces[i].stallLon * 2.0) / 2.0;
            double la = std::floor(traces[i].stallLat * 2.0) / 2.0;
            size_t k = 0;
            for (; k < vN.size(); k++) if (vLon[k] == lo && vLat[k] == la) break;
            if (k == vN.size()) {
                vLon.push_back(lo); vLat.push_back(la);
                vSumLon.push_back(0.0); vSumLat.push_back(0.0); vN.push_back(0);
            }
            vN[k]++;
            vSumLon[k] += traces[i].stallLon;
            vSumLat[k] += traces[i].stallLat;
        }

        std::vector<std::vector<WrapBranch> > wrapOf(vN.size());
        std::vector<VortexEdge> wrapEdge(vN.size());   // (e) 里重建段2 要用速度环
        std::vector<int> entryOf(vN.size(), -1);

        // 【优化⑤ 的"射流速度"要取全域的】不能用被捕获流线的局部速度——
        // 845 条流线在 (136.75,38.75) 的滞止区里爬，那里的"最快"只有 0.3 m/s，
        // 于是连 0.5 m/s 的弱涡都会被判成"比射流还强"。
        //
        // 【2026-11-06】还要防"集合太小"的退化：分段拼接的左段常常只有 1 条种子
        // （singleSeed），只扫流线的话 jetSpeed 就退化成"这条线自己最快处"。
        // 所以再扫一遍 Γ_in 整条断面（粗步进 0.02°），两者取大。
        double jetSpeed = 0.0;
        for (size_t i = 0; i < traces.size(); i++) {
            const std::vector<double> &pp = traces[i].pts;
            // pp 是 lon,lat 交错存的：第 p 个点在 pp[2p],pp[2p+1]，上界是 size()/2
            for (size_t p = 0; 2 * p + 1 < pp.size(); p += 8) {
                double uu, vv;
                if (!seamtSample(F, pp[2 * p], pp[2 * p + 1], uu, vv)) continue;
                const double s2 = std::hypot(uu, vv);
                if (s2 > jetSpeed) jetSpeed = s2;
            }
        }
        {
            double sMinSec = 0.0, sMaxSec = 0.0;
            ginP0Range(params, sMinSec, sMaxSec);
            for (double s = sMinSec; s <= sMaxSec + 1.0e-9; s += 0.02) {
                double gLon = 0.0, gLat = 0.0;
                ginPoint(params, s, gLon, gLat);
                double uu, vv;
                if (!seamtSample(F, gLon, gLat, uu, vv)) continue;
                const double s2 = std::hypot(uu, vv);
                if (s2 > jetSpeed) jetSpeed = s2;
            }
        }
        ncdfLog("[seamt][wrap] jet speed (max |u| over ensemble + gamma_in) = %.3f m/s\n", jetSpeed);
        for (size_t k = 0; k < vN.size(); k++) {
            const double lonC = vSumLon[k] / (double)vN[k];
            const double latC = vSumLat[k] / (double)vN[k];
            VortexEdge edge;
            if (!findVortexEdge(F, lonC, latC, wrapMaxKm, wrapDirs, edge)) {
                ncdfLog("[seamt][wrap] vortex #%u around (%.3f, %.3f) captured=%d: "
                        "W=0 contour NOT found — no wrap for these\n",
                        (unsigned)(k + 1), lonC, latC, vN[k]);
                continue;
            }
            double rMin = 1.0e30, rMax = 0.0;
            for (size_t i = 0; i < edge.rKm.size(); i++) {
                if (edge.rKm[i] < rMin) rMin = edge.rKm[i];
                if (edge.rKm[i] > rMax) rMax = edge.rKm[i];
            }
            ncdfLog("[seamt][wrap] vortex #%u around (%.3f, %.3f) captured=%d: "
                    "core(W=0) r=%.1f km, velocity-ring r=%.1f..%.1f km, %u samples\n",
                    (unsigned)(k + 1), lonC, latC, vN[k], edge.rCoreKm,
                    (edge.rKm.empty() ? 0.0 : rMin), rMax, (unsigned)edge.lon.size());

            // 【2026-11-02 上图】把涡心与边界存下来给渲染用。
            // 被捕获的都是"汇"（det>0、trace<0），就地用雅可比分一下子类。
            {
                double a, b, c, d;
                double vType = -1.0;
                if (uvJacobian(F, lonC, latC, a, b, c, d)) {
                    const double tr = a + d, det = a * d - b * c, disc = tr * tr - 4.0 * det;
                    if (det < 0.0)                       vType = 0.0;   // 鞍点
                    else if (disc < 0.0)                 vType = (std::fabs(tr) <= 0.05 * std::sqrt(-disc))
                                                                    ? 1.0 : (tr < 0.0 ? 2.0 : 3.0);
                    else                                 vType = (tr < 0.0) ? 4.0 : 5.0;
                }
                out.vortexCentres.push_back(lonC);
                out.vortexCentres.push_back(latC);
                out.vortexCentres.push_back(vType);
                out.vortexCentres.push_back(edge.rCoreKm);
                for (size_t i = 0; i < edge.lon.size(); i++) {
                    out.vortexCircles.push_back(edge.lon[i]);
                    out.vortexCircles.push_back(edge.lat[i]);
                }
                if (!edge.lon.empty()) {                 // 闭合成环，便于直接画
                    out.vortexCircles.push_back(edge.lon[0]);
                    out.vortexCircles.push_back(edge.lat[0]);
                    out.vortexCircleLen.push_back((int)edge.lon.size() + 1);
                }
            }

            wrapEdge[k] = edge;

            // (a2) 强涡 / 弱涡分流（优化⑤）
            //
            // 【为什么分】不是每个停滞点都是"急流绕不过去的强涡"。弱的中尺度涡
            // （环上切向速度远小于射流速度）射流本来就能从旁边过去，硬做三段拼接
            // 反而会造出一条绕小圈的假路径。判据用**无量纲强度**：
            //     strength = (速度环上最大 |u|) / (射流速度)
            // 射流速度取被捕获流线进涡前的最快点（每 8 点抽样，省时）。
            // strength < seamtWrapWeakFrac（默认 0.5）→ 弱涡，跳过绕行，只留诊断圆。
            {
                double ringMax = 0.0;
                for (size_t i = 0; i < edge.sp.size(); i++)
                    if (edge.sp[i] > ringMax) ringMax = edge.sp[i];
                const double strength = (jetSpeed > 1.0e-9) ? (ringMax / jetSpeed) : 0.0;
                ncdfLog("[seamt][wrap] vortex #%u strength: ring max |u|=%.3f  jet |u|=%.3f  "
                        "= %.2f (weakFrac=%.2f)\n",
                        (unsigned)(k + 1), ringMax, jetSpeed, strength, wrapWeakFrac);
                if (strength < wrapWeakFrac) {
                    ncdfLog("[seamt][wrap] vortex #%u around (%.3f, %.3f): WEAK — skip wrap "
                            "(jet passes by on its own)\n", (unsigned)(k + 1), lonC, latC);
                    continue;
                }
            }

            // (b) 入口点 = 边界上离捕获点最近的那个（捕获点≈涡心，取它相对涡心的方位）
            double px = 0.0, py = 0.0;
            for (size_t i = 0; i < traces.size(); i++) {
                if (!traces[i].stalled) continue;
                double lo = std::floor(traces[i].stallLon * 2.0) / 2.0;
                double la = std::floor(traces[i].stallLat * 2.0) / 2.0;
                if (lo != vLon[k] || la != vLat[k]) continue;
                px += traces[i].stallLon - lonC;
                py += traces[i].stallLat - latC;
            }
            size_t iIn;
            if (std::fabs(px) + std::fabs(py) > 1.0e-9) {
                double ang = std::atan2(py, px) * 180.0 / M_PI;
                iIn = 0; double bd = 1.0e30;
                for (size_t i = 0; i < edge.angDeg.size(); i++) {
                    double da = std::fabs(edge.angDeg[i] - ang);
                    if (da > 180.0) da = 360.0 - da;
                    if (da < bd) { bd = da; iIn = i; }
                }
            } else {
                iIn = edge.nearestIndex(lonC, latC);
            }
            entryOf[k] = (int)iIn;

            // (c) 出口点：必须在**涡心下游侧** 且 流向朝 Γ_out。
            //
            // 【★ 2026-11-02 踩坑】只按"环上速度最大"选会挑到上游——环上速度最大的
            // 恰恰是射流**进入**涡的那一侧。实测段2 从截断点 (153.21, 35.61) 跳回
            // (152.48, 36.13)，往回 65 km，画出来就是"先走到 153.2°E 再绕回 152.5°E"
            // 的圈。所以落点必须在涡心下游侧（经度方向上不许回折）。
            std::vector<size_t> cand;
            for (size_t i = 0; i < edge.lon.size(); i++) {
                if (!((edge.lon[i] - lonC) * dirSign > 0.0)) continue;   // 涡心下游侧
                if (!(dirSign * edge.u[i] > 0.0)) continue;              // 流向朝 Γ_out
                cand.push_back(i);
            }
            if (cand.empty()) {
                // 放宽：只要在涡心下游侧（东侧可能是回流，但落点之后段3 自己会走）
                for (size_t i = 0; i < edge.lon.size(); i++) {
                    if ((edge.lon[i] - lonC) * dirSign > 0.0) cand.push_back(i);
                }
            }
            if (cand.empty()) continue;

            // (d) 候选（入口, 出口）组合，按**段2 的 min-T** 选优（优化①②③）
            //
            // 【① 段2 只走速度环弧，不许直连】（2026-11-05 用户定）
            //   原来在「速度环弧 / 直连」里按 T 选，实测 135→160 那个涡 71 次全选
            //   直连 —— 捷径赢在"跳过涡"，画出来是横穿涡心的直线，不是急流绕涡的
            //   形态。用户定：**强涡区的线永远绕弧，不许抄近路**。现在只认弧，
            //   建不出弧的（入口=出口）直接不生成该分支。
            // 【② 入口也参与选优】入口不再钉死在"离捕获点最近"那一个，而是它左右
            //   ±seamtWrapEntrySpan 个环点一起试。旧的「按 |u| 降序取前 4 个出口」
            //   会漏掉「|u| 略小但段3 短得多」的出口；现在排名用 T₂，取前
            //   wrapBranches 个**互不相同**的出口各积一条段3，最后由 min-T 比
            //   **T₁+T₂+T₃**（T₃ = wb.br.T，本来就记在 nt.T 里）。
            //   —— 这正面回答了「出口的不同会不会影响从出口到 Γ_out 的 T」：会，
            //      而且现在正是靠 T₃ 参与总 T 的比较来挑出口的（只按 T₂ 预筛，
            //      最终排名比的是 T₁+T₂+T₃）。
            // 【③ T 沿路径积分】polylineTravelDays 逐段取两端速度平均，不用出口点
            //   的速度代表整段（见该函数的注释）。
            const size_t nE = edge.lon.size();
            std::vector<size_t> entryCand;
            for (int dE = -wrapEntrySpan; dE <= wrapEntrySpan; dE++) {
                const long ii = (long)iIn + (long)dE;
                if (ii < 0 || ii >= (long)nE) continue;
                bool dup = false;
                for (size_t q = 0; q < entryCand.size(); q++)
                    if (entryCand[q] == (size_t)ii) { dup = true; break; }
                if (!dup) entryCand.push_back((size_t)ii);
            }
            if (entryCand.empty()) entryCand.push_back(iIn);

            struct WrapCombo {
                size_t ei, oi;
                double t2;
            };
            std::vector<WrapCombo> combos;
            combos.reserve(entryCand.size() * cand.size());
            for (size_t a = 0; a < entryCand.size(); a++) {
                for (size_t b = 0; b < cand.size(); b++) {
                    const size_t ei = entryCand[a], oi = cand[b];
                    if (ei == oi) continue;
                    std::vector<double> arc;
                    double tArc = 1.0e30;
                    ringArcMinT(edge, F, ei, oi, dtSec, dtDays, arc, tArc);
                    if (arc.size() < 4) continue;      // 建不出弧 → 不要这条分支
                    WrapCombo wc;
                    wc.ei = ei;
                    wc.oi = oi;
                    wc.t2 = tArc;
                    combos.push_back(wc);
                }
            }
            std::sort(combos.begin(), combos.end(),
                      [](const WrapCombo &x, const WrapCombo &y) {
                          if (x.t2 != y.t2) return x.t2 < y.t2;
                          return x.oi < y.oi;
                      });
            // 取前 wrapBranches 个**互不相同**的出口（同一出口的段3 一样，重复积没意义）
            {
                std::vector<WrapCombo> keep;
                for (size_t i = 0; i < combos.size() && (int)keep.size() < wrapBranches; i++) {
                    bool dup = false;
                    for (size_t q = 0; q < keep.size(); q++)
                        if (keep[q].oi == combos[i].oi) { dup = true; break; }
                    if (!dup) keep.push_back(combos[i]);
                }
                combos.swap(keep);
            }

            // (d2) 每个入选的出口各积一条段3（真流线）
            for (size_t ci = 0; ci < combos.size(); ci++) {
                const size_t iOut = combos[ci].oi;
                WrapBranch wb;
                wb.iIn   = combos[ci].ei;
                wb.iOut  = iOut;
                wb.tRank = combos[ci].t2;
                wb.br    = SeamtTrace();
                traceOne(F, params, lonDomMin, lonDomMax,
                         edge.lon[iOut], edge.lat[iOut], dtSec, dtDays, nMax,
                         maxBridgeKm, dirSign, stallMeters, stallSteps, wb.br);
                ncdfLog("[seamt][wrap]   branch %u: in=%u out=%u (%.4f,%.4f) |u|=%.3f "
                        "r=%.0fkm  T2=%.4f d  -> reason=%d T3=%.4f\n",
                        (unsigned)(ci + 1), (unsigned)wb.iIn, (unsigned)iOut,
                        edge.lon[iOut], edge.lat[iOut], edge.sp[iOut],
                        edge.rKm[iOut], wb.tRank, wb.br.reason, wb.br.T);
                wrapOf[k].push_back(wb);
            }
        }

        // (e) 拼接：段1 + 段2 + 段3，全部进 traces
        //
        // 【★ 段1 必须截在"还在射流上"的那一点】不能等到流线螺旋进涡心才停。
        // 实测全3.nc 153°E：同一种子 33.4313，Γ_out=152 时 619 步 / 18.71 天就到；
        // Γ_out=160 时却要 1957 步 / 58.86 天才在涡心停住 —— 中间 40 天全在涡里打转。
        // 把这 40 天算进 T₁，绕行流线（T≈76）必输给原来的 43.02，绕行等于白做。
        // 截到最后一个 |u| ≥ seamtWrapEntryU 的点，T₁ 就回到 18.7 天量级，
        // 拼接总 T≈33.8 天，才能在 min-T 里胜出。
        std::vector<SeamtTrace> wrapped;
        std::vector<double> wrappedSeed;
        std::vector<double> wrappedKey;    // 分组键 = 涡号*16 + 分支号
        std::vector<double> wrappedRank;   // 组内排名依据 = nt.T
        std::vector<int> wrappedVtx;       // 出自哪个涡（因果过滤用）
        int nSeg2Arc = 0;                  // 段2 走速度环弧的条数（现在只允许弧）
        for (size_t i = 0; i < traces.size(); i++) {
            if (!traces[i].stalled) continue;
            double lo = std::floor(traces[i].stallLon * 2.0) / 2.0;
            double la = std::floor(traces[i].stallLat * 2.0) / 2.0;
            size_t k = 0;
            for (; k < vN.size(); k++) if (vLon[k] == lo && vLat[k] == la) break;
            if (k == vN.size() || wrapOf[k].empty()) continue;

            // 段1 的截断点：倒着找最后一个速度还够快的点。
            //
            // 【★ 2026-11-02 踩坑】只按速度截会出 1830 km 的直线段：全3.nc 2022-06-17
            // 实测段2 从 (137.03, 32.77) 直连 (156.65, 35.77)，因为那条流线从 137°E 起
            // 就一直在慢水里爬向涡，|u|>=1.0 的最后一点远在 1830 km 外 —— 截断只管速度、
            // 不管离涡多远，段2 就成了一条横穿大洋的直线。
            // **所以还要加"离涡心够近"这一条**：截断点必须在涡的影响范围内，
            // 段2 才是"跨过涡"这一小段，而不是长途直线。
            //
            // 【优化④】再加一条「切向仍朝 Γ_out」：绕到涡的背风侧时速度再大也不该
            // 在那里交接，否则段2 会从涡的另一侧拉回来，画出来是个大钩子。
            // 判据是 u·goutDir > 0（90° 内）—— 刻意放宽到 90° 而不用 jetAngMaxDeg，
            // 因为射流绕涡时本来就在转，卡 45° 会把所有候选都筛掉。
            const double lonC = vSumLon[k] / (double)vN[k];
            const double latC = vSumLat[k] / (double)vN[k];
            const std::vector<double> &pp = traces[i].pts;
            const size_t nPts = pp.size() / 2;
            if (nPts < 2) continue;
            size_t nKeep = 0;
            for (size_t p = nPts; p-- > 0; ) {
                double uu, vv;
                if (!seamtSample(F, pp[2 * p], pp[2 * p + 1], uu, vv)) continue;
                if (std::hypot(uu, vv) < wrapEntryU) continue;
                if (haversineKm(pp[2 * p], pp[2 * p + 1], lonC, latC) > wrapMaxKm) continue;
                if (uu * tx0 + vv * ty0 <= 0.0) continue;
                nKeep = p + 1;
                break;
            }
            // 退化：找不到同时满足两条的，就取"离涡心最近的点"（至少段2 不会拉长）
            if (nKeep < 2) {
                double bestD = 1.0e30;
                for (size_t p = 0; p < nPts; p++) {
                    double dd = haversineKm(pp[2 * p], pp[2 * p + 1], lonC, latC);
                    if (dd < bestD) { bestD = dd; nKeep = p + 1; }
                }
            }
            if (nKeep < 2) continue;
            const double cutLon = pp[2 * (nKeep - 1)], cutLat = pp[2 * (nKeep - 1) + 1];

            const VortexEdge &E = wrapEdge[k];
            for (size_t b = 0; b < wrapOf[k].size(); b++) {
                const WrapBranch &wb = wrapOf[k][b];
                if (wb.br.pts.size() < 2) continue;
                const double exLon = wb.br.pts[0], exLat = wb.br.pts[1];
                // 【不许回折】落点必须在截断点前方，否则拼出来的线会"先走到下游再绕回来"
                if ((exLon - cutLon) * dirSign < 0.0) continue;

                // 段2：截断点 → 入口 → 沿速度环(min-T 弧) → 出口。
                // **只走弧，不许直连**（2026-11-05 用户定，见 (d) 的注释）。
                // T 由 polylineTravelDays 沿路径逐段积分（不用出口点速度代表整段）。
                std::vector<double> seg2, arc;
                double tArc = 1.0e30;
                ringArcMinT(E, F, wb.iIn, wb.iOut, dtSec, dtDays, arc, tArc);
                if (arc.size() < 4) continue;
                seg2.push_back(cutLon); seg2.push_back(cutLat);
                seg2.insert(seg2.end(), arc.begin(), arc.end());
                const double seg2T = polylineTravelDays(F, seg2, dtSec, dtDays);
                if (!(seg2T > 0.0)) continue;
                nSeg2Arc++;

                SeamtTrace nt;
                nt.pts.assign(pp.begin(), pp.begin() + (long)(2 * nKeep));
                // 段2 折线（跳过与段1 末点重合的起点，避免零长度段）
                for (size_t q = (seg2.size() >= 4 ? 2 : 0); q + 1 < seg2.size(); q += 2) {
                    nt.pts.push_back(seg2[q]);
                    nt.pts.push_back(seg2[q + 1]);
                }
                // 段3（同样跳过与段2 末点重合的起点）
                const size_t s3 = (nt.pts.size() >= 4 &&
                                   std::fabs(wb.br.pts[0] - nt.pts[nt.pts.size() - 2]) < 1.0e-9 &&
                                   std::fabs(wb.br.pts[1] - nt.pts[nt.pts.size() - 1]) < 1.0e-9) ? 2 : 0;
                nt.pts.insert(nt.pts.end(), wb.br.pts.begin() + (long)s3, wb.br.pts.end());
                // T = T₁ + T₂ + T₃。T₃ 就是 wb.br.T ——「出口点到 Γ_out」那一段，
                // 出口选得不同 T₃ 就不同，而 G 的 min-T 比的正是这个总和。
                nt.T      = (double)(nKeep - 1) * dtDays + seg2T + wb.br.T;
                // tHav 要与 T 同口径（自检 tHav_rel 要 <1%），所以段1 的 Σds/|u|
                // 也必须记进去 —— 原来只加了段2/段3，tHav_rel 报 68%。
                nt.tHav   = polylineTravelDays(F, pp, dtSec, dtDays, nKeep)
                            + seg2T + wb.br.tHav;
                nt.nSteps = (int)(nKeep - 1) + wb.br.nSteps;
                nt.reason = wb.br.reason;
                nt.lonOut = wb.br.lonOut; nt.latOut = wb.br.latOut;
                nt.nDegraded = wb.br.nDegraded;
                nt.nBridged  = wb.br.nBridged;
                nt.stalled = false;
                nt.stallLon = nt.stallLat = nanD();
                nt.uStall = nt.vStall = 0.0;
                wrapped.push_back(nt);
                wrappedSeed.push_back(seedLat[i]);
                wrappedKey.push_back((double)k * 16.0 + (double)b);
                wrappedRank.push_back(nt.T);
                wrappedVtx.push_back((int)k);
            }
        }
        // 【内存纪律】同一（涡, 出口）组里只留 T 最小的 wrapKeepPerExit 条。
        //
        // 同组的段3 完全相同（都从同一个出口积分出来），所以 min-T 的胜者必然是
        // T₁+T₂ 最小的那条 —— 留下它就等于留下胜者，其余都是陪跑，丢掉不影响轴。
        // 2026-11-05 实测：不限的话全3.nc 130→160 会拼出 2003 条绕行流线（每条
        // 带一整条折线），densEns 自检（561 点集合）直接 bad allocation。
        {
            std::vector<size_t> order(wrapped.size());
            for (size_t q = 0; q < order.size(); q++) order[q] = q;
            std::sort(order.begin(), order.end(),
                      [&](size_t x, size_t y) {
                          if (wrappedKey[x] != wrappedKey[y]) return wrappedKey[x] < wrappedKey[y];
                          return wrappedRank[x] < wrappedRank[y];
                      });
            std::vector<char> keep(wrapped.size(), 0);
            double lastKey = -1.0;
            int nInGrp = 0;
            for (size_t q = 0; q < order.size(); q++) {
                const size_t idx = order[q];
                if (wrappedKey[idx] != lastKey) { lastKey = wrappedKey[idx]; nInGrp = 0; }
                if (nInGrp >= wrapKeepPerExit) continue;
                nInGrp++;
                keep[idx] = 1;
            }
            std::vector<SeamtTrace> kept;
            std::vector<double> keptSeed;
            std::vector<int> keptVtx;
            for (size_t q = 0; q < wrapped.size(); q++) {
                if (!keep[q]) continue;
                kept.push_back(wrapped[q]);          // 只有留下的做深拷贝，其余就地释放
                keptSeed.push_back(wrappedSeed[q]);
                keptVtx.push_back(wrappedVtx[q]);
            }
            ncdfLog("[seamt][wrap] composed %u wrapped streamlines from %u vortex(es) "
                    "(raw %u, kept <=%d per (vortex,exit); seg2 arc=%d)\n",
                    (unsigned)kept.size(), (unsigned)vN.size(),
                    (unsigned)wrapped.size(), wrapKeepPerExit, nSeg2Arc);
            wrapped.swap(kept);
            wrappedSeed.swap(keptSeed);
            wrappedVtx.swap(keptVtx);
        }
        nWrapped = (int)wrapped.size();
        wrapVortexOf.swap(wrappedVtx);
        if (nWrapped > 0) {
            wrapBase = traces.size();
            traces.insert(traces.end(), wrapped.begin(), wrapped.end());
            seedLat.insert(seedLat.end(), wrappedSeed.begin(), wrappedSeed.end());
        }
    }
    out.nWrapped = nWrapped;

    // 逐条流线记录（诊断用）。必须在这里填，后面会 swap 释放 traces。
    // 每行 **8** 个 double：seedLat, seedLon, reason, nSteps, T, latOut, endLon, endLat。
    // seedLat 是「沿断面坐标 s」（经向下就是纬度）；seedLon 是它对应的经度 ——
    // Γ_in 是纬向断面（Wang 域的 A1）时所有种子纬度相同，没有这一列就没法诊断
    // 「哪颗种子走丢了」。
    out.traceTable.reserve(traces.size() * 8);
    for (size_t i = 0; i < traces.size(); i++) {
        double sLon = 0.0, sLat = 0.0;
        ginPoint(params, seedLat[i], sLon, sLat);
        out.traceTable.push_back(seedLat[i]);
        out.traceTable.push_back(sLon);
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
    {
        int bridgedTotal = 0;
        for (size_t i = 0; i < traces.size(); i++) bridgedTotal += traces[i].nBridged;
        ncdfLog("[seamt] traces=%u  G=%d  reasons EXIT=%d OUTDOMAIN=%d AGROUND=%d MAXSTEP=%d  degraded=%d bridged=%d nearMiss=%d\n",
                (unsigned)traces.size(), out.nSuccess,
                out.reasonCount[SEAMT_EXIT], out.reasonCount[SEAMT_OUTDOMAIN],
                out.reasonCount[SEAMT_AGROUND], out.reasonCount[SEAMT_MAXSTEP],
                out.nDegraded, bridgedTotal, out.nNearMiss);
    }
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

        // 【2026-11-02】把 MAXSTEP 的终点按 0.5° 格子聚类打出来，并带上每个簇的
        // **种子纬度范围** —— 只给 "MAXSTEP=707" 看不出问题，只给终点位置也分不清
        // 是哪一批流线死的。带上种子范围就能立刻看出"急流核那批死在哪"。
        //
        // 全3.nc 135→160 实测：一大批卡在 (153.0°E, 36.3°N)，种子 33.23–33.74°N
        // **正是黑潮核**（135→142 的最优种子 33.4361°N 就在里面）—— 那里是流场的
        // 近零速驻点（|u|=0.018 m/s，45 km 以西是 1.8 m/s 的强射流），射流就地终止。
        // 于是 135→160 的 min-T 只能挑离脊的幸存者，轴在 139–142°E 下凹。
        {
            std::vector<double> binLon, binLat, binSeedMin, binSeedMax;
            std::vector<double> binSumLon, binSumLat;
            std::vector<int> binN;
            for (size_t i = 0; i < traces.size(); i++) {
                if (traces[i].reason != SEAMT_MAXSTEP) continue;
                const std::vector<double> &p = traces[i].pts;
                if (p.size() < 2) continue;
                double lo = std::floor(p[p.size() - 2] * 2.0) / 2.0;
                double la = std::floor(p[p.size() - 1] * 2.0) / 2.0;
                size_t k = 0;
                for (; k < binN.size(); k++)
                    if (binLon[k] == lo && binLat[k] == la) break;
                if (k == binN.size()) {
                    binLon.push_back(lo); binLat.push_back(la);
                    binSeedMin.push_back(seedLat[i]); binSeedMax.push_back(seedLat[i]);
                    binSumLon.push_back(0.0); binSumLat.push_back(0.0);
                    binN.push_back(0);
                }
                binN[k]++;
                binSumLon[k] += p[p.size() - 2];
                binSumLat[k] += p[p.size() - 1];
                if (seedLat[i] < binSeedMin[k]) binSeedMin[k] = seedLat[i];
                if (seedLat[i] > binSeedMax[k]) binSeedMax[k] = seedLat[i];
            }
            std::vector<size_t> ord(binN.size());
            for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
            std::sort(ord.begin(), ord.end(),
                      [&](size_t a, size_t b) { return binN[a] > binN[b]; });
            for (size_t k = 0; k < ord.size() && k < 4; k++) {
                size_t j = ord[k];
                ncdfLog("[seamt]   MAXSTEP cluster %u: n=%-4d around (%.2f, %.2f)  "
                        "seeds %.4f..%.4f N\n",
                        (unsigned)(k + 1), binN[j], binLon[j] + 0.25, binLat[j] + 0.25,
                        binSeedMin[j], binSeedMax[j]);
            }
            // 告警：凡是"同一点吞掉 >=20 条"的簇都报，不限于最大的那个 ——
            // 急流那批未必是条数最多的（全3.nc 里最多的是北边无关流线那批）。
            for (size_t k = 0; k < ord.size() && k < 4; k++) {
                size_t j = ord[k];
                if (binN[j] < 20) continue;
                ncdfLog("[seamt] WARN: %d streamlines (seeds %.4f..%.4f N) all stall around "
                        "(%.3f, %.3f) — near-zero-velocity stagnation; min-T can only pick "
                        "the survivors, so the core may leave the speed ridge upstream of it\n",
                        binN[j], binSeedMin[j], binSeedMax[j],
                        binLon[j] + 0.25, binLat[j] + 0.25);
            }
            // 就地做 HEPTA 式临界点分类（Udalov 2026 §2.3）。
            // 用簇内终点的**均值**当临界点位置 —— 流线都停在那里，均值就是它。
            // 分类结果进 critPoints，报告里另立一节。
            for (size_t k = 0; k < ord.size() && k < 4; k++) {
                size_t j = ord[k];
                if (binN[j] < 5) continue;
                classifyCriticalPoint(F,
                                      binSumLon[j] / (double)binN[j],
                                      binSumLat[j] / (double)binN[j],
                                      binN[j], out.critPoints);
            }
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

    size_t best = 0;
    if (params.singleSeed) {
        // 【singleSeed = 只要那一条流线】不做 min-T 选优，也不接受涡绕行拼出来的
        // 备选（那些会 append 到 traces 尾部、T 可能更小）。2026-11-05 实测踩到：
        // 绕行流线把 best 顶掉，left.core 起点跑到 (133,35.695)，与 P*=32.615
        // 对不上，拼接处凭空多出 340 km 跳变。
        //
        // 【只认能到 lonIn 的】2026-11-05 用户定："到不了 130 的都放弃"。
        // 半截线（如 P* 西延走到 (130.375,31.244) 就撞九州）一律不要。
        //
        // 【2026-11-06：涡绕行可以救它】用户定 130–133 也做涡处理。P* 的西延被涡
        // 吃掉时，绕行拼出来的那条（段1 + 环弧 + 段3）才是它的合理延长，而且可能
        // 真的走到 130。所以 traces[0] 不通时，在**同一条种子的绕行版本**里找一条
        // 通的（同源 = seedLat 相同；候选之间取 T 最小，不是全集合 min-T）。
        {
            size_t pick = (size_t)-1;
            if (!traces.empty() && traces[0].reason == SEAMT_EXIT && traces[0].pts.size() >= 4)
                pick = 0;
            if (pick == (size_t)-1 && !traces.empty()) {
                int nRescued = 0;
                for (size_t i = 1; i < traces.size(); i++) {
                    if (traces[i].reason != SEAMT_EXIT) continue;
                    if (traces[i].pts.size() < 4) continue;
                    if (seedLat[i] != seedLat[0]) continue;      // 只认同源
                    nRescued++;
                    if (pick == (size_t)-1 || traces[i].T < traces[pick].T) pick = i;
                }
                if (pick != (size_t)-1)
                    ncdfLog("[seamt] singleSeed: RESCUED by vortex wrap — original stalled, "
                            "%d wrapped version(s) of the same seed reach lonIn, took the "
                            "smallest T (T=%.4f d)\n", nRescued, traces[pick].T);
            }
            if (pick == (size_t)-1) {
                out.ok = false;
                out.message = wxString::FromUTF8("指定起点反推不到起始面（已放弃）");
                out.elapsedMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
                if (!traces.empty() && traces[0].pts.size() >= 2) {
                    const std::vector<double> &pp = traces[0].pts;
                    ncdfLog("[seamt] singleSeed: REJECTED (reason=%d pts=%u end=(%.3f,%.3f)) "
                            "-> does not reach lonIn (and no wrapped version does either)\n",
                            traces[0].reason, (unsigned)(pp.size() / 2),
                            pp[pp.size() - 2], pp[pp.size() - 1]);
                } else {
                    ncdfLog("[seamt] singleSeed: REJECTED (no usable trace)\n");
                }
                return false;
            }
            best = pick;
        }
    } else if (!G.empty()) {
        // ---------------- [4] 最小 T 即核心 ----------------
        best = G[0];
        for (size_t i = 1; i < G.size(); i++) {
            if (traces[G[i]].T < traces[best].T) best = G[i];
        }
    } else {
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
    out.minT   = traces[best].T;
    out.core   = traces[best].pts;
    {
        // latIn 是**纬度**；seedLat[best] 是沿断面坐标 s（经向下两者相同）
        double inLon = 0.0, inLat = 0.0;
        ginPoint(params, seedLat[best], inLon, inLat);
        out.latIn = inLat;
    }
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

    // 次级射流摘要（供上图与"鞍点分裂"判读）
    out.subjetSummary.clear();
    for (size_t i = 0; i < out.subjets.size(); i++) {
        out.subjetSummary.push_back(out.subjets[i].latOut);
        out.subjetSummary.push_back(out.subjets[i].TDays);
        out.subjetSummary.push_back(std::fabs(out.subjets[i].latOut - out.latOut) < 1.0e-6 ? 1.0 : 0.0);
    }
    // 【鞍点 → 次级射流分裂】论文 §3.2：T(φ_out) 上分隔不同局部极小的区域
    // （次级射流之间的边界）在空间上常与双曲点（鞍点）重合。这里把两边都报出来：
    // 鞍点的不稳定方向（分叉走向）+ 次级射流的出口纬度（分叉结果）。
    {
        int nSaddle = 0, nSink = 0;
        for (size_t i = 0; i + 9 < out.critPoints.size(); i += 10) {
            int ty = (int)out.critPoints[i + 2];
            if (ty == 0) nSaddle++;
            else if (ty == 2 || ty == 4) nSink++;
        }
        ncdfLog("[seamt] criticals: %d saddle(s)  %d sink(s)   |   subjets: %u "
                "(T(φ_out) local minima) — a saddle between two subjets = a split\n",
                nSaddle, nSink, (unsigned)out.subjets.size());
        for (size_t i = 0; i < out.subjets.size(); i++)
            ncdfLog("[seamt]   subjet %u: latOut=%.4f T=%.3f d%s\n",
                    (unsigned)(i + 1), out.subjets[i].latOut, out.subjets[i].TDays,
                    (std::fabs(out.subjets[i].latOut - out.latOut) < 1.0e-6) ? "  <- core" : "");
    }

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

    // ---------------- [5b] 只保留主轴路径上的涡（2026-11-02 用户定）----------------
    //
    // 提取过程中会碰到一大堆临界点（130→142 这次检出 18 个涡），但只有主轴真正
    // 经过/擦过的那几个才跟轴有关。按"到核心线的距离"筛，只留 150 km 以内的，
    // 其余丢掉不画 —— 免得满屏都是无关的涡。鞍点同理（双曲点才是次级射流的分界）。
    {
        // 【2026-11-06 用户定：只显示"真的改变了轴"的涡】
        //
        // 原来是「离轴 150 km 以内」的距离判据，但"离得近"不等于"有影响"。
        // 轴 = argmin_{i∈G} T_i，所以"去掉这个涡的绕行会不会换轴"**不用重跑**就能算：
        // 只有当 min-T 的胜者本身就是这个涡的绕行流线时，去掉它才会换轴。
        // 于是因果判据就是「胜者的 wrapVortexOf == 该涡」。临界点同样只留因果涡附近的。
        const double kVortexOnAxisKm = 150.0;
        int causalVtx = -1;
        if (wrapBase != (size_t)-1 && best >= wrapBase) {
            const size_t wi = best - wrapBase;
            if (wi < wrapVortexOf.size()) causalVtx = wrapVortexOf[wi];
        }
        double causLon = 0.0, causLat = 0.0;
        if (causalVtx >= 0 && (size_t)(4 * causalVtx) + 1 < out.vortexCentres.size()) {
            causLon = out.vortexCentres[(size_t)(4 * causalVtx)];
            causLat = out.vortexCentres[(size_t)(4 * causalVtx) + 1];
        }
        std::vector<double> vc, vs, cp;
        std::vector<int>    cl;
        for (size_t i = 0, off = 0; i < out.vortexCircleLen.size(); i++) {
            const int npts = out.vortexCircleLen[i];
            const size_t ci = 4 * i;
            const bool isCausal = (causalVtx >= 0 && (int)i == causalVtx &&
                                   ci + 3 < out.vortexCentres.size());
            if (isCausal) {
                for (int q = 0; q < 4; q++) vc.push_back(out.vortexCentres[ci + q]);
                for (int q = 0; q < npts * 2; q++)
                    vs.push_back(out.vortexCircles[off + (size_t)q]);
                cl.push_back(npts);
            }
            off += (size_t)npts * 2;
        }
        if (causalVtx >= 0) {
            for (size_t i = 0; i + 9 < out.critPoints.size(); i += 10) {
                const double d = haversineKm(out.critPoints[i], out.critPoints[i + 1],
                                             causLon, causLat);
                if (d <= kVortexOnAxisKm)
                    for (int q = 0; q < 10; q++) cp.push_back(out.critPoints[i + q]);
            }
        }
        ncdfLog("[seamt] vortex display filter (CAUSAL): %u -> %u vortex(es), %u -> %u "
                "critical point(s)  [min-T winner %s wrapped streamline, vortex #%d]\n",
                (unsigned)out.vortexCircleLen.size(), (unsigned)cl.size(),
                (unsigned)(out.critPoints.size() / 10), (unsigned)(cp.size() / 10),
                (causalVtx >= 0) ? "IS a" : "is NOT a", causalVtx + 1);
        out.vortexCentres.swap(vc);
        out.vortexCircles.swap(vs);
        out.vortexCircleLen.swap(cl);
        out.critPoints.swap(cp);
    }

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
        if (!seamtSample(F, core[2 * i], lat, u, v)) continue;

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
