//===================================================================
// ncdf_texture_build.cpp —— 纹理构建
// 由 ncdftexture.cpp 拆出（2026-10-28）。类型与跨文件函数声明见
// include/ncdf_texture_internal.h。这里只是代码搬移，无逻辑改动。
//===================================================================

#include "ncdf_texture_internal.h"
#include "ncdfoverlayfactory.h"
#include "ncdf.h"
#include "ncdf_pi.h"
#include "ncdfdata.h"
#include "shader/ncdf_shader.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

extern void ncdfLog(const char* format, ...);

//===================================================================
void FillCoeffTexture(TexBuildContext &ctx,
                             const float* coeffR, float coefMinR, float coefRangeR,
                             const float* coeffG, float coefMinG, float coefRangeG,
                             const unsigned char* validMask)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    int borderH = ctx.borderH;
    unsigned char *texData = ctx.texData;
    if (coefRangeR < 1e-10f) coefRangeR = 1.0f;
    if (coefRangeG < 1e-10f) coefRangeG = 1.0f;
    for (int j = 0; j < nj; j++) {
        int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
        for (int i = 0; i < ni; i++) {
            int x = i + borderH, y = texRow + 1;
            if (x >= tw - 1 || y >= th - 1) continue;
            int off = 4 * (y * tw + x);
            int mi = j * ni + i;
            if (validMask && !validMask[mi]) {
                // Land: a=1 (≈0.004 in shader, 0.001<ca<0.01 → green)
                // Border: a=0 (set by ApplyTextureBorders → transparent)
                texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 1;
            } else {
                texData[off]   = (unsigned char)(fmin(fmax((coeffR[mi] - coefMinR) / coefRangeR, 0.0f), 1.0f) * 255.0f + 0.5f);
                texData[off+1] = (unsigned char)(fmin(fmax((coeffG[mi] - coefMinG) / coefRangeG, 0.0f), 1.0f) * 255.0f + 0.5f);
                texData[off+2] = 0; texData[off+3] = 255;
            }
        }
    }
}

