//===================================================================
// ncdf_landlayer.cpp —— GSHHG 陆地图层
// 由 ncdftexture.cpp 拆出（2026-10-28）。类型与跨文件函数声明见
// include/ncdf_texture_internal.h。这里只是代码搬移，无逻辑改动。
//===================================================================

#include "ncdf_texture_internal.h"
#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_landmask.h"
#include "ncdf_pi.h"
#include "shader/ncdf_shader.h"
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

extern void ncdfLog(const char* format, ...);

// ===========================================================================

// Build a land texture from the mask over [latMin..latMax]x[lonMin..lonMax],
// one byte per texel, hard land/sea decision per texel.
bool BuildLandLayer(NcdfLandMask &m,
                           double latMin, double latMax,
                           double lonMin, double lonMax,
                           LandLayer &out)
{
    const ULONGLONG tLand0 = ::GetTickCount64();
    double a0 = wxMax(latMin, m.latMin()), a1 = wxMin(latMax, m.latMax());
    double o0 = wxMax(lonMin, m.lonMin()), o1 = wxMin(lonMax, m.lonMax());
    if (a1 - a0 < 1e-6 || o1 - o0 < 1e-6) return false;

    int maxTex = 4096;
    GLint mv = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mv);
    if (mv > 512) maxTex = mv;

    // Use the mask's own cell size; only coarsen if the texture would not fit.
    double step = m.dLat();
    if (!(step > 0.0)) step = 1.0 / 120.0;
    int th = (int)((a1 - a0) / step) + 1;
    int tw = (int)((o1 - o0) / step) + 1;
    const long long kMaxCells = 134217728LL;   // 128 MB at 1 byte/texel
    while (tw > maxTex || th > maxTex || (long long)tw * th > kMaxCells) {
        step *= 2.0;
        th = (int)((a1 - a0) / step) + 1;
        tw = (int)((o1 - o0) / step) + 1;
        if (tw <= 1 || th <= 1) return false;
    }

    std::vector<unsigned char> px((size_t)(tw + 2) * (th + 2), 0);   // 1-texel sea pad
    // 逐格独立，可并行（原先 5500 万格串行要 4 秒，是初次加载的最大头）
    // 每行交给 NcdfLandMask::landMaskRow 一次算完：掩膜行号整行只算一次、
    // 列向下标增量步进（任意 step/任意比值），比逐格 landFracInBox 快得多。
    const size_t pxStride = (size_t)(tw + 2);
    parallelFor(th, [&](int j) {
        const double la = a0 + step * j;
        unsigned char *rowOut = &px[((size_t)j + 1) * pxStride + 1];
        m.landMaskRow(la, o0, step, tw, rowOut);
    });

    // 自检（只跑一次）：与逐格 landFracInBox 逐格比对，确保批量版结果完全一致。
    // 不一致就记日志 —— 陆地边界出问题时这是最快的定位线索。
    {
        static bool s_checked = false;
        if (!s_checked && th > 0) {
            s_checked = true;
            std::vector<unsigned char> fast(tw), slow(tw, 0);
            m.landMaskRow(a0 + step * (th / 2), o0, step, tw, &fast[0]);
            const double la = a0 + step * (th / 2);
            for (int i = 0; i < tw; i++) {
                double lo = o0 + step * i;
                slow[i] = (m.landFracInBox(la - 0.5 * step, la + 0.5 * step,
                                           lo - 0.5 * step, lo + 0.5 * step) > 0.5) ? 255 : 0;
            }
            int mism = 0;
            for (int i = 0; i < tw; i++) if (fast[i] != slow[i]) mism++;
            ncdfLog("[landlayer] selfcheck landMaskRow vs landFracInBox: %d/%d mismatch%s\n",
                    mism, tw, mism ? "  <-- 不一致，需排查" : " (一致)");
        }
    }

    if (!out.texID) glGenTextures(1, &out.texID);   // 重建时复用同一 texID
    glBindTexture(GL_TEXTURE_2D, out.texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);   // per-cell judgement
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    // GL_UNPACK_ALIGNMENT defaults to 4, but GL_LUMINANCE is 1 byte/texel and
    // the widths (5761 / 10801) are not multiples of 4. Without this the driver
    // pads every row and the whole texture comes out sheared into stripes.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, tw + 2, th + 2, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, &px[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    out.hasTex = true;
    out.tw = tw + 2; out.th = th + 2;
    out.latMax = a0 + step * (th - 1);
    out.lonMax = o0 + step * (tw - 1);
    out.lonMin = o0; out.latMin = a0;
    out.lonStep = step; out.latStep = step;
    ncdfLog("[landlayer] built %dx%d over lat %.2f..%.2f lon %.2f..%.2f  step=%.3f' (%.2f km)\n",
            tw, th, a0, a1, o0, o1, step * 60.0, step * 111.0);
    ncdfLog("[perf] BuildLandLayer %dx%d step=%.3f' in %llu ms\n",
            tw, th, step * 60.0, (unsigned long long)(::GetTickCount64() - tLand0));
    return true;
}

