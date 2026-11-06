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

    // 临界点（HEPTA 式雅可比分类，Udalov 2026 §2.3）。每条 10 个 double：
    //   lon, lat, type, lambdaRe, lambdaIm, det, trace, disc, div, vort
    //   type: 0=鞍点(双曲) 1=中心(椭圆) 2=螺旋汇(涡心+辐合) 3=螺旋源(涡心+辐散)
    //         4=汇节点 5=源节点 6=退化/不确定
    // 来源是 MAXSTEP 终点聚类的中心：流线在那儿停住，就地做线性化分类。
    std::vector<double> critPoints;

    // 涡的几何（2026-11-02，供上图）：
    //   vortexCentres   每个涡 4 个 double: lon, lat, type, radiusKm（type 同 critPoints）
    //   vortexCircles   每个涡的边界折线 lon,lat,...（速度环，逐个打包）
    //   vortexCircleLen 与 vortexCircles 配套：每个涡的边界点数
    std::vector<double> vortexCentres;
    std::vector<double> vortexCircles;
    std::vector<int>    vortexCircleLen;

    // 次级射流（T(φ_out) 的局部极小）的出口纬度与 T，供上图与"鞍点分裂"判读
    std::vector<double> subjetSummary;   // 每个 3 个 double: latOut, TDays, isPrimary

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

    // 分段拼接时**降级**留下的接缝（左段不是从 P* 出发）在 core 里的点下标，
    // 显示时对它附近做局部平滑。-1 = 不需要（左段就是 P* 那条，接缝天然连续）。
    int seamSmoothIdx;
    double dtDays, uMaxEst, dxMinKm, cfl;
    SeamtAxisFit axisFit;
    int    nSuccess, nTotal;
    int    nNearMiss;      // 出域但离 Γ_out 很近的条数：>0 说明研究域边界在剪流（要放宽）
    int    nDegraded;    // RK4 中间采样点无效、退化为低阶步的次数
    int    nBridged;     // 跨过小岛/缺测缺口的次数（2026-10-30 小缺口桥接）
    int    nWrapped;     // 涡外缘绕行拼出来的流线条数（2026-11-02）
    int    reasonCount[SEAMT_STOP_COUNT];
    double elapsedMs;

    SeamtResult() { clear(); }
    void clear();
};

// ---------------------------------------------------------------------------
// 参数。默认值 = 论文正文取值。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 断面几何。论文的 Γ_in / Γ_out 是「一条经线」，Wang 域要用的是**任意线段**
// （A.south 是纬向的）。两种几何并列提供，各走各的公式 —— 不在一个公式里
// 做 if/else 分派，免得一个应用域的修法波及另一个（两域参数互不套用）。
//
// set=false（默认）时走**经向**实现，公式与最初版本逐位相同：老域的一切行为
// 都由这条路径保证不变。
// ---------------------------------------------------------------------------
struct SeamtSection {
    double lon1, lat1, lon2, lat2;
    bool   set;
    SeamtSection() : lon1(0), lat1(0), lon2(0), lat2(0), set(false) {}
};

// ---------------------------------------------------------------------------
// 陆地容差模式。**每个应用域各自选**，两域设置互不套用。
//
//   TRUNCATE      判陆即截断（现状/默认）：UVField::sample 的严格四角规则 +
//                 GSHHG 掩膜判陆就砍断流线。近岸海域会被四角规则啃掉一圈，
//                 1–2 格的小岛会把流线挡死，但**不会造出跨陆地的假路径**。
//   SKIP_ISLANDS  跳岛容差：sampleCoast / isSeaCoast，2×2 只要有一个有效角就取值，
//                 流线能从被四角规则啃掉的窄条里穿过去、绕过 1–2 格的小岛。
//                 代价是它也能从"只有一个角是海"的格子里**穿过小岛抄近路**。
//
// 两种都留着，按域选。A/B 可用请求文件 landmode=0|1，或 ncdf_flags.txt 的
// seamtCoastTolerant（后者是**诊断覆盖**，优先级高于本设置）。
// ---------------------------------------------------------------------------
enum SeamtLandMode {
    SEAMT_LAND_TRUNCATE     = 0,
    SEAMT_LAND_SKIP_ISLANDS = 1
};

