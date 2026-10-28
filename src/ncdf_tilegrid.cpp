//===================================================================
// ncdf_tilegrid.cpp —— 诊断开关 + 分块 UV + 绘制
// 由 ncdftexture.cpp 拆出（2026-10-28）。类型与跨文件函数声明见
// include/ncdf_texture_internal.h。这里只是代码搬移，无逻辑改动。
//===================================================================

#include "ncdf_texture_internal.h"
#include "ncdfoverlayfactory.h"
#include "ncdf_pi.h"
#include "shader/ncdf_shader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

extern void ncdfLog(const char* format, ...);

//
// 文件：C:\ProgramData\opencpn\ncdf_flags.txt
//   sea=0     不画海色
//   land=0    不画 GSHHG 陆地
//   fill=0    不画补空白
//   tileX=N   强制横向分块数（0 = 自动）
//   tileY=N   强制纵向分块数（0 = 自动）
//   dbgFill=1 把补空白要画的区域涂成纯红，直接看它的边界形状
//
// 文件不存在或键缺失时全部走默认（三 pass 全开、自动分块），
// 所以平时完全不影响任何行为。按文件 mtime 判断是否重读，改完存盘即可生效。
//===================================================================
NcdfDbgFlags s_dbg = { true, true, true, 0, 0, false, false };

// 用 mtime 判断是否需要重读：fopen+fgets+sscanf 只在文件真的变过时才做。
// 渲染热路径每帧每层都会走到这里，原来每 30 次调用就读一遍文件，纯浪费。
void LoadDbgFlags()
{
    static ULONGLONG s_lastCheck = 0;
    static FILETIME   s_lastWrite = { 0, 0 };
    static bool       s_loadedOnce = false;

    ULONGLONG now = ::GetTickCount64();
    if (s_loadedOnce && now - s_lastCheck < 500) return;   // 500ms 内不再 stat
    s_lastCheck = now;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExA("C:\\ProgramData\\opencpn\\ncdf_flags.txt",
                                GetFileExInfoStandard, &fad)) {
        // 文件不存在 -> 保持默认值
        if (!s_loadedOnce) {
            s_dbg.sea = true; s_dbg.land = true; s_dbg.fill = true;
            s_dbg.tileX = 0; s_dbg.tileY = 0;
            s_dbg.dbgFill = false; s_dbg.dbgTiles = false;
            s_loadedOnce = true;
        }
        return;
    }
    if (s_loadedOnce &&
        fad.ftLastWriteTime.dwLowDateTime == s_lastWrite.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == s_lastWrite.dwHighDateTime) return;
    s_lastWrite = fad.ftLastWriteTime;

    s_dbg.sea = true; s_dbg.land = true; s_dbg.fill = true;
    s_dbg.tileX = 0;  s_dbg.tileY = 0;  s_dbg.dbgFill = false; s_dbg.dbgTiles = false;

    FILE *fp = fopen("C:\\ProgramData\\opencpn\\ncdf_flags.txt", "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        int v = 0;
        if (sscanf(line, "sea=%d", &v) == 1)      s_dbg.sea = (v != 0);
        else if (sscanf(line, "land=%d", &v) == 1) s_dbg.land = (v != 0);
        else if (sscanf(line, "fill=%d", &v) == 1) s_dbg.fill = (v != 0);
        else if (sscanf(line, "tileX=%d", &v) == 1) s_dbg.tileX = v;
        else if (sscanf(line, "tileY=%d", &v) == 1) s_dbg.tileY = v;
        else if (sscanf(line, "dbgFill=%d", &v) == 1) s_dbg.dbgFill = (v != 0);
        else if (sscanf(line, "dbgTiles=%d", &v) == 1) s_dbg.dbgTiles = (v != 0);
    }
    fclose(fp);
    s_loadedOnce = true;
}

// 分块被跳过的原因统计 —— 如果隐形线来自某个分块被误剔，
// 这里会直接显示出来（ForEachTile 的 q.u1<=q.u0 判据在日期变更线/旋转
// 海图下会误跳分块，交接文档记过这个坑）。
unsigned int s_tileDrawn = 0, s_tileSkipUV = 0, s_tileSkipU = 0;

RenderTimer g_renderTimer = { 0, 0, 0, 0, 0 };

