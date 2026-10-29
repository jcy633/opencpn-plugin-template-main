// ncdf_kuroshio.cpp — 流线约束最大流速法（黑潮主轴与左右边界提取）
//
// 忠实移植 main_Kuroshio_axis_and_boundaries.m，并做以下适配：
//   * 掩膜来自数据自身有效值（NaN / ncdf_NOTDEF / 超物理量级），不再依赖 mask.mat
//   * 坐标为任意单调 lat/lon 数组（升/降序、非均匀网格均可）
//   * 流线积分步长 = 1 个网格距（MATLAB stream2 stepsize 的物理解释）
//   * 修复官方代码缺陷：xy_segbei 跨段残留；垂断面 argmax 落到陆地
//   * start_dist 在选优时按 prev_end 重算，避免候选收集顺序影响接续代价
//
// 输出走 KuroshioAxisResult，由 UI 导出 / 渲染，本文件不碰 GL。

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>
#include <atomic>
#include <vector>

#include "ncdf.h"
#include "ncdf_reader.h"
#include "ncdf_kuroshio.h"
#include "ncdf_landmask.h"
#include "ncdf_uvfield.h"

extern void ncdfLog(const char* format, ...);

// 诊断开关：C:\ProgramData\opencpn\ncdf_flags.txt 里写 kuroThreads=N。
// N=1 强制串行，用于 A/B 对照排除并行化对结果的影响；未设或 N<=0 = 自动。
// 与 ncdf_tilegrid.cpp 的 sea/land/fill 开关同一套文件、同一套用法。
// 另可写 kuroCap=N 覆盖候选池上限（MATLAB 原版是不设上限的，见下）。
static int kuroFlagInt(const char *key)
{
    FILE *fp = fopen("C:\\ProgramData\\opencpn\\ncdf_flags.txt", "r");
    if (!fp) return 0;
    char line[256];
    int n = 0;
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *eq = strchr(line, '=');
            if (eq) n = atoi(eq + 1);
            break;
        }
    }
    fclose(fp);
    return n;
}
static int kuroThreadsOverride() { return kuroFlagInt("kuroThreads"); }
static int kuroCapOverride()     { return kuroFlagInt("kuroCap"); }
static int kuroDbg()             { return kuroFlagInt("kuroDbg"); }
static int kuroAvgZero()         { return kuroFlagInt("kuroAvgZero"); }
// 复刻 MATLAB 的"无效格点填 0 后插值"（见 UVField::sampleZeroFill）。
// kuroZeroFill=1 打开；默认 0 = 现有语义（四角有一无效即失败）。
// 这是移植验证用的复刻开关，不代表认可该语义的物理合理性。
static int kuroZeroFill()        { return kuroFlagInt("kuroZeroFill"); }

// 统一取速度入口：按 F.zeroFillInvalid 分派到 sample / sampleZeroFill。
// 参数顺序与 F.sample 保持一致，便于原地替换。
static inline bool sampleF(const UVField &F, double lonq, double latq,
                           double &uo, double &vo)
{
    return F.zeroFillInvalid ? F.sampleZeroFill(lonq, latq, uo, vo)
                             : sampleF(F, lonq, latq, uo, vo);
}
// 评分权重 alpha（×1000）。score = alpha*dist_norm + (1-alpha)*(1-spd_norm)。
// MATLAB 取 alpha=0.5。alpha=1000 → 纯按"离上一段最近"选（诊断用）：
//   若此时 D 段骨架贴近真脊、校正后与参考轴吻合，就证明原 0.5 权重
//   在这个场上把贴岸候选输给了离岸快流线。
static double kuroScoreAlpha() {
    int a = kuroFlagInt("kuroAlpha1000");
    return (a > 0) ? (double)a / 1000.0 : 0.5;
}


// ===========================================================================
// 高斯窗 / 陆海约束平滑（smooth_path_bd）
// ===========================================================================
static void gaussWindow(int n, std::vector<double> &w)
{
    w.assign((size_t)(n < 1 ? 1 : n), 1.0);
    if (n <= 1) return;
    double c = (n - 1) / 2.0;
    for (int k = 0; k < n; k++) {
        double t = (k - c) / c;
        w[(size_t)k] = std::exp(-0.5 * (2.5 * t) * (2.5 * t));
    }
}

static int recommendWindowRadius(const std::vector<double> &path, double targetKm)
{
    size_t n = path.size() / 2;
    if (n < 2) return 2;
    double sum = 0; size_t cnt = 0;
    for (size_t i = 1; i < n; i++) {
        if (isNanD(path[2*i-2]) || isNanD(path[2*i-1]) ||
            isNanD(path[2*i])   || isNanD(path[2*i+1])) continue;
        double d = haversineKm(path[2*i-2], path[2*i-1], path[2*i], path[2*i+1]);
        if (std::isfinite(d)) { sum += d; cnt++; }
    }
    if (cnt == 0) return 2;
    double meanSpacing = sum / (double)cnt;
    if (!(meanSpacing > 0.0)) return 2;
    double r = std::ceil(1.5 * targetKm / meanSpacing);
    if (!std::isfinite(r)) return 2;
    return std::max(2, (int)r);
}

// 按连续海上段做高斯平滑；陆地点权重置 0；removedIdx = 被剔除的点
static void smoothPathBD(const std::vector<double> &path, int windowRadius,
                         const UVField &F, std::vector<double> &out,
                         std::vector<int> &removedIdx)
{
    size_t N = path.size() / 2;
    out.assign(N * 2, nanD());
    removedIdx.clear();
    if (N == 0) return;

    std::vector<char> isSea(N, 0);
    for (size_t i = 0; i < N; i++) {
        if (isNanD(path[2 * i]) || isNanD(path[2 * i + 1])) continue;
        isSea[i] = F.isSea(path[2 * i], path[2 * i + 1]) ? 1 : 0;
    }

    size_t s = 0;
    while (s < N) {
        while (s < N && !isSea[s]) s++;
        if (s >= N) break;
        size_t e = s;
        while (e < N && isSea[e]) e++;
        size_t segN = e - s;
        if (segN < 2) {
            for (size_t k = s; k < e; k++) {
                out[2 * k] = path[2 * k];
                out[2 * k + 1] = path[2 * k + 1];
            }
        } else {
            for (int dim = 0; dim < 2; dim++) {
                for (size_t i = 0; i < segN; i++) {
                    int curR = windowRadius;
                    int maxLeft = (int)i, maxRight = (int)(segN - 1 - i);
                    if (curR > maxLeft) curR = maxLeft;
                    if (curR > maxRight) curR = maxRight;
                    size_t a = s + i - (size_t)curR, b = s + i + (size_t)curR;
                    int nWin = (int)(b - a) + 1;
                    std::vector<double> w;
                    gaussWindow(nWin, w);
                    double wsum0 = 0;
                    for (int k = 0; k < nWin; k++) wsum0 += w[(size_t)k];
                    if (wsum0 <= 0) { out[2 * (s + i) + dim] = nanD(); continue; }
                    for (int k = 0; k < nWin; k++) w[(size_t)k] /= wsum0;
                    double num = 0, den = 0;
                    for (int k = 0; k < nWin; k++) {
                        size_t idx = a + (size_t)k;
                        if (!isSea[idx]) continue;
                        double val = path[2 * idx + dim];
                        if (isNanD(val)) continue;
                        num += val * w[(size_t)k];
                        den += w[(size_t)k];
                    }
                    out[2 * (s + i) + dim] = (den > 0) ? (num / den) : nanD();
                }
            }
        }
        s = e;
    }

    for (size_t i = 0; i < N; i++) {
        bool bad = isNanD(out[2 * i]) || isNanD(out[2 * i + 1]);
        if (!bad && !F.isSea(out[2 * i], out[2 * i + 1])) bad = true;
        if (bad) {
            out[2 * i] = out[2 * i + 1] = nanD();
            removedIdx.push_back((int)i);
        }
    }
}

