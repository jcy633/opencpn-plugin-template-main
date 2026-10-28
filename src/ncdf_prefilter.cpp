//===================================================================
// ncdf_prefilter.cpp —— B 样条预滤波
// 由 ncdftexture.cpp 拆出（2026-10-28）。类型与跨文件函数声明见
// include/ncdf_texture_internal.h。这里只是代码搬移，无逻辑改动。
//===================================================================

#include "ncdf_texture_internal.h"
#include "ncdfoverlayfactory.h"
#include "ncdfdata.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern void ncdfLog(const char* format, ...);

static void BicubicSplinePrefilterMasked(float* dst, const float* src,
                                          const unsigned char* mask, int w, int h) {
    const float z = -0.26794919243112270f;
    // inv = 1/(1-z): correct for 2D separable B-spline prefilter
    // causal steady state = K/(1-z), anti-causal produces same
    // reconstruction = (2/3)² * c * inv² * (1-z)² = (4/9) * K/(1-z)² * 1/(1-z)² * (1-z)² = K
    const float inv = 1.0f / (1.0f - z);

    memcpy(dst, src, (size_t)w * h * sizeof(float));

    // Horizontal pass —— 各行完全独立，可并行
    parallelFor(h, [&](int j) {
        float* row = dst + j * w;
        const unsigned char* mrow = mask + j * w;
        bool allValid = true;
        for (int i = 0; i < w; i++) { if (!mrow[i]) { allValid = false; break; } }

        if (allValid) {
            row[0] *= inv;
            for (int i = 1; i < w; i++) row[i] += z * row[i-1];
            row[w-1] *= inv;
            for (int i = w-2; i >= 0; i--) row[i] += z * row[i+1];
        } else {
            int segStart = -1;
            for (int i = 0; i <= w; i++) {
                bool valid = (i < w) && mrow[i];  // guard: mask[w] is past row end
                if (valid && segStart < 0) segStart = i;
                if ((!valid || i == w) && segStart >= 0) {
                    int segEnd = i - 1;
                    if (segEnd == segStart) {
                        row[segStart] *= inv;
                    } else {
                        row[segStart] *= inv;
                        for (int k = segStart + 1; k <= segEnd; k++) row[k] += z * row[k-1];
                        row[segEnd] *= inv;
                        for (int k = segEnd - 1; k >= segStart; k--) row[k] += z * row[k+1];
                    }
                    segStart = -1;
                }
            }
        }
    });

    // Vertical pass —— 各列完全独立，可并行（跨步访问，收益更大）
    parallelFor(w, [&](int i) {
        bool allValid = true;
        for (int j = 0; j < h; j++) { if (!mask[j * w + i]) { allValid = false; break; } }

        if (allValid) {
            dst[i] *= inv;
            for (int j = 1; j < h; j++) dst[j*w+i] += z * dst[(j-1)*w+i];
            dst[(h-1)*w+i] *= inv;
            for (int j = h-2; j >= 0; j--) dst[j*w+i] += z * dst[(j+1)*w+i];
        } else {
            int segStart = -1;
            for (int j = 0; j <= h; j++) {
                bool valid = (j < h) && mask[j * w + i];
                if (valid && segStart < 0) segStart = j;
                if ((!valid || j == h) && segStart >= 0) {
                    int segEnd = j - 1;
                    if (segEnd == segStart) {
                        dst[segStart*w+i] *= inv;
                    } else {
                        dst[segStart*w+i] *= inv;
                        for (int k = segStart + 1; k <= segEnd; k++) dst[k*w+i] += z * dst[(k-1)*w+i];
                        dst[segEnd*w+i] *= inv;
                        for (int k = segEnd - 1; k >= segStart; k--) dst[k*w+i] += z * dst[(k+1)*w+i];
                    }
                    segStart = -1;
                }
            }
        }
    });
}

//===================================================================
// Wrap-aware B-spline prefilter for global data
// Extends data by N columns on each side (wrapping longitude),
// runs prefilter on extended data, then extracts original range.
// This ensures coefficients are continuous across the dateline.
//===================================================================
static const int WRAP_EXT = 8;  // IIR convergence: |z|^8 ≈ 0.0002

