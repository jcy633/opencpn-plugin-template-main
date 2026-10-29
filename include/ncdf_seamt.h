/*********************************************************************
 * ncdf_seamt.h — SEAMT 急流核心提取（流线集合分析求最小 T 参数）
 *
 * 算法：Streamline Ensemble Analysis for Minimum T-parameter
 * 参考：A.A. Udalov (2026) Ocean Modelling 202, 102730
 *
 * 流程（论文 Fig.1 四阶段）：
 *   [1] Γ_in 上取流速模最大点 P₀，布撒起点集合
 *   [2] 对每个起点用 RK4 + 固定时间步 Δt 积分 dr/dt = u（冻结场）
 *   [3] 筛出到达 Γ_out 的成功流线集合 G
 *   [4] G 中 T = Δt×N 最小者即急流核心
 * 附带：T(φ_out) 曲线的局部极小 = 子射流（分叉）核心
 *
 * 与 ncdf_kuroshio（Wang2026 流线约束最大流速法）完全独立：
 *   独立参数、独立结果结构、独立 UI，互不引用。
 *   仅共用 include/ncdf_uvfield.h 的速度插值与球面距离。
 *
 * T 是形式积分量 ∫ds/|u|，不是质点真实输运时间（冻结场近似）。
 * T 的绝对值随 Γ_in / Γ_out 的选择而变，跨研究域不可比。
 *********************************************************************/
#ifndef NCDF_SEAMT_H
#define NCDF_SEAMT_H

#include <vector>
#include <wx/string.h>

class ncdfDataMessage;
class NcdfLandMask;

// ---------------------------------------------------------------------------
// 自检工具（docs/SEAMT… §8.1）。只读诊断，不改结果。
// ---------------------------------------------------------------------------
// [1] 切向一致性：核心线上每点的流向角与线切向的夹角，返回中位数（度）。
//     核心线**必须是流线**，所以这个角应该很小；一旦对核心线做过平滑会立刻露馅。
//     这是 SEAMT 区别于一切断面追踪法的本质特征。
double seamtTangentCheck(const ncdfDataMessage &msg, const std::vector<double> &core);

// [3][4][5] 两条核心线的横向距离中位数（km）。用于步长收敛 / 积分格式 / 集合收敛的对照。
double seamtCoreDistance(const std::vector<double> &a, const std::vector<double> &b);

// ---------------------------------------------------------------------------
// 积分终止原因。论文 §2.2.2 / §3.1 只用这四个，不要再加。
//   到达 Γ_out / 离开计算域 / 搁浅 / 超过最大步数
// ---------------------------------------------------------------------------
enum SeamtStop {
    SEAMT_EXIT = 0,      // 东向跨越 Γ_out —— 唯一的成功条件
    SEAMT_OUTDOMAIN,     // 落出研究域矩形
    SEAMT_AGROUND,       // 搁浅（掩膜判陆）或速度场缺测
    SEAMT_MAXSTEP,       // 超步数上限。论文原话：排除"物理上不真实的过长流线"
    SEAMT_STOP_COUNT
};

// 子射流 = T(φ_out) 的一个局部极小
struct SeamtSubjet {
    double lonOut, latOut, TDays;
    int    nSteps;
    int    row;          // 在 tCurve 中的行号（0 起）
};

// 轴贴合度：核心线上的流速 vs 同一垂断面上的最大流速。
// 用来区分「SEAMT 抄近路绕过弯曲（方法特性）」与「集合被陆地挤成单侧（缺陷）」：
//   偏移有正有负、集中在弯曲处 = 特性；恒偏一侧 = 缺陷。
struct SeamtAxisFit {
    int    checks;
    double ratioAvg, ratioMin;      // 核心流速 / 断面最大流速
    double offsetSignedKm;          // 有符号均值，正 = 偏向法向正侧
    double offsetAbsKm;
    std::vector<double> profile;    // 每行 5 个：lat, ratio, signedOffsetKm, coreTurnDeg, ridgeTurnDeg
    std::vector<double> ridge;      // 速度脊折线 lon,lat,...（断面上最大流速点的连线）
    SeamtAxisFit() { clear(); }
    void clear() {
        checks = 0;
        ratioAvg = ratioMin = offsetSignedKm = offsetAbsKm = 0.0;
        profile.clear();
        ridge.clear();
    }
};