//===================================================================
// (a) Vector mode coefficient texture fill
//===================================================================
void FillVectorCoeffTex(TexBuildContext &ctx,
                               ncdfOverlayFactory::RenderSettings &settings)
{
    int ni = ctx.ni, nj = ctx.nj;
    bool repeat = ctx.repeat;
    float coefMinU, coefMaxU, coefMinV, coefMaxV;

    if (settings.precompCoeffU && settings.precompCoeffV) {
        // Use pre-computed B-spline coefficients (cached from prepareData)
        coefMinU = settings.precompCoefU_min; coefMaxU = settings.precompCoefU_max;
        coefMinV = settings.precompCoefV_min; coefMaxV = settings.precompCoefV_max;
        // Set dataMin/Max to physical value range (coefRange * bsplineScale)
        const float bsplineScale = 2.5858f;
        settings.dataMin = coefMinU * bsplineScale;
        settings.dataMax = coefMaxU * bsplineScale;
        settings.dataMinV = coefMinV * bsplineScale;
        settings.dataMaxV = coefMaxV * bsplineScale;
        // Build valid mask from uGrid
        unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);
        if (validMask && settings.uGrid && settings.vGrid) {
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    float u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                    validMask[j*ni+i] = (isfinite(u) && isfinite(v)) ? 1 : 0;
                }
        }
        FillCoeffTexture(ctx, settings.precompCoeffU, coefMinU, coefMaxU - coefMinU,
                         settings.precompCoeffV, coefMinV, coefMaxV - coefMinV, validMask);
        if (validMask) free(validMask);
    } else {
        // No pre-computed coefficients — run full B-spline prefilter
        float dMin = 1e30f, dMax = -1e30f;
        for (int j = 0; j < nj; j++)
            for (int i = 0; i < ni; i++) {
                float u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                if (isfinite(u) && isfinite(v)) {
                    float spd = sqrtf(u*u + v*v);
                    if (spd < dMin) dMin = spd; if (spd > dMax) dMax = spd;
                }
            }
        if (dMax <= dMin) { dMin = 0; dMax = 1; }
        settings.dataMin = (float)dMin; settings.dataMax = (float)dMax;

        float* rawU = (float*)calloc(nj * ni, sizeof(float));
        float* rawV = (float*)calloc(nj * ni, sizeof(float));
        float* coeffU = (float*)calloc(nj * ni, sizeof(float));
        float* coeffV = (float*)calloc(nj * ni, sizeof(float));
        unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);

        if (rawU && rawV && coeffU && coeffV && validMask) {
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    float u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                    if (!isfinite(u) || !isfinite(v)) {
                        rawU[j*ni+i] = 0; rawV[j*ni+i] = 0; validMask[j*ni+i] = 0;
                    } else {
                        rawU[j*ni+i] = u; rawV[j*ni+i] = v; validMask[j*ni+i] = 1;
                    }
                }
            BicubicSplinePrefilterWrapAware(coeffU, rawU, validMask, ni, nj, repeat);
            BicubicSplinePrefilterWrapAware(coeffV, rawV, validMask, ni, nj, repeat);

            coefMinU = 1e30f; coefMaxU = -1e30f;
            coefMinV = 1e30f; coefMaxV = -1e30f;
            for (int k = 0; k < nj * ni; k++) {
                if (!validMask[k]) continue;
                if (coeffU[k] < coefMinU) coefMinU = coeffU[k];
                if (coeffU[k] > coefMaxU) coefMaxU = coeffU[k];
                if (coeffV[k] < coefMinV) coefMinV = coeffV[k];
                if (coeffV[k] > coefMaxV) coefMaxV = coeffV[k];
            }
            if (coefMaxU <= coefMinU) { coefMinU = 0; coefMaxU = 1; }
            if (coefMaxV <= coefMinV) { coefMinV = 0; coefMaxV = 1; }
            FillCoeffTexture(ctx, coeffU, coefMinU, coefMaxU - coefMinU,
                             coeffV, coefMinV, coefMaxV - coefMinV, validMask);
        }
        if (rawU) free(rawU); if (rawV) free(rawV);
        if (coeffU) free(coeffU); if (coeffV) free(coeffV);
        if (validMask) free(validMask);
    }

    const float bsplineScale = 2.5858f;
    settings.dataMin = coefMinU * bsplineScale;
    settings.dataMax = coefMaxU * bsplineScale;
    settings.dataMinV = coefMinV * bsplineScale;
    settings.dataMaxV = coefMaxV * bsplineScale;
}