void FlushRenderTimer()
{
    RenderTimer &t = g_renderTimer;
    if (t.n <= 0) return;
    ncdfLog("[perf] render x%d | tileUV=%.2f sea=%.2f fill=%.2f land=%.2f ms/frame\n",
            t.n, t.tileUV / t.n, t.sea / t.n, t.fill / t.n, t.land / t.n);
    t.tileUV = t.sea = t.fill = t.land = 0; t.n = 0;
}

//===================================================================

TileGrid ComputeTileUVs(PlugIn_ViewPort *vp, TexBuildContext &ctx,
                               int dataDim[2], int glDim[2])
{
    TileGrid tg;
    memset(&tg, 0, sizeof(tg));

    int ni = ctx.ni, nj = ctx.nj, tw = glDim[0], th = glDim[1];
    double potNormX = (double)dataDim[0] / tw;
    double potNormY = (double)dataDim[1] / th;
    tg.lat_min = ctx.blat; tg.lon_min = ctx.tlon;
    double lat_max = ctx.tlat, lon_max = ctx.blon;
    tg.latstep = fabs(lat_max - tg.lat_min) / (nj - 1);
    tg.lonstep = (lon_max - tg.lon_min) / (ni - 1);
    if (tg.latstep < 1e-10 || tg.lonstep < 1e-10) return tg;
    double clon = (tg.lon_min + lon_max) / 2;

    tg.repeat = (lon_max - tg.lon_min + tg.lonstep >= 360);
    tg.lon_range = lon_max - tg.lon_min;

    // Tile grid for non-linear projection handling
    double pw = vp->view_scale_ppm * 1e6 / pow(2, fabs(vp->clat) / 25);
    if (pw < 20) pw = 20;
    tg.xs = (int)ceil(vp->pix_width / pw);
    tg.ys = (int)ceil(vp->pix_height / pw);
    if (vp->rotation == 0) tg.xs = 1;
    // 分块数下限必须和 ComputeLandUVs 完全一致：两边在屏幕上的分块边界要重合，
    // 否则 GSHHG 海岸线和海色场各自的分段线性 UV 逼近误差不一样，海岸线会被
    // 错开一圈，肉眼看到的就是「屏幕中间一条隐形线」，越过它海域外围变大一圈。
    if (tg.xs < 4) tg.xs = 4; if (tg.ys < 4) tg.ys = 4;
    // Global data needs more tiles to avoid degenerate texture coords
    if (tg.repeat && tg.xs < 4) tg.xs = 4;
    if (tg.xs > 16) tg.xs = 16; if (tg.ys > 16) tg.ys = 16;
    if (s_dbg.tileX > 0) tg.xs = s_dbg.tileX;
    if (s_dbg.tileY > 0) tg.ys = s_dbg.tileY;
    tg.gridW = tg.xs + 1; tg.gridH = tg.ys + 1;

    tg.lva = new(std::nothrow) double[tg.gridW * tg.gridH * 2];
    if (!tg.lva) return tg;

    for (int i = 0; i < tg.gridW; i++) {
        double px = vp->pix_width / (double)tg.xs * i;
        for (int j = 0; j < tg.gridH; j++) {
            double py = vp->pix_height / (double)tg.ys * j;
            double lat, lon;
            wxPoint pt((int)px, (int)py);
            GetCanvasLLPix(vp, pt, &lat, &lon);
            if (tg.repeat) {
                // Wrap longitude into data range for GL_REPEAT
                double d = fmod(lon - tg.lon_min, tg.lon_range);
                if (d < 0) d += tg.lon_range;
                lon = tg.lon_min + d;
            } else {
                if (clon - lon > 180) lon += 360;
                else if (lon - clon > 180) lon -= 360;
            }
            int idx = (i * tg.gridH + j) * 2;
            tg.lva[idx]     = ((lon - tg.lon_min) / tg.lonstep - tg.repeat + 1.5) / tw * potNormX;
            tg.lva[idx + 1] = ((lat - tg.lat_min) / tg.latstep + 1.5) / th * potNormY;
        }
    }
    return tg;
}

//===================================================================
// (i) Common tile iteration with UV correction
//===================================================================