// 按 removedIdx 删除行（官方：corrected_path(removed_idx,:)=[]）
static void dropRows(std::vector<double> &path, const std::vector<int> &removed)
{
    size_t N = path.size() / 2;
    std::vector<char> drop(N, 0);
    for (size_t k = 0; k < removed.size(); k++)
        if (removed[k] >= 0 && (size_t)removed[k] < N) drop[(size_t)removed[k]] = 1;
    std::vector<double> kept;
    kept.reserve(path.size());
    for (size_t i = 0; i < N; i++) {
        if (drop[i]) continue;
        if (isNanD(path[2 * i]) || isNanD(path[2 * i + 1])) continue;
        kept.push_back(path[2 * i]);
        kept.push_back(path[2 * i + 1]);
    }
    path.swap(kept);
}

// ===========================================================================
// 断面归属 / 环流检测
// ===========================================================================
static void onSection(const std::vector<double> &xy,
                      double lon1, double lat1, double lon2, double lat2,
                      double tol, std::vector<int> &idxOut)
{
    idxOut.clear();
    double vx = lon2 - lon1, vy = lat2 - lat1;
    double vv = vx * vx + vy * vy;
    if (vv <= 0) return;
    size_t n = xy.size() / 2;
    for (size_t i = 0; i < n; i++) {
        double wx = xy[2 * i] - lon1, wy = xy[2 * i + 1] - lat1;
        double b = (wx * vx + wy * vy) / vv;
        if (b < 0) b = 0;
        if (b > 1) b = 1;
        double dx = xy[2 * i] - (lon1 + b * vx);
        double dy = xy[2 * i + 1] - (lat1 + b * vy);
        if (std::sqrt(dx * dx + dy * dy) <= tol) idxOut.push_back((int)i);
    }
}

static bool detectLoops(const std::vector<double> &xy, int minPts, double distThresh)
{
    int n = (int)(xy.size() / 2);
    if (n < minPts * 2) return false;
    for (int i = 0; i < n - minPts; i++) {
        for (int j = i + minPts; j < n; j++) {
            double dx = xy[2 * i] - xy[2 * j], dy = xy[2 * i + 1] - xy[2 * j + 1];
            if (std::sqrt(dx * dx + dy * dy) < distThresh) return true;
        }
    }
    return false;
}

// ===========================================================================
// 断面与分段（论文 Table 1 + Fig.1）
//
// 11 条唯一断面（纬向或斜向线段），A–E 五段各由三条断面围成：
//   段 s 的 south = 断面 2s，mid = 2s+1，north = 2s+2
// 相邻段共用断面（A2/B1、B2/C1、C2/D1、D2/E1 是同一条），因此全库只有 11 条。
// 坐标逐字取自论文 Table 1，未作改动。
// ===========================================================================
struct Section {
    char   name[8];
    double lon1, lat1, lon2, lat2;
};

static const Section kSections[11] = {
    { "A1",    122.00, 16.000, 124.00, 16.000 },   // 0  A.south
    { "A3",    122.22, 17.500, 123.50, 17.500 },   // 1  A.mid (fallback)
    { "A2/B1", 120.85, 19.000, 123.00, 19.000 },   // 2  A.north = B.south
    { "B3",    118.00, 20.500, 123.00, 20.500 },   // 3  B.mid (fallback)
    { "B2/C1", 120.85, 22.000, 123.00, 22.000 },   // 4  B.north = C.south
    { "C3",    121.49, 23.500, 123.50, 23.500 },   // 5  C.mid (fallback)
    { "C2/D1", 122.00, 25.000, 124.00, 25.000 },   // 6  C.north = D.south
    { "D3",    125.00, 27.375, 126.85, 26.145 },   // 7  D.mid (fallback, 斜断面)
    { "D2/E1", 126.00, 29.000, 127.85, 27.770 },   // 8  D.north = E.south (斜断面)
    { "E3",    127.00, 30.625, 128.85, 29.395 },   // 9  E.mid (fallback, 斜断面)
    { "E2",    130.38, 28.250, 130.38, 31.250 },   // 10 E.north (经向断面)
};

// 段盒（论文：五段各约 3 个纬度带；经度范围为官方代码的 lon_range / lat_lim）
struct SegDef {
    char   name;
    double latLim[2];
    double lonRange[2];
    int    iSouth, iMid, iNorth;   // 断面下标
};

static const SegDef kSegs[5] = {
    { 'A', {16.0, 19.0},  {120.85, 124.0},  0, 1, 2 },   // origin zone
    { 'B', {19.0, 22.0},  {118.0, 123.0},   2, 3, 4 },   // Luzon Strait
    { 'C', {22.0, 25.0},  {120.85, 124.0},  4, 5, 6 },   // east of Taiwan
    { 'D', {25.0, 29.0},  {122.0, 127.85},  6, 7, 8 },   // upstream ECS
    { 'E', {27.77, 31.25},{126.0, 130.38},  8, 9, 10 },  // downstream ECS
};

// ===========================================================================
// 候选子段
// ===========================================================================
struct Cand {
    std::vector<double> xy;
    double avgSpeed;
};

struct CandList {
    std::vector<Cand> v;
    size_t cap;
    explicit CandList(size_t cap_ = 1500) : cap(cap_) {}
    void add(Cand &c) {
        if (c.xy.size() < 4) return;
        if (v.size() < cap) { v.push_back(Cand()); v.back().xy.swap(c.xy); v.back().avgSpeed = c.avgSpeed; return; }
        // 容量已满：仅当更快时替换最慢者（保证候选池不劣化）
        size_t worst = 0;
        for (size_t i = 1; i < v.size(); i++)
            if (v[i].avgSpeed < v[worst].avgSpeed) worst = i;
        if (c.avgSpeed > v[worst].avgSpeed) {
            v[worst].xy.swap(c.xy);
            v[worst].avgSpeed = c.avgSpeed;
        }
    }
    void clear() { v.clear(); }
    bool empty() const { return v.empty(); }
    size_t size() const { return v.size(); }
};

// pick_best_candidate：start_dist 在选优时按 prev_end 重算
//   score = 0.5*norm(start_dist) + 0.5*(1-norm(avg_speed))  -> 取最小