//===================================================================
// (b) Scalar mode coefficient texture fill
// Receives pre-computed B-spline coefficients, writes to texture buffer.
// No B-spline computation here — coefficients come from overlay cache.
//===================================================================
void FillScalarCoeffTex(TexBuildContext &ctx,
                               float **grid,
                               ncdfOverlayFactory::RenderSettings &settings,
                               float *coeffBuf,
                               float coefMin, float coefMax,
                               float physMin, float physMax)
{
    int ni = ctx.ni, nj = ctx.nj;
    if (!coeffBuf) return;

    float coefRange = coefMax - coefMin;
    if (coefRange < 1e-10f) coefRange = 1.0f;
    float physRange = physMax - physMin;
    if (physRange < 1e-10f) physRange = 1.0f;

    int tw = ctx.tw, th = ctx.th, borderH = ctx.borderH;
    unsigned char *texData = ctx.texData;
    bool hasGrid = (grid && (uintptr_t)grid > 0x100);
    int landTexCount = 0, oceanTexCount = 0;
    // 逐行独立，并行化（每行自己的计数，最后汇总）
    std::vector<unsigned int> rowLand(nj, 0), rowOcean(nj, 0);
    parallelFor(nj, [&](int j) {
        int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
        for (int i = 0; i < ni; i++) {
            int x = i + borderH, y = texRow + 1;
            if (x >= tw - 1 || y >= th - 1) continue;
            int off = 4 * (y * tw + x);
            int k = j * ni + i;
            float val = hasGrid ? grid[j][i] : 0.0f;
            if (hasGrid && !isfinite(val)) {
                // Land: a=1 (≈0.004 in shader, 0.001<ca<0.01 → green)
                // Border: a=0 (set by ApplyTextureBorders → transparent)
                texData[off] = 0; texData[off+1] = 0; texData[off+2] = 0; texData[off+3] = 1;
                rowLand[j]++;
            } else {
                texData[off]   = (unsigned char)(fminf(fmaxf((coeffBuf[k] - coefMin) / coefRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                texData[off+1] = (unsigned char)(fminf(fmaxf((val - physMin) / physRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                texData[off+2] = 0; texData[off+3] = 255;
                rowOcean[j]++;
            }
        }
    });
    for (int j = 0; j < nj; j++) { landTexCount += (int)rowLand[j]; oceanTexCount += (int)rowOcean[j]; }

    ncdfLog("[debug] FillScalarCoeffTex: land(alpha=0)=%d ocean=%d hasGrid=%d\n",
        landTexCount, oceanTexCount, (int)hasGrid);

    const float bsplineScale = 2.5858f;
    settings.dataMin = coefMin * bsplineScale;
    settings.dataMax = coefMax * bsplineScale;
    settings.physMin = physMin;
    settings.physMax = physMax;
}

//===================================================================
// (c) LUT texture creation
//===================================================================
void BuildLUTTex(ncdfOverlayFactory::ColorFunc colorFunc,
                        ncdfOverlayFactory::RenderSettings &settings,
                        GLuint &lutID, bool &hasLUT)
{
    // Only rebuild when lutID doesn't exist (per-overlay, no static cache)
    if (!hasLUT || !lutID) {
        // Delete stale LUT from previous time step (Invalidate set hasLUT=false)
        if (lutID) { glDeleteTextures(1, &lutID); lutID = 0; }
        float lutRange = settings.lutMax - settings.lutMin;
        if (lutRange < 1e-10f) lutRange = 1.0f;
        unsigned char lutData[256 * 4];
        for (int k = 0; k < 256; k++) {
            double val = settings.lutMin + (k / 255.0) * lutRange;
            wxColour c = colorFunc(val);
            lutData[k*4] = c.Red(); lutData[k*4+1] = c.Green();
            lutData[k*4+2] = c.Blue(); lutData[k*4+3] = 255;
        }
        glGenTextures(1, &lutID);
        glBindTexture(GL_TEXTURE_2D, lutID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData);
        hasLUT = true;
    }
}

//===================================================================
// (d) Fixed-function path removed — shader only
//===================================================================

//===================================================================
// (e) Border/repeat handling
//===================================================================
void ApplyTextureBorders(TexBuildContext &ctx)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    bool repeat = ctx.repeat;
    unsigned char *texData = ctx.texData;

    // Repeat mode: duplicate first column at end for seamless wrapping
    if (repeat) {
        for (int j = 0; j < nj; j++) {
            int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
            int y = texRow + 1;
            if (y >= th - 1) continue;
            memcpy(texData + 4 * (y * tw + ni), texData + 4 * (y * tw + 0), 4);
        }
    }

    // GRIB-style border: copy adjacent row/col, then set border alpha=0
    memcpy(texData, texData + 4 * tw, 4 * tw);
    memcpy(texData + 4 * tw * (th - 1), texData + 4 * tw * (th - 2), 4 * tw);
    for (int x = 0; x < tw; x++) {
        texData[4 * x + 3] = 0;
        texData[4 * ((th - 1) * tw + x) + 3] = 0;
    }
    if (!repeat) {
        for (int y = 0; y < th; y++) {
            memcpy(texData + 4 * y * tw, texData + 4 * (y * tw + 1), 4);
            memcpy(texData + 4 * (y * tw + tw - 1), texData + 4 * (y * tw + tw - 2), 4);
        }
        for (int y = 0; y < th; y++) {
            texData[4 * y * tw + 3] = 0;
            texData[4 * (y * tw + tw - 1) + 3] = 0;
        }
    }
}

//===================================================================
// (e2) B 通道 = 亚格点陆地覆盖比例（0..255）
//
// 着色器用 B 做双线性覆盖度来决定海岸线位置。若载入了高分辨率陆海掩膜
// （ETOPO 2022），这里写入该 1/12° 格内被陆地覆盖的**比例**，海岸线位置
// 因此准到亚格点；否则退回二值（数据自身陆标号）。
// 数据本身没值的格点强制写成陆，避免在无数据处画出假的海色。
//===================================================================
void FillLandFractionChannel(TexBuildContext &ctx)
{
    MainDialog *gui = ctx.gui;
    if (!gui || !ctx.texData) return;

    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th, borderH = ctx.borderH;
    unsigned char *texData = ctx.texData;
    const ncdfDataMessage &msg = gui->myMessage;
    // GSHHG 岸线跟随面板显示：面板关着时退回数据自身陆标号，不画高分辨率岸线
    const NcdfLandMask *lm = (gui->IsShown() && gui->pPlugIn && gui->pPlugIn->m_landMask.valid())
                             ? &gui->pPlugIn->m_landMask : NULL;

    const double *latArr = msg.latValues;
    const double *lonArr = msg.lonValues;
    double dLat = fabs(msg.jDirectionIncr);
    double dLon = fabs(msg.iDirectionIncr);
    if (!(dLat > 0.0) || dLat > 10.0) dLat = 0.0833;
    if (!(dLon > 0.0) || dLon > 10.0) dLon = 0.0833;

    unsigned int dbgZero = 0, dbgFull = 0, dbgMid = 0, dbgN = 0;

    std::vector<unsigned int> rowZero(nj, 0), rowFull(nj, 0), rowMid(nj, 0);
    parallelFor(nj, [&](int j) {
        int texRow = (msg.jDirectionIncr >= 0) ? j : (nj - 1 - j);
        int y = texRow + 1;
        if (y >= th - 1) return;   // lambda 里跳过该行用 return，不是 continue
        double lat = latArr ? latArr[j] : 0.0;
        for (int i = 0; i < ni; i++) {
            int x = i + borderH;
            if (x >= tw - 1) continue;
            int off = 4 * (y * tw + x);
            unsigned char a = texData[off + 3];
            unsigned char frac;
            if (a == 0) {
                frac = 0;                      // 纹理边界：透明
            } else if (lm && lm->covers(lat, lonArr ? lonArr[i] : 0.0)) {
                // Per-cell O(1) test. The fine coastline comes from the land
                // LAYER textures, not from here -- this only decides where the
                // colour pass is allowed to draw, so a cheap binary test is
                // enough. (landFracInBox here cost 3.5 G ops and made the
                // colour map take seconds to appear.)
                double lon = lonArr ? lonArr[i] : 0.0;
                frac = lm->isLand(lat, lon) ? 255 : 0;
            } else {
                frac = (a == 1) ? 255 : 0;     // 无掩膜时退回数据自身陆标号
            }
            texData[off + 2] = frac;
            if (frac == 0) rowZero[j]++; else if (frac == 255) rowFull[j]++; else rowMid[j]++;
        }
    });
    for (int j = 0; j < nj; j++) {
        dbgZero += rowZero[j]; dbgFull += rowFull[j]; dbgMid += rowMid[j];
        dbgN += rowZero[j] + rowFull[j] + rowMid[j];
    }
    ncdfLog("[landfrac] texels=%u  B=0:%u (%.1f%%)  B=255:%u (%.1f%%)  mid:%u | lm=%s\n",
            dbgN, dbgZero, dbgN ? 100.0 * dbgZero / dbgN : 0.0,
            dbgFull, dbgN ? 100.0 * dbgFull / dbgN : 0.0, dbgMid,
            lm ? "yes" : "no");
}

void UploadCoeffTexture(GLuint &texID, bool &hasTex, int tw, int th,
                               bool repeat, int dataDim[2], int glDim[2],
                               unsigned char *texData)
{
    if (!hasTex) {
        // First time: create texture
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
        hasTex = true;
        dataDim[0] = tw; dataDim[1] = th;
        glDim[0] = tw;   glDim[1] = th;
    } else {
        // Update existing texture data (no GPU realloc, no OOM)
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, texData);
    }
}

//===================================================================
// (g) Physical texture creation (bilinear fallback)
//===================================================================
void BuildPhysicalTex(TexBuildContext &ctx,
                             float **grid,
                             ncdfOverlayFactory::RenderSettings &settings,
                             bool repeat,
                             GLuint *physicalTexID, bool *hasPhysicalTex)
{
    int ni = ctx.ni, nj = ctx.nj, tw = ctx.tw, th = ctx.th;
    int borderH = ctx.borderH;

    // === Create physical value texture for bilinear fallback ===
    if (physicalTexID && hasPhysicalTex) {
        unsigned char *physData = (unsigned char*)malloc(tw * th * 4);
        if (physData) {
            memset(physData, 0, tw * th * 4);
            bool hasGrid = (grid && (uintptr_t)grid > 0x100);
            // Find physical data range for normalization
            double physMin = 1e30, physMax = -1e30;
            double uMin = 1e30, uMax = -1e30;
            double vMin = 1e30, vMax = -1e30;
            for (int j = 0; j < nj; j++)
                for (int i = 0; i < ni; i++) {
                    if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                        float u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                        if (isfinite(u) && isfinite(v)) {
                            if (u < uMin) uMin = u; if (u > uMax) uMax = u;
                            if (v < vMin) vMin = v; if (v > vMax) vMax = v;
                        }
                    } else if (hasGrid && grid[j]) {
                        float val = grid[j][i];
                        if (isfinite(val)) {
                            if (val < physMin) physMin = val;
                            if (val > physMax) physMax = val;
                        }
                    }
                }
            if (settings.vectorMode) {
                // For vector: independent u/v ranges
                physMin = uMin; physMax = uMax;
            }
            if (physMax <= physMin) { physMin = 0; physMax = 1; }
            double physRange = physMax - physMin;
            if (physRange < 1e-10) physRange = 1.0;
            // V range (vector mode only)
            double physMinV = vMin, physMaxV = vMax;
            if (physMaxV <= physMinV) { physMinV = 0; physMaxV = 1; }
            double physRangeV = physMaxV - physMinV;
            if (physRangeV < 1e-10) physRangeV = 1.0;

            // Fill physical texture with normalized raw values
            parallelFor(nj, [&](int j) {
                int texRow = (ctx.gui->myMessage.jDirectionIncr >= 0) ? j : (nj - 1 - j);
                for (int i = 0; i < ni; i++) {
                    int x = i + borderH, y = texRow + 1;
                    if (x >= tw - 1 || y >= th - 1) continue;
                    int off = 4 * (y * tw + x);
                    if (settings.vectorMode && settings.uGrid && settings.vGrid) {
                        float u = settings.uGrid[j][i], v = settings.vGrid[j][i];
                        if (!isfinite(u) || !isfinite(v)) {
                            physData[off] = 0; physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 0;
                        } else {
                            physData[off]   = (unsigned char)(fminf(fmaxf((u - (float)physMin) / (float)physRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                            physData[off+1] = (unsigned char)(fminf(fmaxf((v - (float)physMinV) / (float)physRangeV, 0.0f), 1.0f) * 255.0f + 0.5f);
                            physData[off+2] = 0; physData[off+3] = 255;
                        }
                    } else if (hasGrid && grid[j]) {
                        float val = grid[j][i];
                        if (!isfinite(val)) {
                            physData[off] = 0; physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 0;
                        } else {
                            physData[off] = (unsigned char)(fminf(fmaxf((val - (float)physMin) / (float)physRange, 0.0f), 1.0f) * 255.0f + 0.5f);
                            physData[off+1] = 0; physData[off+2] = 0; physData[off+3] = 255;
                        }
                    }
                }
            });

            // ---- 最近邻传播：只为亚网格空白带供色 ----
            // 海洋模式分辨率低、GSHHG 分辨率高，海岸线附近有一条亚网格空白带
            // （海色 pass 因 2x2 有陆地而整点跳过的像素）。landMode==4 取「最近的
            // 海的点」的颜色来填这条带，这里的传播就是给它准备那个值。
            //   a=255 -> 本格自己有数据；b=255 -> 最近邻传播值；两者都 0 -> 无值。
            // 轮数刻意压到 2：只扩散到紧邻数据的那一两圈，够亚网格带取色即可。
            // 大范围无数据区（陆地内部、远处空洞）**不填**——陆地交给 GSHHG 层，
            // 那里用不到最近邻。传播只从有效海格点出发、只写无数据格点，既不用
            // 陆地值污染海色，也不改写有效格点，所以插值逻辑完全不受影响。
            {
                std::vector<int> src((size_t)tw * th, -1);
                for (int y = 1; y < th - 1; y++)
                    for (int x = borderH; x < tw - borderH; x++) {
                        size_t k = (size_t)y * tw + x;
                        if (physData[4 * k + 3] == 255) src[k] = (int)k;
                    }
                const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
                const int kMaxFillPasses = 2;
                unsigned int filled = 0;
                for (int pass = 0; pass < kMaxFillPasses; pass++) {
                    // 必须用快照判源：直接改 src 的话，一趟内刚填好的格点会立刻
                    // 成为新源，值就顺着扫描方向（向右、向下）一路传出去，画面上
                    // 就是成片的长横线/长竖线，根本不是最近邻。快照保证一趟只扩一格。
                    const std::vector<int> srcBefore(src);
                    // 行间独立（只读 srcBefore、写互不重叠的行），可并行
                    std::vector<unsigned int> rowFilled(th, 0);
                    parallelFor(th, [&](int y) {
                        if (y < 1 || y >= th - 1) return;
                        for (int x = borderH; x < tw - borderH; x++) {
                            size_t k = (size_t)y * tw + x;
                            if (src[k] >= 0) continue;
                            for (int d = 0; d < 4; d++) {
                                int nx = x + DX[d], ny = y + DY[d];
                                if (nx < 0 || nx >= tw || ny < 0 || ny >= th) continue;
                                size_t nk = (size_t)ny * tw + nx;
                                if (srcBefore[nk] >= 0) {
                                    size_t s = (size_t)srcBefore[nk];
                                    physData[4 * k]     = physData[4 * s];
                                    physData[4 * k + 1] = physData[4 * s + 1];
                                    physData[4 * k + 2] = 255;      // 标记：传播值
                                    // a 保持 0：本格依然没有自己的数据
                                    src[k] = srcBefore[nk];
                                    rowFilled[y]++;
                                    break;
                                }
                            }
                        }
                    });
                    bool changed = false;
                    for (int y = 0; y < th; y++) {
                        if (rowFilled[y]) { changed = true; filled += rowFilled[y]; }
                    }
                    if (!changed) break;
                }
                ncdfLog("[physfill] nearest-propagated %u cells (%d passes max) of %d\n",
                        filled, kMaxFillPasses, tw * th);
            }

            // Upload physical texture
            if (!(*hasPhysicalTex)) {
                // Delete stale texture from previous time step (Invalidate set hasPhysicalTex=false)
                if (*physicalTexID) { glDeleteTextures(1, physicalTexID); *physicalTexID = 0; }
                glGenTextures(1, physicalTexID);
                glBindTexture(GL_TEXTURE_2D, *physicalTexID);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, physData);
                *hasPhysicalTex = true;
            } else {
                glBindTexture(GL_TEXTURE_2D, *physicalTexID);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, physData);
            }
            settings.physMin = (float)physMin;
            settings.physMax = (float)physMax;
            settings.physMinV = (float)physMinV;
            settings.physMaxV = (float)physMaxV;
            free(physData);
        }
    }
}

//===================================================================
// (h) Tile UV computation
//===================================================================

//===================================================================
// 诊断开关 —— 定位「屏幕中间那条隐形线」用