// Screen tiles -> land-texture UVs (borderless grid, CLAMP_TO_EDGE).
void ComputeLandUVs(PlugIn_ViewPort *vp, const LandLayer &L, TileGrid &tg)
{
    memset(&tg, 0, sizeof(tg));
    tg.repeat = false;
    tg.xs = 0; tg.ys = 0;
    double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
    if (pw < 20) pw = 20;
    tg.xs = (int)ceil(vp->pix_width / pw);
    tg.ys = (int)ceil(vp->pix_height / pw);
    if (vp->rotation == 0) tg.xs = 1;
    // 分块下限与 ComputeTileUVs 保持一致（见那边的说明）。
    if (tg.xs < 4) tg.xs = 4; if (tg.ys < 4) tg.ys = 4;
    if (tg.xs > 16) tg.xs = 16; if (tg.ys > 16) tg.ys = 16;
    if (s_dbg.tileX > 0) tg.xs = s_dbg.tileX;
    if (s_dbg.tileY > 0) tg.ys = s_dbg.tileY;
    tg.gridW = tg.xs + 1; tg.gridH = tg.ys + 1;
    tg.lva = new(std::nothrow) double[tg.gridW * tg.gridH * 2];
    if (!tg.lva) return;

    // [步骤2] 经度沿 x 逐点解缠。原先按图层中心 clon 一次性 ±360，在日期变更线
    // 处同一行会从 +180 跳到 -180，UV 从 1.0 断到 0.0，分块被 q.u1<=q.u0 误剔、
    // 纹理也撕裂。改成和左边邻点对齐，UV 沿 x 连续。
    std::vector<double> lonRef(tg.gridH, 0.0);
    for (int i = 0; i < tg.gridW; i++) {
        double px = vp->pix_width / (double)tg.xs * i;
        for (int j = 0; j < tg.gridH; j++) {
            double py = vp->pix_height / (double)tg.ys * j;
            double lat, lon;
            wxPoint pt((int)px, (int)py);
            GetCanvasLLPix(vp, pt, &lat, &lon);
            if (i == 0) {
                lonRef[j] = lon;
            } else {
                while (lon - lonRef[j] > 180.0) lon -= 360.0;
                while (lonRef[j] - lon > 180.0) lon += 360.0;
                lonRef[j] = lon;
            }
            int idx = (i * tg.gridH + j) * 2;
            // +1.5: one texel of transparent sea padding around the grid
            tg.lva[idx]     = ((lon - L.lonMin) / L.lonStep + 1.5) / L.tw;
            tg.lva[idx + 1] = ((lat - L.latMin) / L.latStep + 1.5) / L.th;
        }
    }
}

