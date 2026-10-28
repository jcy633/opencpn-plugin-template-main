//===================================================================
// Auto-split from ncdfoverlayfactory.cpp
//===================================================================

#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"
#include "ncdf_texture_internal.h"
#include "shader/ncdf_shader.h"
#include <thread>

extern void ncdfLog(const char* format, ...);

#ifndef PI
#define PI 3.14159265
#endif

#include "IsoLine2.h"

//===================================================================
// Mask-aware cubic B-spline IIR prefilter
// Processes each row/column's contiguous ocean segments independently.
// Land boundaries reset the IIR state, preventing land values from
// contaminating ocean coefficients near coastlines.
//===================================================================
//===================================================================
bool ncdfOverlayFactory::RenderScalarColorMap(
    PlugIn_ViewPort *vp, MainDialog *gui, ncdf_pi *plugin,
    ScalarOverlayState &st,
    ColorFunc colorFunc, float lutMin, float lutMax,
    double** animatedGrid)
{
    if (!gui) return false;
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return false;

    // Texture reuse: GPU texture already exists, just draw it (view pan / refresh)
    if (*st.p_hasTexture && !*st.p_needsRebuild && !animatedGrid) {
        ncdfLog("[render:scalar] REUSE: hasTex=%d needsRebuild=%d coefMin=%f coefMax=%f\n",
            (int)*st.p_hasTexture, (int)*st.p_needsRebuild, *st.p_cachedCoefMin, *st.p_cachedCoefMax);
        RenderSettings settings;
        settings.vectorMode = false;
        settings.uGrid = NULL;
        settings.vGrid = NULL;
        settings.precompCoeffU = NULL;
        settings.precompCoeffV = NULL;
        settings.precompScalarCoeff = NULL;
        settings.dataMin = *st.p_cachedCoefMin;
        settings.dataMax = *st.p_cachedCoefMax;
        settings.physMin = *st.p_cachedPhysMin;
        settings.physMax = *st.p_cachedPhysMax;
        settings.dataMinV = 0.0f; settings.dataMaxV = 1.0f;
        settings.physMinV = 0.0f; settings.physMaxV = 1.0f;
        settings.lutMin = lutMin;
        settings.lutMax = lutMax;
        RenderGridOverlay(vp, (float**)(size_t)1, colorFunc, settings,
            *st.p_glTexture, *st.p_hasTexture, *st.p_needsRebuild,
            st.p_dataDim, st.p_glDim, *st.p_lutID, *st.p_hasLUT,
            *st.p_uploadBuf, *st.p_uploadBufSize,
                st.p_physicalTexID, st.p_hasPhysicalTex);
        return true;
    }

    // Texture missing but coefficients exist — recreate texture without recomputing
    if (*st.p_cachedCoeff && !*st.p_hasTexture && !animatedGrid) {
        ncdfLog("[render:scalar] RECREATE tex from cached coeff: cachedCoeff=%p\n", (void*)*st.p_cachedCoeff);
        RenderSettings settings;
        settings.vectorMode = false;
        settings.uGrid = NULL; settings.vGrid = NULL;
        settings.precompCoeffU = NULL; settings.precompCoeffV = NULL;
        settings.precompScalarCoeff = *st.p_cachedCoeff;
        settings.precompScalarCoefMin = *st.p_cachedCoefScalarMin;
        settings.precompScalarCoefMax = *st.p_cachedCoefScalarMax;
        settings.precompScalarPhysMin = *st.p_cachedPhysScalarMin;
        settings.precompScalarPhysMax = *st.p_cachedPhysScalarMax;
        settings.dataMin = *st.p_cachedCoefMin;
        settings.dataMax = *st.p_cachedCoefMax;
        settings.physMin = *st.p_cachedPhysMin;
        settings.physMax = *st.p_cachedPhysMax;
        settings.dataMinV = 0.0f; settings.dataMaxV = 1.0f;
        settings.physMinV = 0.0f; settings.physMaxV = 1.0f;
        settings.lutMin = lutMin; settings.lutMax = lutMax;
        float** rebuildGrid = *st.p_cachedBaseGrid ? st.p_cachedBaseGrid->get() : NULL;
        RenderGridOverlay(vp, rebuildGrid, colorFunc, settings,
            *st.p_glTexture, *st.p_hasTexture, *st.p_needsRebuild,
            st.p_dataDim, st.p_glDim, *st.p_lutID, *st.p_hasLUT,
            *st.p_uploadBuf, *st.p_uploadBufSize,
                st.p_physicalTexID, st.p_hasPhysicalTex);
        if (*st.p_hasTexture) {
            *st.p_cachedCoefMin = settings.dataMin;
            *st.p_cachedCoefMax = settings.dataMax;
            *st.p_cachedPhysMin = settings.physMin;
            *st.p_cachedPhysMax = settings.physMax;
        }
        *st.p_needsRebuild = false;
        return true;
    }

    // First frame: need coefficients and texture
    if (!*st.p_cachedCoeff) {
        ncdfLog("[render:scalar] NO COEFF: cachedCoeff=NULL, cannot render\n");
        return false;
    }
    ncdfLog("[render:scalar] FULL REBUILD: cachedCoeff=%p hasTex=%d needsRebuild=%d\n",
        (void*)*st.p_cachedCoeff, (int)*st.p_hasTexture, (int)*st.p_needsRebuild);

    // Convert animation grid (double**) to float** if needed
    float** animFloatGrid = NULL;
    if (animatedGrid) {
        animFloatGrid = (float**)malloc(nj * sizeof(float*));
        if (animFloatGrid) {
            for (int j = 0; j < nj; j++) {
                animFloatGrid[j] = (float*)malloc(ni * sizeof(float));
                for (int i = 0; i < ni; i++) {
                    double val = animatedGrid[j][i];
                    animFloatGrid[j][i] = (val == ncdf_NOTDEF || !isfinite(val)) ? NAN : (float)val;
                }
            }
        }
    }

    RenderSettings settings;
    settings.vectorMode = false;
    settings.uGrid = NULL; settings.vGrid = NULL;
    settings.precompCoeffU = NULL; settings.precompCoeffV = NULL;
    settings.precompScalarCoeff = *st.p_cachedCoeff;
    settings.precompScalarCoefMin = *st.p_cachedCoefScalarMin;
    settings.precompScalarCoefMax = *st.p_cachedCoefScalarMax;
    settings.precompScalarPhysMin = *st.p_cachedPhysScalarMin;
    settings.precompScalarPhysMax = *st.p_cachedPhysScalarMax;
    // Use coefficient range for B-spline denormalization, physical range for fallback
    settings.dataMin = *st.p_cachedCoefMin;
    settings.dataMax = *st.p_cachedCoefMax;
    settings.physMin = *st.p_cachedPhysScalarMin;  // Physical value range (from prepareData)
    settings.physMax = *st.p_cachedPhysScalarMax;
    settings.dataMinV = 0.0f; settings.dataMaxV = 1.0f;
    settings.physMinV = 0.0f; settings.physMaxV = 1.0f;
    settings.lutMin = lutMin; settings.lutMax = lutMax;

    float** gridPtr = animatedGrid ? (float**)animatedGrid :
        (*st.p_cachedBaseGrid ? st.p_cachedBaseGrid->get() : NULL);
    ncdfLog("[debug] RenderScalarColorMap: p_cachedBaseGrid=%p gridPtr=%p hasCoeff=%d\n",
        (void*)st.p_cachedBaseGrid, (void*)gridPtr, (int)(*st.p_cachedCoeff != NULL));
    RenderGridOverlay(vp, gridPtr, colorFunc, settings,
        *st.p_glTexture, *st.p_hasTexture, *st.p_needsRebuild,
        st.p_dataDim, st.p_glDim, *st.p_lutID, *st.p_hasLUT,
        *st.p_uploadBuf, *st.p_uploadBufSize,
            st.p_physicalTexID, st.p_hasPhysicalTex);

    // Free temporary animation float grid
    if (animFloatGrid) {
        for (int j = 0; j < nj; j++) free(animFloatGrid[j]);
        free(animFloatGrid);
    }

    // CPU caches (cachedCoeff, cachedBaseGrid) stay alive for reuse on next time step.
    // setData() will clear them when new data arrives.

    if (*st.p_hasTexture) {
        *st.p_cachedCoefMin = settings.dataMin;
        *st.p_cachedCoefMax = settings.dataMax;
        *st.p_cachedPhysMin = settings.physMin;
        *st.p_cachedPhysMax = settings.physMax;
    }
    return true;
}

