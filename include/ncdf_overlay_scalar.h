#ifndef NCDF_OVERLAY_SCALAR_H
#define NCDF_OVERLAY_SCALAR_H

// ===================================================================
// ncdf_overlay_scalar.h —— 标量色图图层的公共实现
//
// 海温 / 海盐 / 海高三个图层原本是三份逐行复制的代码（各约 390 行），
// 差异只有 6 处：数据源、取值函数、色标、LUT 范围、日志标签、size 查询。
// 复制粘贴的代价是同一个 bug 要修三遍——2026-10-27 就因为海流之外的三个
// 标量层漏传 physicalTexID，导致它们全都没有空白填补。
//
// 现在状态与实现全部放在 ScalarOverlayBase，子类只留钩子 + 色标。
// 外部代码（ncdf.cpp 等）通过 f->m_seaTempOverlay.cachedBaseGrid 访问字段，
// 字段上移到基类后这些访问完全不变。
// ===================================================================

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include "ncdf.h"
#include <vector>
#include <memory>

class MainDialog;
class ncdf_pi;
class ncdfOverlayFactory;
struct PlugIn_ViewPort;
struct ncdfIsoSeg;

// 与 ncdfOverlayFactory::ColorFunc 同型：普通函数指针，便于传给 RenderScalarColorMap
typedef wxColour (*ScalarColorFn)(double);

struct ScalarOverlayBase {
    // ---- 状态（原先三个 struct 各抄一份）----
    GLuint glTexture;
    bool hasTexture;
    bool needsRebuild;
    bool dataReady;
    int dataDim[2];
    int glDim[2];

    GLuint lutID;
    bool hasLUT;

    // 补空白 pass 用的物理值纹理（含最近邻传播值）
    GLuint physicalTexID;
    bool hasPhysicalTex;

    unsigned char *uploadBuf;
    int uploadBufSize;

    std::unique_ptr<float*[]> cachedBaseGrid;
    int cachedNj, cachedNi;

    float cachedDataMin, cachedDataMax;
    float cachedCoefMin, cachedCoefMax;
    float cachedPhysMin, cachedPhysMax;
    float cachedPhysMinV, cachedPhysMaxV;

    float *cachedCoeff;
    float cachedCoefScalarMin, cachedCoefScalarMax;
    float cachedPhysScalarMin, cachedPhysScalarMax;
    int cachedCoeffNj, cachedCoeffNi;

    bool needsIsoRebuild;
    std::vector<ncdfIsoSeg> isoSegments;

    bool gridReady = false;

    // ---- 子类差异：只有这 6 个钩子 ----
    virtual ~ScalarOverlayBase() { Cleanup(); }
    virtual const char *logTag() const = 0;                    // "sst" / "sal" / "zos"
    virtual bool   hasData(MainDialog &gui) const = 0;
    virtual double valueAt(MainDialog &gui, int i, int j) const = 0;
    virtual size_t dataSize(MainDialog &gui) const = 0;
    // 由 valueAt 通用实现，子类不用管
    void fillGrid(MainDialog &gui, std::unique_ptr<double*[]> &grid, int nj, int ni) const;
    // 子类提供静态色标函数（ColorFunc 是普通函数指针，不能传虚成员函数）
    virtual ScalarColorFn colorFunc() const = 0;
    virtual float  lutMin() const = 0;
    virtual float  lutMax() const = 0;

    // ---- 公共实现 ----
    void Init();
    void Cleanup();
    void clearCache();
    void clearCoefficients();
    void ensureGridAllocated(int ni, int nj);
    void prepareGrid(MainDialog *gui);
    void prepareCoeff(ncdfOverlayFactory *factory);
    void releaseCoeffData();
    void Invalidate();
    void prepareData(MainDialog *gui, ncdf_pi *plugin, ncdfOverlayFactory *factory);

    bool RenderColorMap(PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
                        ncdfOverlayFactory *factory, double **animatedGrid = NULL);
    void RenderIsoLines(PlugIn_ViewPort *vp, MainDialog *gui);
};

#endif // NCDF_OVERLAY_SCALAR_H