// 候选池倒出（kuroDbg=1）：判定"贴陆架的候选"是没进池（流线生成问题）
// 还是进了池但输给离岸候选（选优问题）。两者修法完全不同。
static void dumpCands(const char *tag, const CandList &cl,
                      double prevLon, double prevLat, bool havePrev,
                      const UVField &F)
{
    if (!kuroDbg()) return;
    size_t n = cl.v.size();
    if (n == 0) { ncdfLog("[kuro]   cands %s: EMPTY\n", tag); return; }

    struct Row { double score, dN, sN, sp; size_t i; };
    std::vector<Row> rows(n);
    double dMin = 1e300, dMax = -1e300, sMin = 1e300, sMax = -1e300;
    for (size_t i = 0; i < n; i++) {
        double d = havePrev ? std::hypot(cl.v[i].xy[0] - prevLon, cl.v[i].xy[1] - prevLat) : 0.0;
        rows[i].score = d; rows[i].sp = cl.v[i].avgSpeed; rows[i].i = i;
        if (d < dMin) dMin = d;
        if (d > dMax) dMax = d;
        if (cl.v[i].avgSpeed < sMin) sMin = cl.v[i].avgSpeed;
        if (cl.v[i].avgSpeed > sMax) sMax = cl.v[i].avgSpeed;
    }
    double dSpan = (n > 1) ? (dMax - dMin) : 1.0;
    double sSpan = (n > 1) ? (sMax - sMin) : 1.0;
    if (dSpan < 1e-12) dSpan = 1e-12;
    if (sSpan < 1e-12) sSpan = 1e-12;
    std::vector<double> scByOrig(n);      // 以下标（原候选序）索引的 score
    const double alpha = kuroScoreAlpha();
    for (size_t i = 0; i < n; i++) {
        rows[i].dN = (n > 1) ? (rows[i].score - dMin) / dSpan : 0.0;
        rows[i].sN = (n > 1) ? (rows[i].sp - sMin) / sSpan : 1.0;
        rows[i].score = alpha * rows[i].dN + (1.0 - alpha) * (1.0 - rows[i].sN);
        scByOrig[rows[i].i] = rows[i].score;
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row &a, const Row &b) { return a.score < b.score; });
    ncdfLog("[kuro]   cands %s: n=%u  (dSpan=%.3fdeg sSpan=%.3f)\n",
            tag, (unsigned)n, dSpan, sSpan);
    size_t show = (n < 8) ? n : 8;
    for (size_t k = 0; k < show; k++) {
        const std::vector<double> &p = cl.v[rows[k].i].xy;
        size_t m = p.size() / 2;
        ncdfLog("[kuro]     #%u score=%.3f dN=%.3f sN=%.3f sp=%.2f  "
                "(%.3f,%.3f)->(%.3f,%.3f) n=%u\n",
                (unsigned)k, rows[k].score, rows[k].dN, rows[k].sN, rows[k].sp,
                p[0], p[1], p[2 * (m - 1)], p[2 * (m - 1) + 1], (unsigned)m);
    }
    // 起点经度直方图（每 0.5° 一格）：判"贴岸候选在不在池里、它分数多少"。
    // 若某格有候选但 min score 明显高于赢家，说明是**选优**问题；
    // 若某格根本没有候选，说明是**流线生成**问题（流线到不了那里）。
    {
        double lo = 1e300, hi = -1e300;
        for (size_t i = 0; i < n; i++) {
            double x = cl.v[i].xy[0];
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        const int NB = 24;
        double w = (hi - lo) / NB;
        if (w > 1e-9) {
            std::vector<double> bestSc(NB, 1e300), bestSp(NB, -1.0);
            std::vector<int> cnt(NB, 0), bestIdx(NB, -1);
            for (size_t i = 0; i < n; i++) {
                int b = (int)((cl.v[i].xy[0] - lo) / w);
                if (b < 0) b = 0;
                if (b >= NB) b = NB - 1;
                cnt[b]++;
                if (scByOrig[i] < bestSc[b]) {
                    bestSc[b] = scByOrig[i];
                    bestSp[b] = cl.v[i].avgSpeed;
                    bestIdx[b] = (int)i;
                }
            }
            ncdfLog("[kuro]     start-lon histogram (bin=%.2f deg):\n", w);
            for (int b = 0; b < NB; b++) {
                if (!cnt[b]) continue;
                ncdfLog("[kuro]       lon %6.2f..%6.2f  n=%-5d bestScore=%.3f bestSp=%.3f\n",
                        lo + b * w, lo + (b + 1) * w, cnt[b],
                        (bestSc[b] > 1e299) ? -1.0 : bestSc[b], bestSp[b]);
            }
            // 冠军 vs 最靠岸那一格的最优候选：逐点流速剖面
            //   —— 判定"贴岸候选的均速真的低"还是"我们的 avgSpeed 算错了"
            auto dumpProf = [&F](const char *who, const Cand &c) {
                size_t m = c.xy.size() / 2;
                ncdfLog("[kuro]     profile %s: avgSpeed=%.3f  n=%u  "
                        "(%.3f,%.3f)->(%.3f,%.3f)\n", who, c.avgSpeed, (unsigned)m,
                        c.xy[0], c.xy[1], c.xy[2 * (m - 1)], c.xy[2 * (m - 1) + 1]);
                size_t stride = (m > 8) ? m / 8 : 1;
                for (size_t k = 0; k < m; k += stride) {
                    double u, v;
                    bool ok = sampleF(F, c.xy[2 * k], c.xy[2 * k + 1], u, v);
                    ncdfLog("[kuro]        [%3u] (%.3f,%.3f) |u|=%s%.3f\n",
                            (unsigned)k, c.xy[2 * k], c.xy[2 * k + 1],
                            ok ? "" : "FAIL ", ok ? std::hypot(u, v) : -1.0);
                }
            };
            dumpProf("winner", cl.v[rows[0].i]);
            for (int b = 0; b < NB; b++) {
                if (cnt[b] > 0 && bestIdx[b] >= 0) {
                    dumpProf("shoreward-best", cl.v[(size_t)bestIdx[b]]);
                    break;
                }
            }
        }
    }
}

// 逐阶段折线采样：定位偏差是哪一步引入的（拼装 / 平滑 / 垂断面校正）。
// 只在 kuroDbg=1 时输出。诊断输出必须紧跟它要展示的那个步骤之后。
static void logPolySample(const char *tag, const std::vector<double> &p, int nSample)
{
    if (!kuroDbg()) return;
    size_t n = p.size() / 2;
    if (n < 2) { ncdfLog("[kuro]   %s: empty (%u pts)\n", tag, (unsigned)n); return; }
    size_t stride = (n > (size_t)nSample) ? n / (size_t)nSample : 1;
    ncdfLog("[kuro]   %s (%u pts):\n", tag, (unsigned)n);
    for (size_t i = 0; i < n; i += stride)
        ncdfLog("[kuro]     [%4u] (%.4f, %.4f)\n", (unsigned)i, p[2 * i], p[2 * i + 1]);
}
static bool pickBest(const CandList &cands, double prevLon, double prevLat, bool havePrev, Cand &best)
{
    size_t n = cands.v.size();
    if (n == 0) return false;

    std::vector<double> dist(n, 0.0);
    double dMin = 1e300, dMax = -1e300, sMin = 1e300, sMax = -1e300;
    for (size_t i = 0; i < n; i++) {
        dist[i] = havePrev ? std::hypot(cands.v[i].xy[0] - prevLon, cands.v[i].xy[1] - prevLat) : 0.0;
        if (dist[i] < dMin) dMin = dist[i];
        if (dist[i] > dMax) dMax = dist[i];
        if (cands.v[i].avgSpeed < sMin) sMin = cands.v[i].avgSpeed;
        if (cands.v[i].avgSpeed > sMax) sMax = cands.v[i].avgSpeed;
    }
    double dSpan = (n > 1) ? (dMax - dMin) : 1.0;   // 官方：单元素时 dist_norm=0
    double sSpan = (n > 1) ? (sMax - sMin) : 1.0;
    if (dSpan < 1e-12) dSpan = 1e-12;
    if (sSpan < 1e-12) sSpan = 1e-12;

    double bestScore = 1e300;
    size_t bestIdx = 0;
    const double alpha = kuroScoreAlpha();   // MATLAB = 0.5；kuroAlpha1000 可覆盖（诊断）
    for (size_t i = 0; i < n; i++) {
        double dN = (n > 1) ? (dist[i] - dMin) / dSpan : 0.0;
        double sN = (n > 1) ? (cands.v[i].avgSpeed - sMin) / sSpan : 1.0;
        double score = alpha * dN + (1.0 - alpha) * (1.0 - sN);
        if (score < bestScore) { bestScore = score; bestIdx = i; }
    }
    best.xy = cands.v[bestIdx].xy;
    best.avgSpeed = cands.v[bestIdx].avgSpeed;
    return true;
}

// ===========================================================================
// 双向流线追踪（stream2 等价：RK2 中点法，步长 = 1 网格距）
// ===========================================================================
struct TraceBounds {
    double lonMin, lonMax, latMin, latMax;
    bool inside(double lo, double la) const {
        return lo >= lonMin && lo <= lonMax && la >= latMin && la <= latMax;
    }
};

static void integrateOneWay(const UVField &F, double lon0, double lat0,
                            double sign, int maxPts, const TraceBounds &tb,
                            std::vector<double> &out)
{
    out.clear();
    out.reserve((size_t)maxPts * 2);
    double lon = lon0, lat = lat0;
    out.push_back(lon); out.push_back(lat);

    for (int step = 1; step < maxPts; step++) {
        double u1, v1;
        if (!sampleF(F, lon, lat, u1, v1)) break;
        double sp = std::hypot(u1, v1);
        if (sp < 1e-9) break;

        // h 使空间位移恰为一个网格距（stepsize = 1 个网格距的语义）
        double h = F.cellDeg / sp;
        double mLon = lon + sign * u1 * h * 0.5;
        double mLat = lat + sign * v1 * h * 0.5;

        // 中点法；中点落在陆地/域外时退化为一阶欧拉步，避免近岸被过早截断
        double u2 = u1, v2 = v1;
        if (sampleF(F, mLon, mLat, u2, v2)) {
            if (std::hypot(u2, v2) < 1e-9) break;
        } else {
            u2 = u1; v2 = v1;
        }

        double nLon = lon + sign * u2 * h;
        double nLat = lat + sign * v2 * h;
        if (!tb.inside(nLon, nLat)) break;      // 远离流廊道，后续段校验必然失败
        if (!F.isSea(nLon, nLat)) break;

        lon = nLon; lat = nLat;
        out.push_back(lon); out.push_back(lat);
    }
}