struct SeamtParams {
    double lonIn;          // Γ_in  经度（论文 130°E）—— 只在**经向**断面下是几何
    double lonOut;         // Γ_out 经度（日本以南预设 142°E，论文 160°E）
    double latP0Min;       // P₀ 搜索纬度带（论文 28–31°N）
    double latP0Max;
    double latP0Forced;    // 非 NaN 时强制用它作 P₀，不再自动找。
                           // 分段拼接时左段用 P*（右段轴起点），否则反推段会按
                           // "向西流最快"选到黑潮逆流（2026-10-30）
    double splitLon;       // 非 NaN 时把 [lonIn,lonOut] 拆成两段在此拼接，
                           // 共用右段轴的起始点 P*（2026-10-30）
    double latDomMin;      // 研究域纬度范围，越界即 OUTDOMAIN
    double latDomMax;
    double lonPadDeg;      // 研究域经度边界的外扩量（度）。
                           // 【2026-11-02】原先经度域硬绑 [min(lonIn,lonOut), max(lonIn,lonOut)]，
                           // 把 Γ_in 当成了研究域的墙：断面上带向西分量的流线一出发就被判
                           // OUTDOMAIN 丢掉。实测 全3.nc Γ_in=130：172/561 条（31%）平均只走
                           // 15 步就"出域"，终点 avgEnd=(130.000, 30.499) 就压在 Γ_in 上。
                           // 论文里 Γ_in 是研究域**内部**的一条切割断面，不是边界；
                           // 纬度域 latDomMin/Max 本来就比轴线宽得多，经度侧不该贴死。
                           // <=0 = 退回旧行为（域 = [lonIn, lonOut]）。

    // ---- 断面几何（线段形态）----
    // 【默认 set=false】老域（日本以南 / 延伸体）完全走经向实现，行为不变。
    // 只有 presetWangUpstream 会把它们打开。两个应用域的设置各自独立，互不套用。
    SeamtSection inSec;        // Γ_in  线段（Wang 域 = A.south A1，纬向）
    SeamtSection outSec;       // Γ_out 线段（Wang 域 = E.north E2，经向）
    double jetAngMaxDeg;       // 「流向朝 Γ_out」的角度容差。经向断面下 = 旧的
                               // 「朝东 45° 内」判据；默认 45，Wang 域取 60。

    int landMode;              // 陆地容差模式（见 SeamtLandMode）。默认 TRUNCATE。

    // true = 只布 latP0Forced 这**一个**种子，不做 min-T 选优（走单条流线）。
    // 【为什么需要】分段拼接的左段是右段流线的**逆向延拓**，不是独立研究对象。
    // 让它自己跑 min-T 会挑到另一条更快的流 —— 实测 左段起点落到 32.211°N，
    // 而 P*=32.616°N，拼接处断开 44.7 km、弦转角 77°/90°（全轴中位 0.71°）。
    // 默认 false：老域的单段路径行为不变。
    bool singleSeed;

    // 分段拼接**左段**（130→133 逆流向反推）专用的陆地容差。
    // 【与 landMode 分开】左段是"反推补全"，要绕开 1–2 格的小岛才走得通；
    // 右段（正向 min-T）仍用 landMode。默认 SKIP_ISLANDS（2026-11-05 用户定）。
    int leftLandMode;

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

    // 预设（§4.2 两个下游域；上游域见 docs/SEAMT…md「上游域」一节）
    // 【两域的取值各自独立】presetWangUpstream 逐项显式赋值，不继承上面两个的任何值；
    // 同理它也不会去改下面两个的取值。
    static void presetJapanSouth(SeamtParams &p);   // 130 → 142°E
    static void presetPaperDomain(SeamtParams &p);  // 130 → 160°E
    static void presetWangUpstream(SeamtParams &p); // A1(122,16) → E2(130.38,28.25–31.25)
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