//===================================================================
// Shared utilities
//===================================================================

// Sea temperature color map (-2°C to 32°C, purple→red, smoothstep)
wxColour ncdfOverlayFactory::InterpolateStops(const double stops[][4], int nStops, double val, bool smooth)
{
    if (val >= stops[nStops - 1][0])
        return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
    if (val <= stops[0][0])
        return wxColour((unsigned char)stops[0][1], (unsigned char)stops[0][2], (unsigned char)stops[0][3]);

    for (int i = 1; i < nStops; i++) {
        if (val <= stops[i][0]) {
            double range = stops[i][0] - stops[i-1][0];
            double t = (range > 0) ? (val - stops[i-1][0]) / range : 0;
            if (smooth) t = t * t * (3.0 - 2.0 * t);
            unsigned char r = (unsigned char)(stops[i-1][1] + t * (stops[i][1] - stops[i-1][1]));
            unsigned char g = (unsigned char)(stops[i-1][2] + t * (stops[i][2] - stops[i-1][2]));
            unsigned char b = (unsigned char)(stops[i-1][3] + t * (stops[i][3] - stops[i-1][3]));
            return wxColour(r, g, b);
        }
    }
    return wxColour((unsigned char)stops[nStops-1][1], (unsigned char)stops[nStops-1][2], (unsigned char)stops[nStops-1][3]);
}