// ===========================================================================
// 候选子段提取
//   idxA / idxB : 由调用方预先算好的两条断面命中下标（同一断面只算一次）
//   backward    : true = 从后向前扫 A 交点（主路径语义），false = 前向
//   breakAfter  : 找到第一个有效子段即停（主路径 true；降级 false，与官方一致）
// ===========================================================================
static void collectCandidates(const std::vector<double> &xy, const SegDef &seg,
                              const std::vector<int> &idxA, const std::vector<int> &idxB,
                              const UVField &F,
                              bool backward, bool breakAfter,
                              int minLoopPts, double loopDist,
                              CandList &out)
{
    if (idxA.empty() || idxB.empty()) return;

    // 诊断（kuroDbg=1）：候选为什么这么少 —— 卡在「没跨断面」还是「validate 被拒」，
    // 两者修法完全不同。B 段实测主路径只有 1 个候选，靠这个分类定位。
    struct { int seeds, noB, noNorthAfter, boxRej, loopRej, added, brk; } st = {};
    int nA = (int)idxA.size();
    for (int ii = 0; ii < nA; ii++) {
        int s = idxA[backward ? (size_t)(nA - 1 - ii) : (size_t)ii];
        int e = -1;
        for (size_t k = 0; k < idxB.size(); k++) {
            if (idxB[k] > s) { e = idxB[k]; break; }
        }
        st.seeds++;
        if (idxB.empty()) { st.noB++; continue; }
        if (e < 0) { st.noNorthAfter++; continue; }

        Cand c;
        c.xy.assign(xy.begin() + 2 * s, xy.begin() + 2 * e + 2);

        // validate_streamline：盒子约束 + 环流检测
        bool valid = true;
        for (size_t k = 0; k < c.xy.size() / 2 && valid; k++) {
            double lo = c.xy[2 * k], la = c.xy[2 * k + 1];
            if (lo < seg.lonRange[0] || lo > seg.lonRange[1] ||
                la < seg.latLim[0]   || la > seg.latLim[1]) valid = false;
        }
        if (!valid) { st.boxRej++; continue; }
        if (detectLoops(c.xy, minLoopPts, loopDist)) { st.loopRej++; continue; }

        // avg_speed：MATLAB 是 mean(hypot(uu,vv), 'omitnan')，而 Fu/Fv 建在
        // **零填充**的网格上（sub_u(isnan|land)=0）—— 所以无效格点以 0 计入均值，
        // 不是被跳过。我们原先只统计有效点，口径不同，而 avgSpeed 正是
        // pickBest 评分的一项。kuroAvgZero=1 复刻 MATLAB 口径。
        double sum = 0.0;
        int cnt = (int)(c.xy.size() / 2);
        for (size_t k = 0; k < c.xy.size() / 2; k++) {
            double uu, vv;
            if (sampleF(F, c.xy[2 * k], c.xy[2 * k + 1], uu, vv)) sum += std::hypot(uu, vv);
            // 取不到速度的点：kuroAvgZero 时按 0 计（复刻 MATLAB）；否则不计分母
        }
        if (!kuroAvgZero()) {
            int valid = 0;
            for (size_t k = 0; k < c.xy.size() / 2; k++) {
                double uu, vv;
                if (sampleF(F, c.xy[2 * k], c.xy[2 * k + 1], uu, vv)) valid++;
            }
            cnt = valid;
        }
        c.avgSpeed = (cnt > 0) ? sum / (double)cnt : 0.0;
        out.add(c);
        st.added++;
        if (breakAfter) { st.brk++; break; }
    }
    if (kuroDbg()) {
        ncdfLog("[kuro]     collect(%s%c seg=%c): seeds=%d added=%d | noNorthAfter=%d "
                "boxRej=%d loopRej=%d brk=%d\n",
                backward ? "backward" : "forward", breakAfter ? '+' : '-', seg.name,
                st.seeds, st.added, st.noNorthAfter, st.boxRej, st.loopRej, st.brk);
    }
}

// ===========================================================================
// correct_axis_transect：垂断面 ±halfLenKm 取沿流流速极大点
// ===========================================================================
static void correctAxisTransect(const UVField &F, const std::vector<double> &ref,
                                double halfLenKm, int nSect,
                                double smoothFinalKm, std::vector<double> &out)
{
    size_t N = ref.size() / 2;
    out.clear();
    if (N == 0) return;

    std::vector<double> raw(N * 2, nanD());
    for (size_t i = 0; i < N; i++) {
        double rlon = ref[2 * i], rlat = ref[2 * i + 1];
        if (isNanD(rlon) || isNanD(rlat)) continue;
        double u0, v0;
        if (!sampleF(F, rlon, rlat, u0, v0) || (u0 == 0.0 && v0 == 0.0)) {
            raw[2 * i] = rlon; raw[2 * i + 1] = rlat;
            continue;
        }
        double thetaRef = std::atan2(v0, u0);

        double degLat = halfLenKm / kKmPerDeg;
        double cLat = std::cos(rlat * M_PI / 180.0);
        if (cLat < 1e-6) cLat = 1e-6;
        double degLon = halfLenKm / (kKmPerDeg * cLat);

        double perp = thetaRef + M_PI / 2.0;
        double deltaLon = degLon * std::cos(perp);
        double deltaLat = degLat * std::sin(perp);

        // 垂断面取沿流投影最大点。
        //
        // 【与 MATLAB 原版的一处实质差异】官方是
        //     V_proj_masked(mask_sect < 1) = 0;
        //     [~, idx_max] = max(V_proj_masked, [], 'omitnan');
        // 无效点置 **0** —— 当断面上所有有效点的沿流投影都是负值时，
        // argmax 会落到陆地/无效点，轴线被拉过去。
        // 我们当初判为官方 bug 改成置 -inf（强制选有效点）。
        // **但发布数据集是用原版生成的**，验证移植正确性要先复刻原版行为，
        // 故提供 kuroTransectZero=1 切回置 0 的语义做 A/B。
        const bool matlabZero = (kuroFlagInt("kuroTransectZero") != 0);
        double bestV = -std::numeric_limits<double>::infinity();
        double bestLon = rlon, bestLat = rlat;
        bool got = false;
        for (int k = 0; k < nSect; k++) {
            double t = (nSect > 1) ? (double)k / (double)(nSect - 1) : 0.5;
            double lo = (rlon - deltaLon) + 2.0 * deltaLon * t;
            double la = (rlat - deltaLat) + 2.0 * deltaLat * t;
            double us, vs;
            double vp;
            bool valid = F.isSea(lo, la) && sampleF(F, lo, la, us, vs);
            if (valid)      vp = us * std::cos(thetaRef) + vs * std::sin(thetaRef);
            else if (matlabZero) vp = 0.0;      // 官方语义：无效点置 0，可能被 argmax 选中
            else                 vp = -std::numeric_limits<double>::infinity();
            if (vp > bestV) { bestV = vp; bestLon = lo; bestLat = la; got = true; }
        }
        if (got && std::isfinite(bestV)) {
            raw[2 * i] = bestLon; raw[2 * i + 1] = bestLat;   // alpha = 1 硬吸附
        } else {
            raw[2 * i] = rlon; raw[2 * i + 1] = rlat;
        }
    }

    // 官方：transition_radius = recommend_window_radius(ref_path, 110)，
    // 在校正前的骨架上取窗（不是在 raw 上）。
    int radius = recommendWindowRadius(ref, smoothFinalKm);
    std::vector<int> removed;
    smoothPathBD(raw, radius, F, out, removed);
    dropRows(out, removed);
}