// ---------------------------------------------------------------------------
// 提取结果
// ---------------------------------------------------------------------------
struct SeamtResult {
    bool   ok;
    wxString message;

    std::vector<double> core;        // 核心线 lon,lat,lon,lat,...
    std::vector<double> subjetLines; // 子射流线，逐条打包（可选）
    std::vector<int>    subjetLineLen; // 与 subjetLines 配套：每条线的点数

    // T(φ_out) 表，按出口纬度升序。每行 4 个 double：latOut, TDays, nSteps, reason
    std::vector<double> tCurve;
    // 逐条流线记录：seedLat, reason, nSteps, T, latOut（5 个 double/行）。
    // 诊断用：能直接看出某个起点在不同步长下走了哪条路。
    std::vector<double> traceTable;
    std::vector<SeamtSubjet> subjets;

    double minT;         // 天 = N × Δt
    double meanSpeed;    // m/s，按论文口径 L/(T·86400)
    double lengthKm;
    double latIn, latOut;
    double varLatOut;    // G 的出口纬度方差（deg²），分叉 / 空间稳定性指数
    double tHavCheck;    // |T_hav − N·Δt| / (N·Δt)，T 累加自检
    double dtDays, uMaxEst, dxMinKm, cfl;
    SeamtAxisFit axisFit;
    int    nSuccess, nTotal;
    int    nNearMiss;      // 出域但离 Γ_out 很近的条数：>0 说明研究域边界在剪流（要放宽）
    int    nDegraded;    // RK4 中间采样点无效、退化为低阶步的次数
    int    reasonCount[SEAMT_STOP_COUNT];
    double elapsedMs;

    SeamtResult() { clear(); }
    void clear();
};

// ---------------------------------------------------------------------------
// 参数。默认值 = 论文正文取值。
// ---------------------------------------------------------------------------
struct SeamtParams {
    double lonIn;          // Γ_in  经度（论文 130°E）
    double lonOut;         // Γ_out 经度（日本以南预设 142°E，论文 160°E）
    double latP0Min;       // P₀ 搜索纬度带（论文 28–31°N）
    double latP0Max;
    double latDomMin;      // 研究域纬度范围，越界即 OUTDOMAIN
    double latDomMax;

    int    nPts;           // 集合规模（论文 141，取奇数保证关于 P₀ 对称）
    double halfWidthDeg;   // 集合半宽（论文 ±0.7°）

    double dtDays;         // 固定时间步。<=0 则按 CFL 自动推
    int    nMaxSteps;      // 步数上限。<=0 则按 300 天形式积分量推
    double cflTarget;      // 自动推 Δt 用的目标 CFL（论文隐含 0.46，取 0.5）

    double minSepDeg;      // 子射流最小出口纬度间隔（0 = 关掉该过滤）
    double minProminence;  // 子射流最小显著度，天（0 = 关掉）
    bool   wantSubjetLines;// 是否保留子射流线坐标用于着色

    SeamtParams();
    void normalize();      // 取奇数、纠正 lat 上下界等

    // 两个预设（§4.2）
    static void presetJapanSouth(SeamtParams &p);   // 130 → 142°E
    static void presetPaperDomain(SeamtParams &p);  // 130 → 160°E
};

// ---------------------------------------------------------------------------
// 提取器
// ---------------------------------------------------------------------------
class SeamtExtractor {
public:
    SeamtExtractor();

    SeamtParams params;

    // 高分辨率陆海掩膜（GSHHG）。传 NULL 则退回数据自身的无效值掩膜。
    void setLandMask(const NcdfLandMask *lm) { m_landMask = lm; }

    // 主入口：从当前时间步的 U/V 场提取急流核心（及子射流）
    bool extract(const ncdfDataMessage &msg, SeamtResult &out);

private:
    const NcdfLandMask *m_landMask;
};

#endif // NCDF_SEAMT_H
