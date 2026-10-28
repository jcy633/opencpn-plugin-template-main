#ifndef NCDF_TEXTURE_INTERNAL_H
#define NCDF_TEXTURE_INTERNAL_H

// ===================================================================
// ncdf_texture_internal.h —— 纹理/分块/陆地图层各模块共享的类型与入口
//
// 2026-10-28 由 ncdftexture.cpp 拆出（纯代码搬移，无逻辑改动）：
//   ncdf_prefilter.cpp     B 样条预滤波
//   ncdf_texture_build.cpp 纹理构建
//   ncdf_tilegrid.cpp      诊断开关 + 分块 UV + 绘制
//   ncdf_landlayer.cpp     GSHHG 陆地图层
//   ncdftexture.cpp        渲染入口（RenderGridOverlay 等）
// ===================================================================

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif
#include <GL/gl.h>
#include <thread>
#include <vector>

//-------------------------------------------------------------------
// parallelFor: 把 [0,n) 切成若干块并行执行。
// 只用局部 std::thread，不持有任何 static —— 插件加载期会执行到的代码里
// 不能有非平凡类型的 static（MSVC 线程安全初始化未就绪会挂死，见 CLAUDE.md）。
//-------------------------------------------------------------------
template <typename F>
inline void parallelFor(int n, F fn)
{
    if (n <= 0) return;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    if (hw > 8) hw = 8;
    int nThreads = (int)hw;
    if (nThreads <= 1 || n < 64) { for (int i = 0; i < n; i++) fn(i); return; }

    std::vector<std::thread> ts;
    const int chunk = (n + nThreads - 1) / nThreads;
    for (int t = 0; t < nThreads; t++) {
        int lo = t * chunk, hi = lo + chunk;
        if (hi > n) hi = n;
        if (lo >= hi) break;
        ts.emplace_back([lo, hi, &fn]() {
            for (int i = lo; i < hi; i++) fn(i);
        });
    }
    for (size_t k = 0; k < ts.size(); k++) ts[k].join();
}

class MainDialog;
class ncdf_pi;
class ncdfOverlayFactory;
struct PlugIn_ViewPort;
class NcdfLandMask;

#include "ncdfoverlayfactory.h"

//-------------------------------------------------------------------
// 纹理构建上下文
//-------------------------------------------------------------------
struct TexBuildContext {
    MainDialog *gui;
    int ni, nj;
    int tw, th;
    int borderH;
    bool repeat;
    unsigned char *texData;
    double tlon, tlat, blon, blat;
};

//-------------------------------------------------------------------
// 分块 UV 网格（处理非线性投影）
//-------------------------------------------------------------------
struct TileGrid {
    double *lva;
    int xs, ys, gridW, gridH;
    bool repeat;
    double lon_min, lon_range, lonstep;
    double lat_min, latstep;
};

struct TileQuad {
    double u0, v0, u1, v1, u2, v2, u3, v3;
    float x, y, xS, yS;
};
typedef void (*TileDrawFunc)(const TileQuad &q, void *userData);

//-------------------------------------------------------------------
// GSHHG 陆地图层（1 字节/格，GL_NEAREST）
//-------------------------------------------------------------------
struct LandLayer {
    GLuint texID;
    bool   hasTex;
    int    tw, th;
    double lonMin, latMin, lonStep, latStep;
    double latMax, lonMax;   // exact data extent (the texture has one cell of padding)
    LandLayer() : texID(0), hasTex(false), tw(0), th(0),
                  lonMin(0), latMin(0), lonStep(0), latStep(0), latMax(0), lonMax(0) {}
};

//-------------------------------------------------------------------
// 运行时诊断开关：C:/ProgramData/opencpn/ncdf_flags.txt
//-------------------------------------------------------------------
struct NcdfDbgFlags {
    bool sea, land, fill;
    int  tileX, tileY;
    bool dbgFill;
    bool dbgTiles;
};
//-------------------------------------------------------------------
// 渲染热路径分段计时：只累计、限流输出（每 120 次 flush 一行），
// 绝不能每帧写日志 —— 那曾经把 ncdf_debug.log 撑到 154 MB。
//-------------------------------------------------------------------
struct RenderTimer {
    double tileUV, sea, fill, land;
    int    n;
};
extern RenderTimer g_renderTimer;
void FlushRenderTimer();   // 累计满 120 次自动输出，也可手动调

extern NcdfDbgFlags s_dbg;
void LoadDbgFlags();

// 分块统计（诊断用）
extern unsigned int s_tileDrawn, s_tileSkipUV, s_tileSkipU;

// 当前唯一的 GSHHG 陆地图层（补空白 pass 要用它查掩膜）
extern LandLayer s_landLayer;

//-------------------------------------------------------------------
// 跨文件入口
//-------------------------------------------------------------------
// ncdftexture.cpp
void InvalidateLandLayers();
bool LandLayersActive();
void DrawLandLayers(PlugIn_ViewPort *vp);

// ncdf_prefilter.cpp
void BicubicSplinePrefilterWrapAware(float* dst, const float* src,
                                     const unsigned char* mask,
                                     int w, int h, bool isGlobal);

// ncdf_texture_build.cpp
void FillCoeffTexture(TexBuildContext &ctx, float coefMin, float coefMax,
                      float physMin, float physMax);
void FillVectorCoeffTex(TexBuildContext &ctx, ncdfOverlayFactory::RenderSettings &settings);
void FillScalarCoeffTex(TexBuildContext &ctx, float **grid,
                        ncdfOverlayFactory::RenderSettings &settings,
                        float *coeffBuf, float coefMin, float coefMax,
                        float physMin, float physMax);
void BuildLUTTex(ncdfOverlayFactory::ColorFunc colorFunc,
                 ncdfOverlayFactory::RenderSettings &settings,
                 GLuint &lutID, bool &hasLUT);
void ApplyTextureBorders(TexBuildContext &ctx);
void FillLandFractionChannel(TexBuildContext &ctx);
void UploadCoeffTexture(GLuint &texID, bool &hasTex, int tw, int th,
                        bool repeat, int dataDim[2], int glDim[2],
                        unsigned char *texData);
void BuildPhysicalTex(TexBuildContext &ctx, float **grid,
                      ncdfOverlayFactory::RenderSettings &settings,
                      bool repeat, GLuint *physicalTexID, bool *hasPhysicalTex);

// ncdf_tilegrid.cpp
TileGrid ComputeTileUVs(PlugIn_ViewPort *vp, TexBuildContext &ctx,
                        int dataDim[2], int glDim[2]);
void ForEachTile(PlugIn_ViewPort *vp, TileGrid &tg,
                 TileDrawFunc drawFunc, void *userData);
void DrawShaderTile(const TileQuad &q, void *userData);
void MakeDataGeo(const TileGrid &tg, int tw, int th,
                 int dataDim0, int dataDim1, float out[4]);
void MakeLandGeo(const LandLayer &L, float out[4]);

// ncdf_landlayer.cpp
bool BuildLandLayer(NcdfLandMask &m, double latMin, double latMax,
                    double lonMin, double lonMax, LandLayer &out);
void ComputeLandUVs(PlugIn_ViewPort *vp, const LandLayer &L, TileGrid &tg);
void DrawLandLayer(PlugIn_ViewPort *vp, const LandLayer &L);
void BuildLandLayers(NcdfLandMask &fine, NcdfLandMask *coarse,
                     double tlat, double tlon, double blat, double blon,
                     bool repeat);

#endif // NCDF_TEXTURE_INTERNAL_H