// ===========================================================================
// cut_reference_axis
// ===========================================================================
static void cutReferenceAxis(std::vector<double> &path, double latMin, double lonMax)
{
    size_t N = path.size() / 2;
    if (N == 0) return;
    size_t idxStart = 0; bool found = false;
    for (size_t i = 0; i < N; i++) {
        if (path[2 * i + 1] >= latMin) { idxStart = i; found = true; break; }
    }
    if (!found) idxStart = N - 1;

    size_t idxEnd = idxStart; bool foundEnd = false;
    for (size_t i = idxStart + 1; i < N; i++)
        if (path[2 * i] <= lonMax) { idxEnd = i; foundEnd = true; }
    if (!foundEnd) {
        double best = 1e300;
        for (size_t i = idxStart + 1; i < N; i++) {
            double d = std::fabs(path[2 * i] - lonMax);
            if (d < best) { best = d; idxEnd = i; }
        }
        if (idxEnd < idxStart) idxEnd = idxStart;
    }
    std::vector<double> kept(path.begin() + 2 * idxStart, path.begin() + 2 * idxEnd + 2);
    path.swap(kept);
}

// ===========================================================================
// 左右边界
// ===========================================================================
static void searchBoundaryKM(const UVField &F, double lon0, double lat0,
                             double dirLon, double dirLat,
                             double threshold, double maxDistKm, double stepKm,
                             double tLon, double tLat, bool checkDirection,
                             double &outLon, double &outLat)
{
    outLon = outLat = nanD();
    double lastLon = nanD(), lastLat = nanD();
    double farLon = nanD(), farLat = nanD();
    double farDist = 0.0;

    int steps = (int)std::floor(maxDistKm / stepKm);
    if (steps < 1) steps = 1;

    for (int s = 1; s <= steps; s++) {
        double latT = lat0 + dirLat * ((stepKm * s) / kKmPerDeg);
        double kmPerDegLon = kKmPerDeg * std::cos(latT * M_PI / 180.0);
        if (kmPerDegLon < 1e-6) kmPerDegLon = 1e-6;
        double lonT = lon0 + (stepKm * s * dirLon) / kmPerDegLon;

        double uT, vT;
        if (!F.isSea(lonT, latT) || !sampleF(F, lonT, latT, uT, vT)) break;   // NaN / 陆地 -> 终止

        lastLon = lonT; lastLat = latT;

        double vProj = uT * tLon + vT * tLat;
        if (checkDirection) {
            double sp = std::hypot(uT, vT);
            if (sp <= 0) continue;
            if (vProj / sp <= 0) continue;             // 反向流（逆流/再循环）不计
            double dist = std::hypot(lonT - lon0, latT - lat0);
            if (dist > farDist) { farDist = dist; farLon = lonT; farLat = latT; }
            if (std::fabs(vProj) <= threshold) { outLon = lonT; outLat = latT; return; }
        } else {
            if (std::fabs(vProj) <= threshold) { outLon = lonT; outLat = latT; return; }
        }
    }

    if (isNanD(outLon)) {
        if (!isNanD(farLon))      { outLon = farLon; outLat = farLat; }
        else if (!isNanD(lastLon)){ outLon = lastLon; outLat = lastLat; }
        else                      { outLon = lon0;   outLat = lat0; }
    }
}

static void findKuroshioBoundaries(const UVField &F, const std::vector<double> &axis,
                                   double pctSpeed, double maxDistKm, double stepKm,
                                   double smoothFinalKm,
                                   std::vector<double> &left, std::vector<double> &right)
{
    size_t N = axis.size() / 2;
    left.assign(N * 2, nanD());
    right.assign(N * 2, nanD());
    if (N == 0) return;

    for (size_t i = 0; i < N; i++) {
        double lon0 = axis[2 * i], lat0 = axis[2 * i + 1];
        if (isNanD(lon0) || isNanD(lat0)) continue;
        double u0, v0;
        if (!sampleF(F, lon0, lat0, u0, v0) || (u0 == 0.0 && v0 == 0.0)) continue;

        double nrm = std::hypot(u0, v0);
        double t1 = u0 / nrm, t2 = v0 / nrm;
        double thr = std::fabs(u0 * t1 + v0 * t2) * pctSpeed;

        double llon, llat, rlon, rlat;
        searchBoundaryKM(F, lon0, lat0, -t2,  t1, thr, maxDistKm, stepKm, t1, t2, true, llon, llat);
        searchBoundaryKM(F, lon0, lat0,  t2, -t1, thr, maxDistKm, stepKm, t1, t2, true, rlon, rlat);
        left[2 * i] = llon;  left[2 * i + 1] = llat;
        right[2 * i] = rlon; right[2 * i + 1] = rlat;
    }

    std::vector<int> rem;
    std::vector<double> tmp;

    smoothPathBD(left, recommendWindowRadius(left, smoothFinalKm), F, tmp, rem);
    left.swap(tmp);
    for (size_t k = 0; k < rem.size(); k++)
        if ((size_t)rem[k] < N) { left[2 * rem[k]] = left[2 * rem[k] + 1] = nanD(); }

    smoothPathBD(right, recommendWindowRadius(right, smoothFinalKm), F, tmp, rem);
    right.swap(tmp);
    for (size_t k = 0; k < rem.size(); k++)
        if ((size_t)rem[k] < N) { right[2 * rem[k]] = right[2 * rem[k] + 1] = nanD(); }
}

// drop_decreasing_lat：最大纬度点前强制纬度递增，之后强制经度递增 + 缺口插值
static void dropDecreasingLat(std::vector<double> &pts)
{
    size_t N = pts.size() / 2;
    if (N < 2) return;

    std::vector<char> process(N, 0);
    bool any = false;
    for (size_t i = 0; i < N; i++)
        if (pts[2 * i] > 120.85) { process[i] = 1; any = true; }
    if (!any) return;

    std::vector<double> west(pts);

    double maxLat = -1e300; size_t maxIdx = 0; bool haveMax = false;
    for (size_t i = 0; i < N; i++) {
        if (!process[i] || isNanD(pts[2 * i]) || isNanD(pts[2 * i + 1])) continue;
        if (!haveMax || pts[2 * i + 1] > maxLat) { maxLat = pts[2 * i + 1]; maxIdx = i; haveMax = true; }
    }
    if (!haveMax) return;

    // 与官方一致：lastLat 取 pts(1,2)，i 从 2 扫到 maxIdx（下标 1..maxIdx）
    double lastLat = isNanD(pts[1]) ? -1e300 : pts[1];
    for (size_t i = 1; i <= maxIdx; i++) {
        if (!process[i] || isNanD(pts[2 * i])) continue;
        if (pts[2 * i + 1] <= lastLat) pts[2 * i] = pts[2 * i + 1] = nanD();
        else lastLat = pts[2 * i + 1];
    }

    // lastLon 取 pts(maxIdx,1)，i 从 maxIdx+1 扫到 N
    double lastLon = isNanD(pts[2 * maxIdx]) ? -1e300 : pts[2 * maxIdx];
    for (size_t i = maxIdx + 1; i < N; i++) {
        if (!process[i] || isNanD(pts[2 * i])) continue;
        if (pts[2 * i] <= lastLon) pts[2 * i] = pts[2 * i + 1] = nanD();
        else lastLon = pts[2 * i];
    }

    size_t i = 0;
    while (i < N) {
        if (process[i] && (isNanD(pts[2 * i]) || isNanD(pts[2 * i + 1]))) {
            size_t gs = i;
            while (i < N && process[i] && (isNanD(pts[2 * i]) || isNanD(pts[2 * i + 1]))) i++;
            size_t ge = i - 1;
            if (gs > 0 && ge + 1 < N &&
                !isNanD(pts[2 * (gs - 1)]) && !isNanD(pts[2 * (gs - 1) + 1]) &&
                !isNanD(pts[2 * (ge + 1)]) && !isNanD(pts[2 * (ge + 1) + 1])) {
                double preLon = pts[2 * (gs - 1)], preLat = pts[2 * (gs - 1) + 1];
                double postLon = pts[2 * (ge + 1)], postLat = pts[2 * (ge + 1) + 1];
                for (size_t j = gs; j <= ge; j++) {
                    double t = (double)(j - gs + 1) / (double)(ge - gs + 2);
                    pts[2 * j] = (1 - t) * preLon + t * postLon;
                    pts[2 * j + 1] = (1 - t) * preLat + t * postLat;
                }
            }
        } else {
            i++;
        }
    }

    for (size_t k = 0; k < N; k++)
        if (!process[k]) { pts[2 * k] = west[2 * k]; pts[2 * k + 1] = west[2 * k + 1]; }
}