// dataGeo: data-UV -> 经纬度；landGeoC/landGeoF: 经纬度 -> 掩膜 UV。
// 补空白 pass 用它们查 GSHHG 陆海掩膜（必须绕道经纬度，见 MakeDataGeo 说明）。
static void DrawShaderPath(PlugIn_ViewPort *vp,
                           ncdfOverlayFactory::RenderSettings &settings,
                           GLuint texID, GLuint lutID, bool hasLUT,
                           GLuint *physicalTexID, bool *hasPhysicalTex,
                           TileGrid &tg, int tw, int th, int landMode,
                           GLuint landTexC = 0, GLuint landTexF = 0,
                           const float *dataGeo = NULL,
                           const float *landGeoC = NULL, const float *landGeoF = NULL)
{
    ncdf_shader_use_program(ncdf_shader_get_grid_program());
    NcdfShaderUniforms u = ncdf_shader_get_uniforms();
    if (u.landMode >= 0) ncdf_shader_uniform_1i(u.landMode, landMode);
    if (u.dbgFill >= 0) ncdf_shader_uniform_1f(u.dbgFill,
        (s_dbg.dbgFill && landMode == NCDF_LANDMODE_FILL) ? 1.0f : 0.0f);
    if (u.texSize >= 0) ncdf_shader_uniform_2f(u.texSize, (float)tw, (float)th);
    if (u.vectorMode >= 0) ncdf_shader_uniform_1i(u.vectorMode, settings.vectorMode ? 1 : 0);
    if (u.dataMin >= 0) ncdf_shader_uniform_1f(u.dataMin, settings.dataMin);
    if (u.dataMax >= 0) ncdf_shader_uniform_1f(u.dataMax, settings.dataMax);
    if (u.dataMinV >= 0) ncdf_shader_uniform_1f(u.dataMinV, settings.dataMinV);
    if (u.dataMaxV >= 0) ncdf_shader_uniform_1f(u.dataMaxV, settings.dataMaxV);
    if (u.lutMin >= 0) ncdf_shader_uniform_1f(u.lutMin, settings.lutMin);
    if (u.lutMax >= 0) ncdf_shader_uniform_1f(u.lutMax, settings.lutMax);
    if (u.physMin >= 0) ncdf_shader_uniform_1f(u.physMin, settings.physMin);
    if (u.physMax >= 0) ncdf_shader_uniform_1f(u.physMax, settings.physMax);
    if (u.physMinV >= 0) ncdf_shader_uniform_1f(u.physMinV, settings.physMinV);
    if (u.physMaxV >= 0) ncdf_shader_uniform_1f(u.physMaxV, settings.physMaxV);
    if (u.dataTex >= 0) {
        ncdf_shader_active_texture(0x84C0);
        glBindTexture(GL_TEXTURE_2D, texID);
        ncdf_shader_uniform_1i(u.dataTex, 0);
    }
    if (u.colorLUT >= 0 && hasLUT) {
        ncdf_shader_active_texture(0x84C1);
        glBindTexture(GL_TEXTURE_2D, lutID);
        ncdf_shader_uniform_1i(u.colorLUT, 1);
    }
    // physicalTex: vector mode 的 readPhysical 要用，补空白 pass（landMode==4）
    // 两种模式都直接从它取最近海点的值，所以这里不能只在 vectorMode 时绑定。
    if (u.physicalTex >= 0 &&
        (settings.vectorMode || landMode == NCDF_LANDMODE_FILL) && hasPhysicalTex && *hasPhysicalTex) {
        ncdf_shader_active_texture(0x84C2);
        glBindTexture(GL_TEXTURE_2D, *physicalTexID);
        ncdf_shader_uniform_1i(u.physicalTex, 2);
    }
    // GSHHG 陆海掩膜（补空白 pass 用来避开陆地）
    if (u.landMaskTex >= 0) {
        ncdf_shader_active_texture(0x84C3);
        glBindTexture(GL_TEXTURE_2D, landTexC ? landTexC : landTexF);
        ncdf_shader_uniform_1i(u.landMaskTex, 3);
    }
    if (u.landMaskTex2 >= 0) {
        ncdf_shader_active_texture(0x84C4);
        glBindTexture(GL_TEXTURE_2D, landTexF ? landTexF : landTexC);
        ncdf_shader_uniform_1i(u.landMaskTex2, 4);
    }
    if (u.dataGeo >= 0) {
        if (dataGeo) ncdf_shader_uniform_4f(u.dataGeo, dataGeo[0], dataGeo[1], dataGeo[2], dataGeo[3]);
        else         ncdf_shader_uniform_4f(u.dataGeo, 0, 0, 0, 0);
    }
    if (u.landGeoC >= 0) {
        const float *x = landGeoC ? landGeoC : landGeoF;
        if (x) ncdf_shader_uniform_4f(u.landGeoC, x[0], x[1], x[2], x[3]);
        else   ncdf_shader_uniform_4f(u.landGeoC, 0, 0, 0, 0);
    }
    if (u.landGeoF >= 0) {
        const float *x = landGeoF ? landGeoF : landGeoC;
        if (x) ncdf_shader_uniform_4f(u.landGeoF, x[0], x[1], x[2], x[3]);
        else   ncdf_shader_uniform_4f(u.landGeoF, 0, 0, 0, 0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ForEachTile(vp, tg, DrawShaderTile, NULL);

    // Restore fixed-function state
    ncdf_shader_use_program(0);
    ncdf_shader_active_texture(0x84C0);
    glDisable(GL_BLEND);
}

//===================================================================
// 两个坐标映射，补空白 pass 查 GSHHG 掩膜用
//
// dataGeo : data-UV -> 经纬度
//   ComputeTileUVs: u = ((lon-lon_min)/lonstep - repeat + 1.5)/tw * potNormX
//   反解  lon = kU*u + cU,  lat = kV*v + cV
// landGeo : 经纬度 -> 掩膜 UV
//   ComputeLandUVs: uL = ((lon-L.lonMin)/L.lonStep + 1.5)/L.tw
//   =>  uL = aL*lon + bL
//
// 为什么绕道经纬度：全世界数据的 dataTex 是 GL_REPEAT，vUV.x 在日期变更线
// 会 1->0 绕回。若直接用 dataUV 的仿射去查掩膜，那个映射对 u 不是周期的，
// 绕回处掩膜 UV 会平移一整格宽度，陆地判定跟着翻转，画面上就是日期变更线
// 处一条硬竖边，而且深处陆地会被误判成海。经度规范化到 [-180,180) 后就没有
// 绕回这回事了。
//===================================================================
static void MakeDataGeo(const TileGrid &tg, int tw, int th,
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

static void MakeLandGeo(const LandLayer &L, float out[4])
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
//===================================================================
void ncdfOverlayFactory::RenderGridOverlay(PlugIn_ViewPort *vp,
                                           float **grid,
                                           ColorFunc colorFunc,
                                           RenderSettings &settings,
                                           GLuint &texID, bool &hasTex, bool &needsRebuild,
                                           int dataDim[2], int glDim[2],
                                           GLuint &lutID, bool &hasLUT,
                                           unsigned char *&uploadBuf, int &uploadBufSize,
                                           GLuint *physicalTexID, bool *hasPhysicalTex)
{
    // [步骤5f] 逐帧日志已删：每帧每层都写，4 层 60fps 约 200 MB/小时
    if (!gui || !vp) return;
    if (!grid && !settings.vectorMode) return;  // grid required for scalar mode only
    int ni = gui->myMessage.lonLength;
    int nj = gui->myMessage.latLength;
    if (ni < 2 || nj < 2) return;

    int tw = 0, th = 0;

    if (!m_pdc) {
#ifdef ocpnUSE_GL
        // (逐帧日志已删)
        // --- Texture creation/update ---
        double lonMin = tlon, lonMax = blon;
        double gridSpacingLon = (lonMax - lonMin) / (ni - 1);
        bool repeat = (lonMax - lonMin + gridSpacingLon >= 360);
        int borderH = repeat ? 0 : 1;
        int extraCol = repeat ? 1 : 0;
        int expectedTw = ni + 2 * borderH + extraCol;
        int expectedTh = nj + 2;
        // (逐帧日志已删)

        bool dataChanged = needsRebuild || !hasTex;
        if (needsRebuild) {
            if (hasTex && texID && (glDim[0] != expectedTw || glDim[1] != expectedTh)) {
                glDeleteTextures(1, &texID); texID = 0;
                hasTex = false;
            }
        }

        if (!hasTex || dataChanged) {
            const ULONGLONG tTex0 = ::GetTickCount64();
            tw = expectedTw; th = expectedTh;
            int _bufSize = tw * th * 4;
            // Allocate texture buffer on demand (freed after upload)
            unsigned char *texData = (unsigned char*)malloc(_bufSize);
            if (!texData) return;
            memset(texData, 0, _bufSize);
            ncdfLog("[render] RenderGridOverlay: buf allocated, tw=%d th=%d bufSize=%d\n", tw, th, _bufSize);

            TexBuildContext ctx = { gui, ni, nj, tw, th, borderH, repeat,
                                   texData, tlon, tlat, blon, blat };

            if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                ncdfLog("[render] RenderGridOverlay: FillVectorCoeffTex\n");
                FillVectorCoeffTex(ctx, settings);
            } else {
                ncdfLog("[render] RenderGridOverlay: FillScalarCoeffTex, precomp=%p\n", (void*)settings.precompScalarCoeff);
                FillScalarCoeffTex(ctx, grid, settings,
                    settings.precompScalarCoeff,
                    settings.precompScalarCoefMin, settings.precompScalarCoefMax,
                    settings.precompScalarPhysMin, settings.precompScalarPhysMax);
            }
            ncdfLog("[render] RenderGridOverlay: FillCoeff done\n");
            const ULONGLONG tA = ::GetTickCount64();
            BuildLUTTex(colorFunc, settings, lutID, hasLUT);

            ApplyTextureBorders(ctx);
            // 注意：不再对 texData 做 FillTextureGaps。它会把无数据格点（陆地）
            // 填成海洋值并把 alpha 改写成 255，既毁掉 FillLandFractionChannel 的
            // `a == 1` 陆地标记，也让着色器的 2x2 判陆彻底失效。空白带改由
            // BuildPhysicalTex 的最近邻传播 + landMode==4 的 gap-fill pass 处理。
            FillLandFractionChannel(ctx);   // B 通道 = 亚格点陆地比例
            const ULONGLONG tB = ::GetTickCount64();
            ncdfLog("[render] RenderGridOverlay: UploadCoeffTexture\n");
            UploadCoeffTexture(texID, hasTex, tw, th, repeat, dataDim, glDim, texData);
            const ULONGLONG tC = ::GetTickCount64();
            free(texData);  // Release texture buffer after GPU upload
            BuildPhysicalTex(ctx, grid, settings, repeat, physicalTexID, hasPhysicalTex);
            const ULONGLONG tD = ::GetTickCount64();
            ncdfLog("[render] RenderGridOverlay: texture uploaded, needsRebuild cleared\n");
            ncdfLog("[perf] texture build total=%llu ms | coeff=%llu lut+borders+landfrac=%llu upload=%llu physfill=%llu\n",
                    (unsigned long long)(::GetTickCount64() - tTex0),
                    (unsigned long long)(tA - tTex0),
                    (unsigned long long)(tB - tA),
                    (unsigned long long)(tC - tB),
                    (unsigned long long)(tD - tC));
            needsRebuild = false;  // Always clear after successful upload
        }  // end if (!hasTex || dataChanged)

        // GSHHG 高分辨率陆地图层：数据只上色，海岸线由掩膜纹理逐像素画出
    if (gui && gui->pPlugIn)
        BuildLandLayers(gui->pPlugIn->m_landMask,
                        gui->pPlugIn->m_landMaskCoarse.valid() ? &gui->pPlugIn->m_landMaskCoarse : NULL,
                        tlat, tlon, blat, blon, repeat);

    // --- Draw tiled texture (GRIB pattern) ---
        if (hasTex && texID) {
            // Reuse tlon/tlat/blon/blat from class members for draw context
            TexBuildContext drawCtx = { gui, ni, nj, glDim[0], glDim[1], 0, false,
                                       NULL, tlon, tlat, blon, blat };
            const ULONGLONG tT0 = ::GetTickCount64();
            TileGrid tg = ComputeTileUVs(vp, drawCtx, dataDim, glDim);
            g_renderTimer.tileUV += (double)(::GetTickCount64() - tT0);
            if (!tg.lva) return;

            if (ncdf_shader_initialized() && ncdf_shader_get_grid_program() > 0) {
                // 有 GSHHG 陆地层 -> CLIP（数据 pass 自裁到海，陆地留给陆地层）
                // 没有但面板开着 -> PAINT（数据 pass 按 B 通道自己画陆地色）
                int landMode = LandLayersActive() ? NCDF_LANDMODE_CLIP
                              : (gui->IsShown() ? NCDF_LANDMODE_PAINT : NCDF_LANDMODE_CLIP);
                LoadDbgFlags();
                if (s_dbg.sea) {
                    const ULONGLONG tS = ::GetTickCount64();
                    DrawShaderPath(vp, settings, texID, lutID, hasLUT,
                                   physicalTexID, hasPhysicalTex,
                                   tg, glDim[0], glDim[1], landMode);
                    g_renderTimer.sea += (double)(::GetTickCount64() - tS);
                }

                // [步骤5b] 逐层的 GSHHG 陆地绘制已移除，只保留 DoRenderncdfOverlay
                // 末尾统一画一次（顺序变成 海色→填补 → … → 陆地收尾）。

                // ---- 填补 GSHHG 与海色之间的空白带 ----
                // 取色 = 最近的海的点的颜色，纯点取色、不跨陆地插值。
                // 关键：本 pass 自己查 GSHHG 陆海掩膜，**绝不在陆地上画**，
                // 不依赖陆地层事后去盖。一旦靠事后盖，哪一侧的覆盖差一点，
                // 海色就被顶到海岸线外面去，表现为「海域外围多出一圈」。
                if (s_dbg.fill && physicalTexID && hasPhysicalTex && *hasPhysicalTex) {
                    // 诊断：补空白若在无掩膜时跑，其陆地判定必然失效（查不到陆地，
                    // 会把海底色涂到陆地上）。日志里出现这条就说明是这个原因。
                    if (!s_landLayer.hasTex) {
                        static bool s_warnedNoMask = false;
                        if (!s_warnedNoMask) {
                            s_warnedNoMask = true;
                            ncdfLog("[landlayer] WARN: gap-fill running with NO land mask (hasTex=0) -> land painted seabed\n");
                        }
                    }
                    float dGeo[4], gC[4], gF[4];
                    MakeDataGeo(tg, glDim[0], glDim[1], dataDim[0], dataDim[1], dGeo);
                    MakeLandGeo(s_landLayer, gC);
                    MakeLandGeo(s_landLayer, gF);
                    const ULONGLONG tF = ::GetTickCount64();
                    DrawShaderPath(vp, settings, texID, lutID, hasLUT,
                                   physicalTexID, hasPhysicalTex,
                                   tg, glDim[0], glDim[1], NCDF_LANDMODE_FILL,
                                   s_landLayer.hasTex ? s_landLayer.texID : 0,
                                   s_landLayer.hasTex ? s_landLayer.texID : 0,
                                   dGeo,
                                   s_landLayer.hasTex ? gC : NULL,
                                   s_landLayer.hasTex ? gF : NULL);
                    g_renderTimer.fill += (double)(::GetTickCount64() - tF);
                }

                // 分块统计：如果隐形线来自某个分块被误剔，这里会直接显示出来
                {
                    static unsigned int s_logTick = 0;
                    if ((s_logTick++ % 120) == 0)
                        ncdfLog("[tiles] xs=%d ys=%d drawn=%u skipUV=%u skipU1<=U0=%u "
                                "| flags sea=%d land=%d fill=%d tileX=%d tileY=%d\n",
                                tg.xs, tg.ys, s_tileDrawn, s_tileSkipUV, s_tileSkipU,
                                (int)s_dbg.sea, (int)s_dbg.land, (int)s_dbg.fill,
                                s_dbg.tileX, s_dbg.tileY);
                }
            }
            delete[] tg.lva;
            g_renderTimer.n++;
            if (g_renderTimer.n >= 120) FlushRenderTimer();
        }
#endif
    }
}

// Generic isoline renderer: CPU Marching Squares + glBegin/glEnd
void ncdfOverlayFactory::DrawIsoLines(PlugIn_ViewPort *vp,
                         double **grid, int ni, int nj,
                         bool &needsRebuild,
                         std::vector<ncdfIsoSeg> &segments,
                         double tlat, double tlon, double blat, double blon,
                         double jDirectionIncr)
{
    if (!vp || !grid || ni < 2 || nj < 2) return;

    // Rebuild segments when data changes
    if (needsRebuild) {
        segments.clear();
        double dMin = 1e10, dMax = -1e10;
        for (int j = 0; j < nj; j++) {
            if (!grid[j]) break;
            for (int i = 0; i < ni; i++) {
                double v = grid[j][i];
                if (v == ncdf_NOTDEF || v != v || !isfinite(v)) continue;
                if (v < dMin) dMin = v;
                if (v > dMax) dMax = v;
            }
        }
        if (dMin < dMax) {
            // Adaptive spacing: ~10 isolines, rounded to "nice" numbers
            double range = dMax - dMin;
            double rawSpacing = range / 10.0;
            double mag = pow(10.0, floor(log10(rawSpacing)));
            double norm = rawSpacing / mag;
            double spacing;
            if (norm < 1.5) spacing = mag;
            else if (norm < 3.5) spacing = 2.0 * mag;
            else if (norm < 7.5) spacing = 5.0 * mag;
            else spacing = 10.0 * mag;
            if (spacing < 1e-10) spacing = 1.0;

            dMin = floor(dMin / spacing) * spacing;
            dMax = ceil(dMax / spacing) * spacing;
            double lat_origin, lon_origin;
            if (jDirectionIncr >= 0) {
                // South-to-north: j=0 at south boundary
                lat_origin = blat;  // min latitude
            } else {
                // North-to-south: j=0 at north boundary
                lat_origin = tlat;  // max latitude
            }
            lon_origin = tlon;  // min longitude
            double incrLat = fabs((tlat - blat) / (nj - 1));
            if (jDirectionIncr < 0) incrLat = -incrLat;
            double incrLon = (blon - lon_origin) / (ni - 1);
            for (double val = dMin; val <= dMax; val += spacing) {
                IsoLine isoLine(val, grid, nj, ni, lat_origin, lon_origin, incrLat, incrLon);
                std::list<Segment*>& trace = isoLine.getTrace();
                for (std::list<Segment*>::iterator it = trace.begin(); it != trace.end(); ++it) {
                    Segment *seg = *it;
                    ncdfIsoSeg s = {seg->py1, seg->px1, seg->py2, seg->px2, val};
                    segments.push_back(s);
                }
            }
        }
        needsRebuild = false;
    }

    if (segments.empty()) return;

#ifdef ocpnUSE_GL
    glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glLineWidth(1.0f);
        glColor4ub(80, 80, 80, 220);

        glBegin(GL_LINES);
        for (size_t k = 0; k < segments.size(); k++) {
            const ncdfIsoSeg& s = segments[k];
            wxPoint ab, cd;
            GetCanvasPixLL(vp, &ab, s.lat1, s.lon1);
            GetCanvasPixLL(vp, &cd, s.lat2, s.lon2);
            if (fabs((double)ab.x - cd.x) > vp->pix_width / 2) continue;
            glVertex2i(ab.x, ab.y);
            glVertex2i(cd.x, cd.y);
        }
        glEnd();

        glDisable(GL_LINE_SMOOTH);
        glDisable(GL_BLEND);
#endif
}