void ForEachTile(PlugIn_ViewPort *vp, TileGrid &tg,
                         TileDrawFunc drawFunc, void *userData)
{
    int xs = tg.xs, ys = tg.ys, gridH = tg.gridH;
    double *lva = tg.lva;
    bool repeat = tg.repeat;
    double xS = vp->pix_width / (double)xs;
    double yS = vp->pix_height / (double)ys;

    for (int i = 0; i < xs; i++) {
        for (int j = 0; j < ys; j++) {
            int i00 = (i * gridH + j) * 2;
            int i10 = ((i+1) * gridH + j) * 2;
            int i11 = ((i+1) * gridH + j+1) * 2;
            int i01 = (i * gridH + j+1) * 2;
            TileQuad q;
            q.u0=lva[i00]; q.u1=lva[i10]; q.u2=lva[i11]; q.u3=lva[i01];
            q.v0=lva[i00+1]; q.v1=lva[i10+1]; q.v2=lva[i11+1]; q.v3=lva[i01+1];

            if (repeat) {
                if (q.u1 - q.u0 > .5) q.u1--;
                else if (q.u0 - q.u1 > .5) q.u1++;
                if (q.u2 - q.u0 > .5) q.u2--;
                else if (q.u0 - q.u2 > .5) q.u2++;
                if (q.u3 - q.u0 > .5) q.u3--;
                else if (q.u0 - q.u3 > .5) q.u3++;
            }

            if (!((repeat ||
                   ((q.u0>=0||q.u1>=0||q.u2>=0||q.u3>=0)&&(q.u0<=1||q.u1<=1||q.u2<=1||q.u3<=1))) &&
                  (q.v0>=0||q.v1>=0||q.v2>=0||q.v3>=0)&&(q.v0<=1||q.v1<=1||q.v2<=1||q.v3<=1))) {
                s_tileSkipUV++;
                continue;
            }
            if (q.u1 <= q.u0) { s_tileSkipU++; continue; }
            s_tileDrawn++;

            q.x = (float)(xS * i);
            q.y = (float)(yS * j);
            q.xS = (float)xS;
            q.yS = (float)yS;
            drawFunc(q, userData);
        }
    }
}

//===================================================================
// (i) Shader draw path
//===================================================================
void DrawShaderTile(const TileQuad &q, void *userData) {
    GLfloat verts[8] = {q.x, q.y, q.x+q.xS, q.y,
                        q.x+q.xS, q.y+q.yS, q.x, q.y+q.yS};
    GLfloat uvs[8] = {(float)q.u0,(float)q.v0, (float)q.u1,(float)q.v1,
                      (float)q.u2,(float)q.v2, (float)q.u3,(float)q.v3};
    ncdf_shader_enable_attrib(0);
    ncdf_shader_enable_attrib(1);
    ncdf_shader_attrib_pointer(0, 2, 0, verts);
    ncdf_shader_attrib_pointer(1, 2, 0, uvs);
    glDrawArrays(GL_QUADS, 0, 4);
    ncdf_shader_disable_attrib(0);
    ncdf_shader_disable_attrib(1);
}

// [步骤5d] FillTextureGaps 已删（基准里就已无调用，纯死代码）


//
// Drawn as its own texture/pass, ONE BYTE PER MASK CELL with GL_NEAREST, so
// each screen pixel is judged against a mask cell rather than against the
// coarse data grid. The source data only supplies the colours; the coastline
// shape comes entirely from the mask. That is what keeps it from looking
// like a mosaic.
void MakeDataGeo(const TileGrid &tg, int tw, int th,
                        int dataDim0, int dataDim1, float out[4])
{
    double potNormX = (double)dataDim0 / (double)tw;
    double potNormY = (double)dataDim1 / (double)th;
    if (potNormX < 1e-9) potNormX = 1.0;
    if (potNormY < 1e-9) potNormY = 1.0;
    out[0] = (float)((double)tw / potNormX * tg.lonstep);                 // kU
    out[1] = (float)(tg.lon_min + ((double)tg.repeat - 1.5) * tg.lonstep); // cU
    out[2] = (float)((double)th / potNormY * tg.latstep);                 // kV
    out[3] = (float)(tg.lat_min - 1.5 * tg.latstep);                      // cV
}

void MakeLandGeo(const LandLayer &L, float out[4])
{
    if (fabs(L.lonStep) < 1e-12 || fabs(L.latStep) < 1e-12 ||
        L.tw <= 0 || L.th <= 0) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    out[0] = (float)(1.0 / (L.lonStep * (double)L.tw));
    out[1] = (float)((1.5 - L.lonMin / L.lonStep) / (double)L.tw);
    out[2] = (float)(1.0 / (L.latStep * (double)L.th));
    out[3] = (float)((1.5 - L.latMin / L.latStep) / (double)L.th);
}

//===================================================================
// Shared grid overlay renderer
