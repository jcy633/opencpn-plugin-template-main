/*********************************************************************
 * ncdf_landmask.h — 高分辨率陆海掩膜（ETOPO 2022）
 *
 * 论文 Methods："Land masking was applied using the Earth Topography 2022
 * (ETOPO 2022) global DEM dataset at a 30-arc-second resolution."
 *
 * 两个用途，彼此独立：
 *   1) 算法侧：smooth_path_bd / search_boundary_km / correct_axis_transect
 *      在**非格点位置**查询陆海。9 km 的数据掩膜只能回答"这一格是不是海"。
 *      注意：边界搜索步长是 7 km，所以掩膜精度到 ~2 km 就已饱和，再细
 *      （30" = 0.9 km）不会让边界位置更准。
 *   2) 显示侧：给 1/12° 纹理的 B 通道写入**格内陆地覆盖比例**，着色器据此
 *      做双线性覆盖度，海岸线位置准到亚格点而非被 9 km 量化。
 *
 * 两级结构（setFallback）：
 *   精层：黑潮流区 30"（约 4.2 MB）——算法与流区显示用
 *   粗层：全球 1' 位打包（约 29 MB）——流区之外的显示用
 * 查询时精层优先，覆盖不到自动落到粗层。
 *
 * 存储一律位打包（1 bit/格），全球 30" 也只需 117 MB；首次从 NetCDF 建好后
 * 写 .bin 缓存，之后启动直接读缓存，不再解析大文件。
 *********************************************************************/
#ifndef NCDF_LANDMASK_H
#define NCDF_LANDMASK_H

#include <vector>
#include <cstdint>
#include <wx/string.h>

class NcdfLandMask {
public:
    NcdfLandMask();

    // 从高程 NetCDF 读入 [latMin,latMax]x[lonMin,lonMax] 子集。
    // 超出文件覆盖的部分自动裁剪；失败返回 false 并置 error()。
    bool load(const wxString &path,
              double latMin, double latMax, double lonMin, double lonMax);

    // 二进制缓存（首次从 NetCDF 建好后写，之后启动秒开）
    bool saveCache(const wxString &path) const;
    bool loadCache(const wxString &path);

    void clear();
    bool valid() const { return m_valid; }
    const wxString &error() const { return m_error; }
    const wxString &sourcePath() const { return m_path; }

    // 粗层回退：本层覆盖不到的点由 fallback 回答
    void setFallback(const NcdfLandMask *coarse) { m_fallback = coarse; }

    // 单点判定：true = 陆地（高程 > 0）
    bool isLand(double lat, double lon) const;
    // 单点陆地覆盖度 0..1（对网格做双线性）
    double landFrac(double lat, double lon) const;
    // 区域内地陆地比例 0..1
    double landFracInBox(double lat0, double lat1, double lon0, double lon1) const;

    // 逐行批量判定（供规则网格建陆地图层用）：
    //   out[i] = 255 当 landFracInBox(latC±0.5*step, lon0+i*step±0.5*step) > 0.5
    // 与逐格调 landFracInBox 结果一致，但下标换算用增量步进，省掉每格的除法。
    // step 可与掩膜格距成**任意**比值（陆地格距由 nc 文件范围自适应决定，
    // 可能是 30″/1′/2′…），这里不做任何整数比假设。
    void landMaskRow(double latC, double lon0, double step,
                     int tw, unsigned char *out) const;
    // 是否能回答该点（含回退层）
    bool covers(double lat, double lon) const;

    int    nlat() const { return m_nlat; }
    int    nlon() const { return m_nlon; }
    double latMin() const { return m_lat0; }
    double latMax() const { return m_lat1; }
    double lonMin() const { return m_lon0; }
    double lonMax() const { return m_lon1; }
    double dLat() const { return m_dlat; }
    double dLon() const { return m_dlon; }
    double cellCount() const { return (double)m_nlat * (double)m_nlon; }

private:
    bool m_valid;
    wxString m_error;
    wxString m_path;
    int m_nlat, m_nlon;
    double m_lat0, m_lat1, m_lon0, m_lon1;
    double m_dlat, m_dlon;
    // 带符号步长：坐标升序/降序都正确（m_bits 按文件维度顺序存）
    double m_latFirst, m_latStep, m_lonFirst, m_lonStep;
    std::vector<uint64_t> m_bits;      // 1 bit/格，1 = 陆
    const NcdfLandMask *m_fallback;

    inline bool bitAt(size_t k) const {
        return (m_bits[k >> 6] >> (k & 63)) & 1ULL;
    }
    // 统计第 j 行 [i0,i1] 列的陆地格数（按位段一次统计，避免逐 bit 访问）
    size_t countLandRange(int j, int i0, int i1) const;
    inline void setBit(size_t k) { m_bits[k >> 6] |= 1ULL << (k & 63); }

    // 坐标 -> 浮点下标（越界返回 <0）
    double indexF(double lat, double lon, int &j, int &i) const;
    bool   coversSelf(double lat, double lon) const;
};

#endif // NCDF_LANDMASK_H