// mask_side_flip：保证左右边界不串位
static void maskSideFlip(const std::vector<double> &ref,
                         std::vector<double> &left, std::vector<double> &right)
{
    size_t N = ref.size() / 2;
    for (size_t i = 0; i < N; i++) {
        if (isNanD(ref[2 * i]) || isNanD(ref[2 * i + 1])) continue;
        if (isNanD(left[2 * i]) || isNanD(left[2 * i + 1])) continue;
        if (isNanD(right[2 * i]) || isNanD(right[2 * i + 1])) continue;

        double tx, ty;
        if (i + 1 < N) {
            tx = ref[2 * (i + 1)] - ref[2 * i];
            ty = ref[2 * (i + 1) + 1] - ref[2 * i + 1];
        } else {
            if (i == 0) continue;
            tx = ref[2 * i] - ref[2 * (i - 1)];
            ty = ref[2 * i + 1] - ref[2 * (i - 1) + 1];
        }
        double nrm = std::hypot(tx, ty);
        if (nrm <= 0) continue;
        tx /= nrm; ty /= nrm;

        double sideL = (left[2 * i] - ref[2 * i]) * (-ty) + (left[2 * i + 1] - ref[2 * i + 1]) * (tx);
        double sideR = (right[2 * i] - ref[2 * i]) * (-ty) + (right[2 * i + 1] - ref[2 * i + 1]) * (tx);
        if (sideL <= 0) left[2 * i] = left[2 * i + 1] = nanD();
        if (sideR >= 0) right[2 * i] = right[2 * i + 1] = nanD();
    }
}

// ===========================================================================
// 构造 / 默认参数
// ===========================================================================
KuroshioExtractor::KuroshioExtractor()
{
    seedSpacingDeg = 0.0;       // <=0 : 取数据网格距（论文 = 1/12°）
    sectionTolDeg = 0.5;
    smoothRefKm = 55.0;
    smoothFinalKm = 110.0;
    halfLenKm = 70.0;           // 论文 "a 140-km transect" = ±70 km
    nSect = 21;                 // ±70 km 上 7 km 一个采样 -> 21 点（论文 "7 km intervals"）
    pctSpeed = 0.30;
    maxDistKm = 70.0;
    stepKm = 7.0;
    cutLatMin = 16.0;
    cutLonMax = 130.38;
    maxStreamPoints = 800;
    wantBoundaries = true;
    minLoopPoints = 8;
    loopDistDeg = 0.15;
}

void KuroshioExtractor::defaultDomain(double &latMin, double &latMax,
                                      double &lonMin, double &lonMax)
{
    latMin = 15.0; latMax = 32.0;
    lonMin = 115.0; lonMax = 132.0;
}