void BicubicSplinePrefilterWrapAware(float* dst, const float* src,
                                             const unsigned char* mask,
                                             int w, int h, bool isGlobal) {
    if (!isGlobal) {
        BicubicSplinePrefilterMasked(dst, src, mask, w, h);
        return;
    }

    // Global data: extend columns for wrap-around
    int extW = w + 2 * WRAP_EXT;
    int totalPixels = extW * h;

    float* extSrc = (float*)calloc(totalPixels, sizeof(float));
    float* extDst = (float*)calloc(totalPixels, sizeof(float));
    unsigned char* extMask = (unsigned char*)calloc(totalPixels, 1);
    if (!extSrc || !extDst || !extMask) {
        if (extSrc) free(extSrc);
        if (extDst) free(extDst);
        if (extMask) free(extMask);
        BicubicSplinePrefilterMasked(dst, src, mask, w, h);
        return;
    }

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < extW; i++) {
            int origI = ((i - WRAP_EXT) % w + w) % w;
            int extOff = j * extW + i;
            int origOff = j * w + origI;
            extSrc[extOff] = src[origOff];
            extMask[extOff] = mask[origOff];
        }
    }

    BicubicSplinePrefilterMasked(extDst, extSrc, extMask, extW, h);

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            dst[j * w + i] = extDst[j * extW + WRAP_EXT + i];
        }
    }

    free(extSrc);
    free(extDst);
    free(extMask);
}

//===================================================================
// B-spline prefilter wrapper for overlay use
//===================================================================
void ncdfOverlayFactory::PrefilterCoefficients(
    float* coeffU, float* coeffV,
    float** uGrid, float** vGrid,
    int ni, int nj, bool isGlobal,
    float &coefU_min, float &coefU_max,
    float &coefV_min, float &coefV_max)
{
    float* rawU = (float*)calloc(nj * ni, sizeof(float));
    float* rawV = (float*)calloc(nj * ni, sizeof(float));
    unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);
    if (!rawU || !rawV || !validMask) {
        if (rawU) free(rawU); if (rawV) free(rawV); if (validMask) free(validMask);
        return;
    }
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float u = uGrid[j][i], v = vGrid[j][i];
            if (!isfinite(u) || !isfinite(v)) {
                rawU[j*ni+i] = 0; rawV[j*ni+i] = 0; validMask[j*ni+i] = 0;
            } else {
                rawU[j*ni+i] = u; rawV[j*ni+i] = v; validMask[j*ni+i] = 1;
            }
        }
    // B-spline prefilter U and V sequentially (avoids concurrent heap pressure)
    BicubicSplinePrefilterWrapAware(coeffU, rawU, validMask, ni, nj, isGlobal);
    BicubicSplinePrefilterWrapAware(coeffV, rawV, validMask, ni, nj, isGlobal);
    coefU_min = 1e30f; coefU_max = -1e30f;
    coefV_min = 1e30f; coefV_max = -1e30f;
    for (int k = 0; k < nj * ni; k++) {
        if (!validMask[k]) continue;
        if (coeffU[k] < coefU_min) coefU_min = coeffU[k];
        if (coeffU[k] > coefU_max) coefU_max = coeffU[k];
        if (coeffV[k] < coefV_min) coefV_min = coeffV[k];
        if (coeffV[k] > coefV_max) coefV_max = coeffV[k];
    }
    if (coefU_max <= coefU_min) { coefU_min = 0; coefU_max = 1; }
    if (coefV_max <= coefV_min) { coefV_min = 0; coefV_max = 1; }
    free(rawU); free(rawV); free(validMask);
}

// Scalar overload: single-channel B-spline prefilter (for SST, Salinity)
void ncdfOverlayFactory::PrefilterScalarCoefficients(
    float* coeff, float** grid,
    int ni, int nj, bool isGlobal,
    float &coefMin, float &coefMax,
    float physMin, float physMax)
{
    float* raw = (float*)calloc(nj * ni, sizeof(float));
    unsigned char* validMask = (unsigned char*)calloc(nj * ni, 1);
    if (!raw || !validMask) { if (raw) free(raw); if (validMask) free(validMask); return; }
    for (int j = 0; j < nj; j++)
        for (int i = 0; i < ni; i++) {
            float v = grid[j][i];
            if (!isfinite(v)) { raw[j*ni+i] = 0; validMask[j*ni+i] = 0; }
            else { raw[j*ni+i] = v; validMask[j*ni+i] = 1; }
        }
    BicubicSplinePrefilterWrapAware(coeff, raw, validMask, ni, nj, isGlobal);
    coefMin = 1e30f; coefMax = -1e30f;
    for (int k = 0; k < nj * ni; k++) {
        if (!validMask[k]) continue;
        if (coeff[k] < coefMin) coefMin = coeff[k];
        if (coeff[k] > coefMax) coefMax = coeff[k];
    }
    if (coefMax <= coefMin) { coefMin = 0; coefMax = 1; }
    free(raw); free(validMask);
}

//===================================================================
// Shared scalar overlay rendering (SeaTemp / Salinity)
