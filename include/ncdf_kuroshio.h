/*********************************************************************
 * ncdf_kuroshio.h — 黑潮主轴与左右边界提取
 *
 * 算法：streamline-constrained maximum velocity method（流线约束最大流速法）
 * 参考：Wang et al. (2026, Scientific Data) 三维日尺度黑潮轴线与边界数据集
 *       官方代码 main_Kuroshio_axis_and_boundaries.m
 *
 * 流程：
 *   [0] 黑潮流廊道种子点 -> [1] 双向流线追踪 -> [2] A–E 五段断面拼装 + 打分
 *   -> [3] 陆海约束高斯平滑(55km) -> [4] 流线约束最大流速校正(垂断面±70km)
 *   -> [5] 裁剪 -> [6] 法向 30% 流速阈值左右边界 -> [7] 边界后处理
 *
 * 与 MATLAB 原版的差异（对应核心算法解读.md 第六节问题清单）：
 *   - 修复 xy_segbei 跨段残留（每次进入降级分支前清空）
 *   - 修复 correct_axis_transect 中陆地点 argmax 候选问题（陆地点置 -inf）
 *   - 掩膜来自数据自身的有效值（NaN / ncdf_NOTDEF），无需外部 mask.mat
 *********************************************************************/
#ifndef NCDF_KUROSHIO_H
#define NCDF_KUROSHIO_H

#include <vector>
#include <wx/string.h>

class ncdfDataMessage;
class NcdfLandMask;

// ---------------------------------------------------------------------------
// 提取结果：主轴 + 左右边界（lon,lat 成对存储，边界缺口为 NaN）
// ---------------------------------------------------------------------------
struct KuroshioAxisResult {
    bool ok;
    wxString message;          // 状态说明（用于界面读数 / 日志）
    std::vector<double> axis;  // 主轴  lon,lat,lon,lat,...
    std::vector<double> left;  // 左边界（与 axis 同长度，缺口为 NaN）
    std::vector<double> right; // 右边界
    double meanSpeed;          // 轴上平均流速 m/s
    double maxSpeed;           // 轴上最大流速 m/s
    double lengthKm;           // 主轴长度 km
    int    nAxis;              // 主轴点数
    int    segFound[5];        // A–E 各段是否拼装成功
    double elapsedMs;

    KuroshioAxisResult() { clear(); }
    void clear() {
        ok = false; message.clear();
        axis.clear(); left.clear(); right.clear();
        meanSpeed = maxSpeed = lengthKm = 0; nAxis = 0;
        for (int i = 0; i < 5; i++) segFound[i] = 0;
        elapsedMs = 0;
    }
};

// ---------------------------------------------------------------------------
// 提取器：无状态参数 + 有状态结果；extract() 每次重算
// ---------------------------------------------------------------------------
class KuroshioExtractor {
public:
    KuroshioExtractor();

    // ---- 参数（默认值 = 论文正文取值；官方 MATLAB 的差异见实现内注释）----
    double seedSpacingDeg;   // 种子间距；<=0 取数据网格距（论文 1/12°）
    double sectionTolDeg;   // 断面归属容差 tol = 0.5
    double smoothRefKm;     // 骨架平滑目标 55 km
    double smoothFinalKm;   // 轴/边界平滑目标 110 km
    double halfLenKm;       // 垂断面半长 70 km（论文 "140-km transect"）
    int    nSect;           // 垂断面采样点数 21（±70 km 上 7 km 一点）
    double pctSpeed;        // 边界流速阈值 = 轴上流速 * 0.3
    double maxDistKm;       // 边界搜索范围 70 km（两侧合计 140 km）
    double stepKm;          // 边界搜索步长 7 km
    double cutLatMin;       // 裁剪纬度下限 16 N
    double cutLonMax;       // 裁剪经度上限 130.38 E
    int    maxStreamPoints; // 单条流线最大点数（正/反向各半）
    bool   wantBoundaries;  // 是否计算左右边界
    int    minLoopPoints;   // 环流检测
    double loopDistDeg;

    // 主入口：从当前时间步的 U/V 场提取黑潮主轴（及左右边界）
    bool extract(const ncdfDataMessage &msg, KuroshioAxisResult &out);

    // 高分辨率陆海掩膜（ETOPO 2022）。论文 Methods 用 ETOPO 2022 (30") 做陆海
    // 掩膜；本方法在**非格点位置**查询陆海（平滑窗、±70 km 断面、法向边界搜索），
    // 9 km 的数据掩膜回答不了这些查询，0.9 km 的 ETOPO 才行。传 NULL 则退回
    // 数据自身的无效值掩膜。
    void setLandMask(const NcdfLandMask *lm) { m_landMask = lm; }

    // 仅保留论文默认的黑潮流廊道种子域，供 UI 显示
    static void defaultDomain(double &latMin, double &latMax,
                              double &lonMin, double &lonMax);

private:
    const NcdfLandMask *m_landMask = nullptr;
};

#endif // NCDF_KUROSHIO_H