// ===========================================================================
// 主流程
// ===========================================================================
bool KuroshioExtractor::extract(const ncdfDataMessage &msg, KuroshioAxisResult &out)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    out.clear();

    // 分段计时：总耗时看不出瓶颈在哪，提速前后都需要（2026-10-29 加）
    auto tPh = t0;
    auto phaseMs = [&tPh]() -> long long {
        auto now = std::chrono::high_resolution_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - tPh).count();
        tPh = now;
        return ms;
    };

    if (!msg.hasCurrent() || !msg.latValues || !msg.lonValues ||
        msg.latLength < 4 || msg.lonLength < 4) {
        out.message = wxString::FromUTF8("数据无效：需要已加载的 U/V 流场与坐标");
        return false;
    }

    UVField F;
    F.bind(msg);
    F.lm = m_landMask;
    // kuroZeroFill=1 时复刻 MATLAB 的"无效格点填 0 后插值"语义（移植验证用）。
    // 默认 false：保持 sample() 的"四角有一无效即失败"。
    F.zeroFillInvalid = (kuroZeroFill() != 0);
    if (F.zeroFillInvalid)
        ncdfLog("[kuro] zeroFill mode ON (MATLAB semantics: invalid cells -> 0)\n");
    if (F.lm && F.lm->valid()) {
        ncdfLog("[kuro] land mask: %s  %dx%d  lat %.2f..%.2f lon %.2f..%.2f\n",
                (const char*)F.lm->sourcePath().mb_str(), F.lm->nlat(), F.lm->nlon(),
                F.lm->latMin(), F.lm->latMax(), F.lm->lonMin(), F.lm->lonMax());
    } else {
        ncdfLog("[kuro] land mask: none (using data validity as land mask)\n");
    }
    ncdfLog("[kuro] extract: grid lat=%d lon=%d cell=%.4f deg latAsc=%d lonAsc=%d\n",
            (int)F.nlat, (int)F.nlon, F.cellDeg, (int)F.latAsc, (int)F.lonAsc);

    // ---- [0] 种子点：规则网格 x 黑潮流廊道 x 海点 ----
    // 论文："seed points were placed on a regular grid with 1/12° spacing covering
    // the study domain" —— 即种子间距 = 数据网格距。seedSpacingDeg <= 0 时取该值
    // （官方 MATLAB 用 0.125°，比论文粗，属代码与正文的分歧）。
    const double domLon0 = 115.0, domLon1 = 132.0, domLat0 = 15.0, domLat1 = 32.0;
    double pre = (seedSpacingDeg > 1e-4) ? seedSpacingDeg : F.cellDeg;
    if (!(pre > 1e-4) || pre > 1.0) pre = 0.125;

    // 最近格点索引：均匀网格用 O(1)，否则线性扫描（种子生成仅一次）
    std::vector<double> seeds;
    {
        std::vector<char> used(F.nlat * F.nlon, 0);
        for (double la = domLat0; la <= domLat1 + 1e-9; la += pre) {
            for (double lo = domLon0; lo <= domLon1 + 1e-9; lo += pre) {
                bool inCorridor = (la < 24.0 && lo >= 115.0 && lo <= 124.0) ||
                                  (la >= 24.0 && lo >= 120.0 && lo <= 132.0);
                if (!inCorridor) continue;

                size_t bi = F.nearest(F.lon, F.nlon, F.lonAsc, F.lonUni, F.dLon, lo);
                size_t bj = F.nearest(F.lat, F.nlat, F.latAsc, F.latUni, F.dLat, la);
                size_t key = bj * F.nlon + bi;
                if (used[key]) continue;
                used[key] = 1;

                double gu, gv;
                if (!F.isSea(F.lon[bi], F.lat[bj])) continue;   // 仅海点
                if (!sampleF(F, F.lon[bi], F.lat[bj], gu, gv)) continue;
                seeds.push_back(F.lon[bi]);
                seeds.push_back(F.lat[bj]);
            }
        }
    }

    ncdfLog("[kuro] seeds: %d sea points in corridor\n", (int)(seeds.size() / 2));
    ncdfLog("[kuro][perf] setup+seeds=%lldms\n", phaseMs());
    if (seeds.size() < 4) {
        out.message = wxString::FromUTF8(
            "流廊道内无有效海点：数据范围可能不覆盖 115~132E / 15~32N 黑潮流区");
        return false;
    }

    // ---- [1] 双向流线追踪，逐条丢弃，只留候选子段 ----
    // 追踪边界：11 条断面 + 五段段盒外扩 4 度。流线一旦离开即截断——
    // 后续 validate_streamline 的盒约束必然失败，继续积分为纯浪费。
    TraceBounds tb = { 1e300, -1e300, 1e300, -1e300 };
    for (int s = 0; s < 11; s++) {
        for (int p = 0; p < 2; p++) {
            double lo = (p == 0) ? kSections[s].lon1 : kSections[s].lon2;
            double la = (p == 0) ? kSections[s].lat1 : kSections[s].lat2;
            if (lo < tb.lonMin) tb.lonMin = lo;
            if (lo > tb.lonMax) tb.lonMax = lo;
            if (la < tb.latMin) tb.latMin = la;
            if (la > tb.latMax) tb.latMax = la;
        }
    }
    for (int s = 0; s < 5; s++) {
        if (kSegs[s].lonRange[0] < tb.lonMin) tb.lonMin = kSegs[s].lonRange[0];
        if (kSegs[s].lonRange[1] > tb.lonMax) tb.lonMax = kSegs[s].lonRange[1];
        if (kSegs[s].latLim[0] < tb.latMin) tb.latMin = kSegs[s].latLim[0];
        if (kSegs[s].latLim[1] > tb.latMax) tb.latMax = kSegs[s].latLim[1];
    }
    const double kTraceMargin = 4.0;
    tb.lonMin -= kTraceMargin; tb.lonMax += kTraceMargin;
    tb.latMin -= kTraceMargin; tb.latMax += kTraceMargin;

    CandList candsMain[5], candsSM[5], candsMN[5];

    // 候选池上限。MATLAB 原版**不设上限**（candidates(end+1) 一路追加），
    // 这里的 cap 是移植时加的内存保护。但它按 avgSpeed 截断，而选优评分是
    //   score = 0.5*norm(start_dist) + 0.5*(1-norm(avg_speed))
    // —— 只按速度截断会把"离上一段近但略慢"的候选丢掉，偏向离岸的快流线。
    // 实测复杂段（B/C/D）三张表全部顶满 1500，故提供 kuroCap 做 A/B。
    size_t candCap = 1500;
    {
        int ovr = kuroCapOverride();
        if (ovr > 0) {
            candCap = (size_t)ovr;
            ncdfLog("[kuro] candCap forced to %u by ncdf_flags.txt (kuroCap)\n", (unsigned)candCap);
        }
    }
    for (int s = 0; s < 5; s++) {
        candsMain[s].cap = candCap;
        candsSM[s].cap   = candCap;
        candsMN[s].cap   = candCap;
    }

    // ---- 并行化（2026-10-29）----
    // 实测本阶段占总耗时 98.8%（integrate+collect=6672ms / 合计 6752ms）。
    // 22293 条种子流线彼此完全独立（UVField 只读、结果写各自的候选表）。
    //
    // 【为什么按线程分候选表而不是全程共享】CandList::add 不是线程安全的。
    // 【为什么结果仍确定】淘汰策略是"仅当更快者替换最慢者"——按 avgSpeed 质量取舍，
    //   与插入顺序无关。合并时按线程序号顺序重放，最终池等价于"全体候选里最快的 1500 条"。
    //   （严格说，若出现 avgSpeed 完全相同的并列，取舍会有先后差别；实测不会发生。）
    // 【注意】每线程各自 cap=1500 会让池子比串行**更宽**（8×1500 合并到 1500），
    //   即质量只会更好不会更差。改完须核对 nAxis / len / segs 与改前一致。
    unsigned nThreads = std::thread::hardware_concurrency();
    if (nThreads == 0) nThreads = 4;
    if (nThreads > 8) nThreads = 8;
    {
        int ovr = kuroThreadsOverride();
        if (ovr > 0) {
            nThreads = (unsigned)ovr;
            ncdfLog("[kuro] threads forced to %u by ncdf_flags.txt (kuroThreads)\n", nThreads);
        }
    }
    const size_t nSeeds = seeds.size() / 2;
    if ((size_t)nThreads > nSeeds) nThreads = (unsigned)nSeeds;
    if (nThreads < 1) nThreads = 1;

    struct WorkerOut {
        CandList cm[5], cs[5], cn[5];
        explicit WorkerOut(size_t cap = 1500) {
            for (int s = 0; s < 5; s++) { cm[s] = CandList(cap); cs[s] = CandList(cap); cn[s] = CandList(cap); }
        }
    };
    std::vector<WorkerOut> wout;
    wout.reserve(nThreads);
    for (unsigned t = 0; t < nThreads; t++) wout.push_back(WorkerOut(candCap));

    {
        // 动态派发（原子计数器）而不是静态分块：各处流线长短差别很大，
        // 静态分块会让先做完的线程干等。合并是"取全体前 cap 条"，与派发顺序无关。
        std::atomic<size_t> nextSeed(0);
        std::vector<std::thread> ts;
        ts.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; t++) {
            ts.emplace_back([&, t]() {
                WorkerOut &w = wout[t];
                std::vector<double> fwd, bwd, full;
                std::vector<int> hits[11];
                int half = maxStreamPoints / 2;
                if (half < 8) half = 8;

                for (;;) {
                    size_t si = nextSeed.fetch_add(1, std::memory_order_relaxed);
                    if (si >= nSeeds) break;
                    double sLon = seeds[2 * si], sLat = seeds[2 * si + 1];

                    integrateOneWay(F, sLon, sLat, +1.0, half, tb, fwd);
                    integrateOneWay(F, sLon, sLat, -1.0, half, tb, bwd);
                    if (fwd.size() + bwd.size() < 8) continue;

                    full.clear();
                    size_t nb = bwd.size() / 2;
                    full.reserve(nb * 2 + fwd.size());
                    for (size_t k = nb; k-- > 0; ) { full.push_back(bwd[2 * k]); full.push_back(bwd[2 * k + 1]); }
                    for (size_t k = 1; k < fwd.size() / 2; k++) { full.push_back(fwd[2 * k]); full.push_back(fwd[2 * k + 1]); }
                    if (full.size() < 4) continue;

                    // 11 条断面各算一次命中下标，五段复用（相邻段共用断面，勿重复计算）
                    for (int s = 0; s < 11; s++)
                        onSection(full, kSections[s].lon1, kSections[s].lat1,
                                  kSections[s].lon2, kSections[s].lat2, sectionTolDeg, hits[s]);

                    for (int s = 0; s < 5; s++) {
                        const std::vector<int> &hs = hits[kSegs[s].iSouth];
                        const std::vector<int> &hm = hits[kSegs[s].iMid];
                        const std::vector<int> &hn = hits[kSegs[s].iNorth];
                        // 主路径：从最后一个 south 交点向前回溯，取第一条有效子段
                        collectCandidates(full, kSegs[s], hs, hn, F,
                                          true, true, minLoopPoints, loopDistDeg, w.cm[s]);
                        // 降级路径：south->mid 与 mid->north（官方前向、不 break）
                        collectCandidates(full, kSegs[s], hs, hm, F,
                                          false, false, minLoopPoints, loopDistDeg, w.cs[s]);
                        collectCandidates(full, kSegs[s], hm, hn, F,
                                          false, false, minLoopPoints, loopDistDeg, w.cn[s]);
                    }
                }
            });
        }
        for (size_t k = 0; k < ts.size(); k++) ts[k].join();
    }

    // 合并：把所有线程的同一张表（同段同类型）汇到一起，按 avgSpeed 取前 cap 条入库。
    //
    // 【不能逐条 add】CandList::add 在池满后每条都要 O(cap) 线性扫最慢者，
    // 8 线程 × 1500 条 × 15 个表 ≈ 2.7 亿次比较，反而成了新瓶颈（8 线程只换到 3 倍）。
    // 改成先排序取前 cap 再依次入库：此时池未满，add 是 O(1)。
    //
    // 【结果与线程数无关】取的是"全体候选里最快的 cap 条"，与合并顺序无关 ——
    // 比串行的"先到先得占坑、后来者替换最慢"更严格（串行可能把后到的好候选漏在坑外）。
    //
    // 【用 swap 不拷贝】候选里是折线坐标，拷贝代价高。
    auto mergeCapped = [&wout, nThreads](int which, int s, CandList &dst) {
        struct Ref { double sp; Cand *c; };
        std::vector<Ref> all;
        for (unsigned t = 0; t < nThreads; t++) {
            CandList &L = (which == 0) ? wout[t].cm[s]
                        : (which == 1) ? wout[t].cs[s] : wout[t].cn[s];
            for (size_t i = 0; i < L.v.size(); i++)
                all.push_back(Ref{ L.v[i].avgSpeed, &L.v[i] });
        }
        if (all.empty()) return;
        size_t cap = std::min(dst.cap, all.size());
        std::partial_sort(all.begin(), all.begin() + (std::ptrdiff_t)cap, all.end(),
                          [](const Ref &a, const Ref &b) { return a.sp > b.sp; });
        for (size_t i = 0; i < cap; i++) {
            Cand c;
            c.xy.swap(all[i].c->xy);          // 移交，不拷贝
            c.avgSpeed = all[i].c->avgSpeed;
            dst.add(c);
        }
    };
    for (int s = 0; s < 5; s++) {
        mergeCapped(0, s, candsMain[s]);
        mergeCapped(1, s, candsSM[s]);
        mergeCapped(2, s, candsMN[s]);
    }
    ncdfLog("[kuro][perf] integrate+collect=%lldms (%u threads, %u seeds)\n",
            phaseMs(), nThreads, (unsigned)nSeeds);

    // ---- [2] 分段拼装：选优 + prev_end 接力 ----
    std::vector<double> segXY[5];
    bool segOk[5] = {false, false, false, false, false};
    double prevEndLon = 0, prevEndLat = 0;
    bool havePrevEnd = false;

    for (int s = 0; s < 5; s++) {
        Cand best;
        {
            char tag[8]; snprintf(tag, sizeof(tag), "%c/main", kSegs[s].name);
            dumpCands(tag, candsMain[s], prevEndLon, prevEndLat, havePrevEnd, F);
        }
        if (!candsMain[s].empty() && pickBest(candsMain[s], prevEndLon, prevEndLat, havePrevEnd, best)) {
            segXY[s].swap(best.xy);
            prevEndLon = segXY[s][segXY[s].size() - 2];
            prevEndLat = segXY[s][segXY[s].size() - 1];
            havePrevEnd = true;
            segOk[s] = true;
        } else {
            // 降级：south->mid 与 mid->north 分别选优后拼接
            // （官方 xy_segbei 跨段残留缺陷在此不复存在：bei 为局部变量）
            std::vector<double> bei[2];
            double localLon = prevEndLon, localLat = prevEndLat;
            bool localHave = havePrevEnd;

            if (pickBest(candsSM[s], localLon, localLat, localHave, best)) {
                bei[0].swap(best.xy);
                localLon = bei[0][bei[0].size() - 2];
                localLat = bei[0][bei[0].size() - 1];
                localHave = true;
            }
            if (pickBest(candsMN[s], localLon, localLat, localHave, best)) {
                bei[1].swap(best.xy);
                localLon = bei[1][bei[1].size() - 2];
                localLat = bei[1][bei[1].size() - 1];
                localHave = true;
            }
            if (!bei[0].empty() || !bei[1].empty()) {
                segXY[s].clear();
                segXY[s].insert(segXY[s].end(), bei[0].begin(), bei[0].end());
                segXY[s].insert(segXY[s].end(), bei[1].begin(), bei[1].end());
                if (segXY[s].size() >= 4) {
                    segOk[s] = true;
                    prevEndLon = localLon; prevEndLat = localLat; havePrevEnd = localHave;
                }
            }
        }
        out.segFound[s] = segOk[s] ? 1 : 0;
        ncdfLog("[kuro] segment %c: %s (main=%d sm=%d mn=%d)\n",
                kSegs[s].name, segOk[s] ? "OK" : "MISS",
                (int)candsMain[s].size(), (int)candsSM[s].size(), (int)candsMN[s].size());
    }

    ncdfLog("[kuro][perf] assemble=%lldms\n", phaseMs());
    std::vector<double> reference;
    for (int s = 0; s < 5; s++)
        if (segOk[s]) reference.insert(reference.end(), segXY[s].begin(), segXY[s].end());

    if (reference.size() < 8) {
        out.message = wxString::FromUTF8("未拼装出有效流线骨架：A–E 各段均无候选（数据范围/流速不足）");
        return false;
    }
    logPolySample("1-ref-assembled", reference, 30);   // [1] 拼装后的骨架

    // ---- [3] 陆海约束高斯平滑（55 km）----
    std::vector<double> smoothed;
    std::vector<int> removed;
    smoothPathBD(reference, recommendWindowRadius(reference, smoothRefKm), F, smoothed, removed);
    dropRows(smoothed, removed);
    ncdfLog("[kuro][perf] smooth=%lldms\n", phaseMs());
    logPolySample("2-smoothed", smoothed, 30);          // [2] 平滑后
    if (smoothed.size() < 8) {
        out.message = wxString::FromUTF8("平滑后主轴点过少（陆海掩膜剔除过多）");
        return false;
    }

    // ---- [4] 流线约束最大流速校正 ----
    std::vector<double> mainAxis;
    correctAxisTransect(F, smoothed, halfLenKm, nSect, smoothFinalKm, mainAxis);
    ncdfLog("[kuro][perf] correct=%lldms\n", phaseMs());
    logPolySample("3-after-correct", mainAxis, 30);     // [3] 垂断面校正后

    // ---- [5] 裁剪 ----
    cutReferenceAxis(mainAxis, cutLatMin, cutLonMax);
    if (mainAxis.size() < 6) {
        out.message = wxString::FromUTF8("裁剪后主轴点过少");
        return false;
    }

    // ---- [6] 左右边界 ----
    std::vector<double> left, right;
    if (wantBoundaries) {
        findKuroshioBoundaries(F, mainAxis, pctSpeed, maxDistKm, stepKm, smoothFinalKm, left, right);
        dropDecreasingLat(left);
        dropDecreasingLat(right);
        maskSideFlip(mainAxis, left, right);
        ncdfLog("[kuro][perf] boundaries=%lldms\n", phaseMs());
    }

    // ---- 输出统计 ----
    out.axis = mainAxis;
    out.left = left;
    out.right = right;
    out.nAxis = (int)(mainAxis.size() / 2);

    double sumSpd = 0, maxSpd = 0; int nSpd = 0;
    for (size_t i = 0; i < mainAxis.size() / 2; i++) {
        double uu, vv;
        if (sampleF(F, mainAxis[2 * i], mainAxis[2 * i + 1], uu, vv)) {
            double sp = std::hypot(uu, vv);
            sumSpd += sp; nSpd++;
            if (sp > maxSpd) maxSpd = sp;
        }
    }
    out.meanSpeed = (nSpd > 0) ? sumSpd / nSpd : 0.0;
    out.maxSpeed = maxSpd;

    double lenKm = 0;
    for (size_t i = 1; i < mainAxis.size() / 2; i++)
        lenKm += haversineKm(mainAxis[2 * i - 2], mainAxis[2 * i - 1],
                             mainAxis[2 * i], mainAxis[2 * i + 1]);
    out.lengthKm = lenKm;

    auto t1 = std::chrono::high_resolution_clock::now();
    out.elapsedMs = (double)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    int nOK = 0;
    for (int s = 0; s < 5; s++) if (segOk[s]) nOK++;
    out.ok = true;
    out.message = wxString::Format(
        wxString::FromUTF8("主轴 %d 点 / 长 %.0f km / 均速 %.2f m/s / 峰值 %.2f m/s / 分段 %d/5 / 耗时 %.0f ms"),
        out.nAxis, out.lengthKm, out.meanSpeed, out.maxSpeed, nOK, out.elapsedMs);

    ncdfLog("[kuro] extract OK: nAxis=%d len=%.0fkm mean=%.2f max=%.2f segs=%d/5 time=%.0fms\n",
            out.nAxis, out.lengthKm, out.meanSpeed, out.maxSpeed, nOK, out.elapsedMs);
    return true;
}