// Draw the land layer: dataTex bound to the land texture, landMode = 2.
void DrawLandLayer(PlugIn_ViewPort *vp, const LandLayer &L)
{
    if (!L.hasTex) return;
    TileGrid tg;
    ComputeLandUVs(vp, L, tg);
    if (!tg.lva) return;

    ncdf_shader_use_program(ncdf_shader_get_grid_program());
    NcdfShaderUniforms u = ncdf_shader_get_uniforms();
    if (u.landMode >= 0) ncdf_shader_uniform_1i(u.landMode, NCDF_LANDMODE_LAND);
    if (u.dataTex >= 0) {
        ncdf_shader_active_texture(0x84C0);
        glBindTexture(GL_TEXTURE_2D, L.texID);
        ncdf_shader_uniform_1i(u.dataTex, 0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ForEachTile(vp, tg, DrawShaderTile, NULL);
    glDisable(GL_BLEND);
    ncdf_shader_active_texture(0x84C0);
    ncdf_shader_use_program(0);
    delete[] tg.lva;
}

// ---- Two land layers: a coarse one over the whole data extent, and a fine
// ---- one over the region where the high-res mask lives. The coarse layer is
// ---- built with the fine region cut out so the two never overlap.
LandLayer s_landLayer;   // [步骤5c] 两层合并为一层
static bool s_landBuilt = false;

// 数据范围 / 掩膜变化后必须调用：陆地图层是按 nc 文件经纬度范围建的，而
// s_landBuilt 是进程级标志，原先置 true 后永不复位 —— 换文件后陆地图层会一直
// 停在第一次建的那个范围，范围外没有 GSHHG，补空白就把海底色铺到陆地上。
// 现象是「陆地颜色消失」时好时坏，取决于哪个文件先加载。
// 不在这里删 GL 纹理（setData 可能跑在非 GL 线程），只标记过期，由
// BuildLandLayer 在渲染线程里复用同一 texID 重新 glTexImage2D。
void InvalidateLandLayers()
{
    s_landBuilt = false;
    s_landLayer.hasTex = false;
}

bool LandLayersActive() { return s_landBuilt && s_landLayer.hasTex; }

void BuildLandLayers(NcdfLandMask &fine, NcdfLandMask *coarse,
                            double tlat, double tlon, double blat, double blon,
                            bool repeat)
{
    if (s_landBuilt) return;
    double latMin = wxMin(tlat, blat), latMax = wxMax(tlat, blat);
    double lonMin = wxMin(tlon, blon), lonMax = wxMax(tlon, blon);

    // [步骤1] 日期变更线接缝：数据纹理是 GL_REPEAT，会画到 lon=180；但陆地图层
    // 若按数据范围建，只到最后一格（1/12° 数据是 179.9167），差一格就露出空白
    // 条带。全世界数据按整周期建到 ±180，掩膜本身是闭合的（nlon=43201）。
    if (repeat) {
        lonMin = wxMin(lonMin, -180.0);
        lonMax = wxMax(lonMax, 180.0);
    }

    // [步骤4] 停用精层，只建粗层 —— 验证「陆地不正常」是否由此引起
    bool hasFine = false;
    (void)hasFine;
    if (coarse && coarse->valid()) {
        BuildLandLayer(*coarse, latMin, latMax, lonMin, lonMax, s_landLayer);
    }
    s_landBuilt = true;
    ncdfLog("[landlayer] active=%d hasTex=%d\n",
            (int)LandLayersActive(), (int)s_landLayer.hasTex);
}

void DrawLandLayers(PlugIn_ViewPort *vp)
{
    if (!LandLayersActive()) return;
    if (!s_dbg.land) return;          // 诊断开关
    const ULONGLONG t0 = ::GetTickCount64();
    DrawLandLayer(vp, s_landLayer);
    g_renderTimer.land += (double)(::GetTickCount64() - t0);
}

// dataGeo: data-UV -> 经纬度；landGeoC/landGeoF: 经纬度 -> 掩膜 UV。
