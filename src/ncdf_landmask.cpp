// ncdf_landmask.cpp — ETOPO 2022 陆海掩膜载入、查询与二进制缓存
//
// 存储一律位打包（1 bit/格），全球 30" 也只需 117 MB、全球 1' 只需 29 MB。
// 从 NetCDF 建好后写 .bin 缓存，之后启动直接读缓存，不再解析大文件。

#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <algorithm>

#include <netcdf.h>

#include "ncdf_landmask.h"

extern void ncdfLog(const char* format, ...);

static const char kCacheMagic[8] = { 'N','C','D','F','L','M','0','1' };

NcdfLandMask::NcdfLandMask()
{
    m_fallback = NULL;
    clear();
}

void NcdfLandMask::clear()
{
    m_valid = false;
    m_error.Clear();
    m_path.Clear();
    m_nlat = m_nlon = 0;
    m_lat0 = m_lat1 = m_lon0 = m_lon1 = 0.0;
    m_dlat = m_dlon = 0.0;
    m_latFirst = m_latStep = m_lonFirst = m_lonStep = 0.0;
    m_bits.clear();
}

// ---------------------------------------------------------------------------
bool NcdfLandMask::coversSelf(double lat, double lon) const
{
    return m_valid &&
           lat >= m_lat0 && lat <= m_lat1 &&
           lon >= m_lon0 && lon <= m_lon1;
}

bool NcdfLandMask::covers(double lat, double lon) const
{
    if (coversSelf(lat, lon)) return true;
    return m_fallback ? m_fallback->covers(lat, lon) : false;
}

double NcdfLandMask::indexF(double lat, double lon, int &j, int &i) const
{
    if (!m_valid || m_nlat < 1 || m_nlon < 1) return -1.0;
    if (m_latStep == 0.0 || m_lonStep == 0.0) return -1.0;

    double fj = (lat - m_latFirst) / m_latStep;
    double fi = (lon - m_lonFirst) / m_lonStep;
    if (fj < -0.5 || fj > m_nlat - 0.5 || fi < -0.5 || fi > m_nlon - 0.5) return -1.0;
    if (fj < 0) fj = 0;
    if (fi < 0) fi = 0;
    if (fj > m_nlat - 1) fj = m_nlat - 1;
    if (fi > m_nlon - 1) fi = m_nlon - 1;
    j = (int)(fj + 0.5);
    i = (int)(fi + 0.5);
    return fj;
}

// ---- 位段统计：一次读取一个 uint64 并 popcount，代替逐 bit 访问 ----
static inline unsigned popcnt64(uint64_t x)
{
    // Kernighan 位清零法；我们的位段很窄（4~5 bit），循环次数极少
    unsigned n = 0;
    while (x) { x &= x - 1; n++; }
    return n;
}

size_t NcdfLandMask::countLandRange(int j, int i0, int i1) const
{
    if (!m_valid || i1 < i0 || j < 0 || j >= (int)m_nlat) return 0;
    if (i0 < 0) i0 = 0;
    if (i1 > m_nlon - 1) i1 = m_nlon - 1;
    if (i1 < i0) return 0;

    const size_t base = (size_t)j * (size_t)m_nlon;
    const size_t lo = base + (size_t)i0;
    const size_t hi = base + (size_t)i1;
    const size_t w0 = lo >> 6, w1 = hi >> 6;

    if (w0 == w1) {
        const int sh = (int)(lo & 63);
        const int span = (int)(hi - lo + 1);
        uint64_t m = (span >= 64) ? ~0ULL : (((1ULL << span) - 1ULL) << sh);
        return (size_t)popcnt64(m_bits[w0] & m);
    }
    size_t n = 0;
    // 首个不完整字
    n += (size_t)popcnt64(m_bits[w0] & (~0ULL << (lo & 63)));
    // 中间整字
    for (size_t w = w0 + 1; w < w1; w++) n += (size_t)popcnt64(m_bits[w]);
    // 末个不完整字
    n += (size_t)popcnt64(m_bits[w1] & ((1ULL << ((hi & 63) + 1)) - 1ULL));
    return n;
}

bool NcdfLandMask::isLand(double lat, double lon) const
{
    int j, i;
    if (indexF(lat, lon, j, i) >= 0.0) return bitAt((size_t)j * m_nlon + i);
    return m_fallback ? m_fallback->isLand(lat, lon) : false;
}

// 双线性陆地覆盖度 0..1
double NcdfLandMask::landFrac(double lat, double lon) const
{
    if (!m_valid || m_latStep == 0.0 || m_lonStep == 0.0)
        return m_fallback ? m_fallback->landFrac(lat, lon) : 0.0;

    double fj = (lat - m_latFirst) / m_latStep;
    double fi = (lon - m_lonFirst) / m_lonStep;
    if (fj < 0 || fj > m_nlat - 1 || fi < 0 || fi > m_nlon - 1)
        return m_fallback ? m_fallback->landFrac(lat, lon) : 0.0;

    int j0 = (int)fj, i0 = (int)fi;
    if (j0 > m_nlat - 2) j0 = m_nlat - 2;
    if (i0 > m_nlon - 2) i0 = m_nlon - 2;
    if (j0 < 0) j0 = 0;
    if (i0 < 0) i0 = 0;
    double tj = fj - j0, ti = fi - i0;

    double a00 = bitAt((size_t)j0 * m_nlon + i0) ? 1.0 : 0.0;
    double a10 = bitAt((size_t)j0 * m_nlon + i0 + 1) ? 1.0 : 0.0;
    double a01 = bitAt((size_t)(j0 + 1) * m_nlon + i0) ? 1.0 : 0.0;
    double a11 = bitAt((size_t)(j0 + 1) * m_nlon + i0 + 1) ? 1.0 : 0.0;
    return a00 * (1 - ti) * (1 - tj) + a10 * ti * (1 - tj)
         + a01 * (1 - ti) * tj       + a11 * ti * tj;
}

double NcdfLandMask::landFracInBox(double lat0, double lat1,
                                   double lon0, double lon1) const
{
    if (lat0 > lat1) std::swap(lat0, lat1);
    if (lon0 > lon1) std::swap(lon0, lon1);
    double clat = 0.5 * (lat0 + lat1), clon = 0.5 * (lon0 + lon1);
    if (!coversSelf(clat, clon))
        return m_fallback ? m_fallback->landFracInBox(lat0, lat1, lon0, lon1) : 0.0;
    if (!m_valid) return 0.0;

    double a0 = std::max(lat0, m_lat0), a1 = std::min(lat1, m_lat1);
    double o0 = std::max(lon0, m_lon0), o1 = std::min(lon1, m_lon1);
    if (a1 <= a0 || o1 <= o0) return 0.0;

    int j0, i0, j1, i1;
    if (indexF(a0, o0, j0, i0) < 0.0 || indexF(a1, o1, j1, i1) < 0.0) return 0.0;
    if (j0 > j1) std::swap(j0, j1);
    if (i0 > i1) std::swap(i0, i1);

    size_t land = 0, total = 0;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            total++;
            if (bitAt((size_t)j * m_nlon + i)) land++;
        }
    return (total > 0) ? (double)land / (double)total : 0.0;
}

// ---------------------------------------------------------------------------
// 逐行批量判定：结果与逐格 landFracInBox(latC±h, lo±h) > 0.5 完全一致。
// 优化点：掩膜行号 j0/j1 整行只算一次；列向浮点下标随 i 增量推进
// （step/m_lonStep 是双精度，**不要求整数比**），省掉每格的除法和函数调用。
// 陆地格距由 nc 文件范围自适应决定（30″/1′/2′…），这里不做整数比假设。
// 任何裁剪/越界/取整都按 landFracInBox 原语义复刻；覆盖不到的格退回通用路径。
// ---------------------------------------------------------------------------
void NcdfLandMask::landMaskRow(double latC, double lon0, double step,
                               int tw, unsigned char *out) const
{
    const double half = 0.5 * step;

    auto fallbackCell = [&](int i) {
        double lo = lon0 + step * i;
        out[i] = (landFracInBox(latC - half, latC + half, lo - half, lo + half) > 0.5) ? 255 : 0;
    };

    if (!m_valid || m_nlat < 1 || m_nlon < 1 || m_latStep == 0.0 || m_lonStep == 0.0) {
        for (int i = 0; i < tw; i++) fallbackCell(i);
        return;
    }

    // ---- 行方向：整行固定，只算一次 ----
    double a0 = latC - half, a1 = latC + half;
    if (a0 > a1) { double t = a0; a0 = a1; a1 = t; }
    a0 = std::max(a0, m_lat0); a1 = std::min(a1, m_lat1);
    if (a1 <= a0) { memset(out, 0, (size_t)tw); return; }

    double fja = (a0 - m_latFirst) / m_latStep;
    double fjb = (a1 - m_latFirst) / m_latStep;
    if (fja < -0.5 || fja > m_nlat - 0.5 || fjb < -0.5 || fjb > m_nlat - 0.5) {
        for (int i = 0; i < tw; i++) fallbackCell(i);
        return;
    }
    if (fja < 0) fja = 0; if (fja > m_nlat - 1) fja = m_nlat - 1;
    if (fjb < 0) fjb = 0; if (fjb > m_nlat - 1) fjb = m_nlat - 1;
    int j0 = (int)(fja + 0.5), j1 = (int)(fjb + 0.5);
    if (j0 > j1) { int t = j0; j0 = j1; j1 = t; }

    // ---- 列方向：浮点下标增量推进 ----
    const double ratio = step / m_lonStep;
    double fo0 = ((lon0 - half) - m_lonFirst) / m_lonStep;
    double fo1 = ((lon0 + half) - m_lonFirst) / m_lonStep;

    for (int i = 0; i < tw; i++, fo0 += ratio, fo1 += ratio) {
        const double lo = lon0 + step * i;
        double o0 = lo - half, o1 = lo + half;
        if (o0 > o1) { double t = o0; o0 = o1; o1 = t; }

        // landFracInBox 会先看盒中心是否在本层覆盖内，不在就交给 fallback
        if (!coversSelf(latC, 0.5 * (o0 + o1))) { fallbackCell(i); continue; }

        const bool unclipped = (o0 >= m_lon0 && o1 <= m_lon1);
        o0 = std::max(o0, m_lon0); o1 = std::min(o1, m_lon1);
        if (o1 <= o0) { out[i] = 0; continue; }

        // 裁剪会改变下标，故只有未被裁到的格才用增量值
        double fi0 = unclipped ? fo0 : (o0 - m_lonFirst) / m_lonStep;
        double fi1 = unclipped ? fo1 : (o1 - m_lonFirst) / m_lonStep;

        if (fi0 < -0.5 || fi0 > m_nlon - 0.5 || fi1 < -0.5 || fi1 > m_nlon - 0.5) {
            out[i] = 0; continue;
        }
        if (fi0 < 0) fi0 = 0; if (fi0 > m_nlon - 1) fi0 = m_nlon - 1;
        if (fi1 < 0) fi1 = 0; if (fi1 > m_nlon - 1) fi1 = m_nlon - 1;
        int i0 = (int)(fi0 + 0.5), i1 = (int)(fi1 + 0.5);
        if (i0 > i1) { int t = i0; i0 = i1; i1 = t; }

        // 逐 bit 统计换成按位段 popcount：每行 1 次内存读取代替 i1-i0+1 次
        size_t land = 0;
        const size_t total = (size_t)(j1 - j0 + 1) * (size_t)(i1 - i0 + 1);
        for (int j = j0; j <= j1; j++) land += countLandRange(j, i0, i1);
        out[i] = (total > 0 && (double)land / (double)total > 0.5) ? 255 : 0;
    }
}

// ---------------------------------------------------------------------------
// 二进制缓存：首轮从 NetCDF 建好后写盘，之后启动直接读
// ---------------------------------------------------------------------------
bool NcdfLandMask::saveCache(const wxString &path) const
{
    if (!m_valid) return false;
    FILE *fp = fopen((const char*)path.mb_str(), "wb");
    if (!fp) return false;
    int32_t hdr[4] = { m_nlat, m_nlon, 0, 0 };
    double geo[4] = { m_latFirst, m_latStep, m_lonFirst, m_lonStep };
    uint64_t nwords = (uint64_t)m_bits.size();
    bool ok = fwrite(kCacheMagic, 1, 8, fp) == 8
           && fwrite(hdr, sizeof(hdr), 1, fp) == 1
           && fwrite(geo, sizeof(geo), 1, fp) == 1
           && fwrite(&nwords, sizeof(nwords), 1, fp) == 1
           && (nwords == 0 || fwrite(&m_bits[0], sizeof(uint64_t), (size_t)nwords, fp) == nwords);
    fclose(fp);
    return ok;
}

bool NcdfLandMask::loadCache(const wxString &path)
{
    clear();
    FILE *fp = fopen((const char*)path.mb_str(), "rb");
    if (!fp) return false;
    char magic[8];
    int32_t hdr[4] = {0, 0, 0, 0};
    double geo[4] = {0, 0, 0, 0};
    uint64_t nwords = 0;
    bool ok = fread(magic, 1, 8, fp) == 8
           && memcmp(magic, kCacheMagic, 8) == 0
           && fread(hdr, sizeof(hdr), 1, fp) == 1
           && fread(geo, sizeof(geo), 1, fp) == 1
           && fread(&nwords, sizeof(nwords), 1, fp) == 1;
    if (ok && hdr[0] > 0 && hdr[1] > 0 && nwords < (uint64_t)1 << 32) {
        m_bits.assign((size_t)nwords, 0);
        ok = (nwords == 0) || fread(&m_bits[0], sizeof(uint64_t), (size_t)nwords, fp) == nwords;
    } else {
        ok = false;
    }
    fclose(fp);
    if (!ok) { clear(); return false; }

    m_nlat = hdr[0]; m_nlon = hdr[1];
    m_latFirst = geo[0]; m_latStep = geo[1]; m_lonFirst = geo[2]; m_lonStep = geo[3];
    double latEnd = m_latFirst + m_latStep * (m_nlat - 1);
    double lonEnd = m_lonFirst + m_lonStep * (m_nlon - 1);
    m_lat0 = std::min(m_latFirst, latEnd); m_lat1 = std::max(m_latFirst, latEnd);
    m_lon0 = std::min(m_lonFirst, lonEnd); m_lon1 = std::max(m_lonFirst, lonEnd);
    m_dlat = std::fabs(m_latStep);
    m_dlon = std::fabs(m_lonStep);
    m_path = path;
    m_valid = true;
    ncdfLog("[landmask] cache hit %s: %dx%d  lat %.3f..%.3f lon %.3f..%.3f\n",
            (const char*)path.mb_str(), m_nlat, m_nlon, m_lat0, m_lat1, m_lon0, m_lon1);
    return true;
}

// ---------------------------------------------------------------------------
static int findElevVar(int ncid, int latDim, int lonDim)
{
    static const char *names[] = {
        "z", "Band1", "elevation", "elev", "topo", "topography", "height",
        "dem", "DEM", "orog", "altitude", NULL
    };
    for (int k = 0; names[k]; k++) {
        int varid = -1;
        if (nc_inq_varid(ncid, names[k], &varid) == NC_NOERR) return varid;
    }
    int nvars = 0;
    nc_inq_nvars(ncid, &nvars);
    int found = -1, foundCount = 0;
    for (int v = 0; v < nvars; v++) {
        int nd = 0, dimids[NC_MAX_VAR_DIMS];
        nc_inq_varndims(ncid, v, &nd);
        if (nd != 2) continue;
        nc_inq_vardimid(ncid, v, dimids);
        bool hasLat = false, hasLon = false;
        for (int d = 0; d < 2; d++) {
            if (dimids[d] == latDim) hasLat = true;
            if (dimids[d] == lonDim) hasLon = true;
        }
        if (hasLat && hasLon) { found = v; foundCount++; }
    }
    return (foundCount == 1) ? found : -1;
}

static int findCoordDim(int ncid, const char **preferred, int &varid)
{
    varid = -1;
    for (int k = 0; preferred[k]; k++) {
        int d = -1;
        if (nc_inq_dimid(ncid, preferred[k], &d) == NC_NOERR) {
            nc_inq_varid(ncid, preferred[k], &varid);
            return d;
        }
    }
    return -1;
}

bool NcdfLandMask::load(const wxString &path,
                        double latMin, double latMax,
                        double lonMin, double lonMax)
{
    clear();
    m_path = path;
    if (latMin > latMax) std::swap(latMin, latMax);
    if (lonMin > lonMax) std::swap(lonMin, lonMax);

    const wxCharBuffer mb = path.mb_str();
    int ncid = -1;
    if (nc_open((const char*)mb, NC_NOWRITE, &ncid) != NC_NOERR) {
        m_error = wxString::FromUTF8("无法打开掩膜文件");
        ncdfLog("[landmask] open failed: %s\n", (const char*)mb);
        return false;
    }

    static const char *latNames[] = {"latitude", "lat", "y", "Y", NULL};
    static const char *lonNames[] = {"longitude", "lon", "x", "X", NULL};
    int latVar = -1, lonVar = -1;
    int latDim = findCoordDim(ncid, latNames, latVar);
    int lonDim = findCoordDim(ncid, lonNames, lonVar);
    if (latDim < 0 || lonDim < 0) {
        m_error = wxString::FromUTF8("掩膜文件缺少 latitude/longitude 维");
        nc_close(ncid);
        return false;
    }

    size_t nlat = 0, nlon = 0;
    nc_inq_dimlen(ncid, latDim, &nlat);
    nc_inq_dimlen(ncid, lonDim, &nlon);
    if (nlat < 2 || nlon < 2) {
        m_error = wxString::FromUTF8("掩膜文件经纬维过短");
        nc_close(ncid);
        return false;
    }

    std::vector<double> lat((size_t)nlat), lon((size_t)nlon);
    if (latVar >= 0) nc_get_var_double(ncid, latVar, &lat[0]);
    if (lonVar >= 0) nc_get_var_double(ncid, lonVar, &lon[0]);
    for (size_t j = 0; j < nlat; j++) if (!std::isfinite(lat[j])) lat[j] = 0.0;
    for (size_t i = 0; i < nlon; i++) if (!std::isfinite(lon[i])) lon[i] = 0.0;

    int elevVar = findElevVar(ncid, latDim, lonDim);
    if (elevVar < 0) {
        m_error = wxString::FromUTF8("未在掩膜文件中找到高程变量");
        nc_close(ncid);
        return false;
    }

    double fLat0 = std::min(lat[0], lat[nlat - 1]), fLat1 = std::max(lat[0], lat[nlat - 1]);
    double fLon0 = std::min(lon[0], lon[nlon - 1]), fLon1 = std::max(lon[0], lon[nlon - 1]);
    double qLat0 = std::max(latMin, fLat0), qLat1 = std::min(latMax, fLat1);
    double qLon0 = std::max(lonMin, fLon0), qLon1 = std::min(lonMax, fLon1);
    if (qLat1 <= qLat0 || qLon1 <= qLon0) {
        m_error = wxString::FromUTF8("掩膜文件与所需范围无重叠");
        nc_close(ncid);
        return false;
    }

    auto nearestIdx = [](const std::vector<double> &a, size_t n, double x) -> size_t {
        size_t best = 0;
        double bd = std::fabs(a[0] - x);
        for (size_t k = 1; k < n; k++) {
            double d = std::fabs(a[k] - x);
            if (d < bd) { bd = d; best = k; }
        }
        return best;
    };
    size_t ja = nearestIdx(lat, nlat, qLat0), jb = nearestIdx(lat, nlat, qLat1);
    size_t ia = nearestIdx(lon, nlon, qLon0), ib = nearestIdx(lon, nlon, qLon1);
    size_t j0 = std::min(ja, jb), j1 = std::max(ja, jb);
    size_t i0 = std::min(ia, ib), i1 = std::max(ia, ib);

    // 上限：位打包 256M 格 = 32 MB。全球 1'（233M 格）原样装下；
    // 全球 30"（933M 格）自动抽稀 2 倍到 1' 等效。再大才有损失。
    const size_t kMaxCells = (size_t)256 * 1024 * 1024;
    size_t cntJ = j1 - j0 + 1, cntI = i1 - i0 + 1;
    size_t stride = 1;
    while (cntJ / stride * (cntI / stride) > kMaxCells) stride++;

    size_t outJ = (cntJ + stride - 1) / stride;
    size_t outI = (cntI + stride - 1) / stride;
    const size_t kCells = outJ * outI;
    m_bits.assign((kCells + 63) / 64, 0);

    // 维度顺序必须看**变量自己的 dimid 顺序**：文件先声明 lon 后声明 lat 时
    // 维度 ID 的大小会反过来，按 ID 判断就会把掩膜转置。
    int vdims[NC_MAX_VAR_DIMS];
    nc_inq_vardimid(ncid, elevVar, vdims);
    int dimOrder = (vdims[0] == latDim && vdims[1] == lonDim) ? 0 : 1;
    ncdfLog("[landmask] elev var %d dims order=%s (latDim=%d lonDim=%d)\n",
            elevVar, dimOrder == 0 ? "(lat,lon)" : "(lon,lat)", latDim, lonDim);

    // 分块读取：峰值内存只与块高有关
    const size_t kBlockRows = 256;
    std::vector<float> block;
    for (size_t jb = 0; jb < outJ; jb += kBlockRows) {
        size_t rows = std::min(kBlockRows, outJ - jb);
        block.assign(rows * outI, 0.0f);

        size_t start[2], count[2];
        ptrdiff_t strideArr[2];
        if (dimOrder == 0) {
            start[0] = j0 + jb * stride; count[0] = rows; strideArr[0] = (ptrdiff_t)stride;
            start[1] = i0;               count[1] = outI; strideArr[1] = (ptrdiff_t)stride;
        } else {
            start[0] = i0;               count[0] = outI; strideArr[0] = (ptrdiff_t)stride;
            start[1] = j0 + jb * stride; count[1] = rows; strideArr[1] = (ptrdiff_t)stride;
        }

        int st = nc_get_vars_float(ncid, elevVar, start, count, strideArr, &block[0]);
        if (st != NC_NOERR) {
            m_error = wxString::FromUTF8("读取高程数据失败");
            nc_close(ncid);
            clear();
            return false;
        }

        for (size_t r = 0; r < rows; r++) {
            size_t base = (jb + r) * outI;
            if (dimOrder == 0) {
                const float *src = &block[r * outI];
                for (size_t c = 0; c < outI; c++) {
                    float e = src[c];
                    if (std::isfinite(e) && e > 0.0f) setBit(base + c);
                }
            } else {
                for (size_t c = 0; c < outI; c++) {
                    float e = block[c * rows + r];
                    if (std::isfinite(e) && e > 0.0f) setBit(base + c);
                }
            }
        }
    }
    nc_close(ncid);

    m_nlat = (int)outJ;
    m_nlon = (int)outI;
    {
        size_t jLast = j0 + (outJ - 1) * stride; if (jLast >= nlat) jLast = nlat - 1;
        size_t iLast = i0 + (outI - 1) * stride; if (iLast >= nlon) iLast = nlon - 1;
        m_latFirst = lat[j0];   m_lonFirst = lon[i0];
        m_latStep = (outJ > 1) ? (lat[jLast] - lat[j0]) / (double)(outJ - 1) : 1.0;
        m_lonStep = (outI > 1) ? (lon[iLast] - lon[i0]) / (double)(outI - 1) : 1.0;
    }
    if (m_latStep == 0.0) m_latStep = 1.0;
    if (m_lonStep == 0.0) m_lonStep = 1.0;

    double latEnd = m_latFirst + m_latStep * (m_nlat - 1);
    double lonEnd = m_lonFirst + m_lonStep * (m_nlon - 1);
    m_lat0 = std::min(m_latFirst, latEnd); m_lat1 = std::max(m_latFirst, latEnd);
    m_lon0 = std::min(m_lonFirst, lonEnd); m_lon1 = std::max(m_lonFirst, lonEnd);
    m_dlat = std::fabs(m_latStep);
    m_dlon = std::fabs(m_lonStep);

    size_t landCnt = 0;
    for (size_t k = 0; k < kCells; k++) if (bitAt(k)) landCnt++;

    m_valid = true;
    ncdfLog("[landmask] loaded %s: %dx%d stride=%d  lat %.3f..%.3f lon %.3f..%.3f  d=%.5f/%.5f deg  land=%.1f%%\n",
            (const char*)path.mb_str(), m_nlat, m_nlon, (int)stride,
            m_lat0, m_lat1, m_lon0, m_lon1, m_dlat, m_dlon,
            100.0 * landCnt / std::max<size_t>(1, kCells));
    // 四角陆地比例：维度顺序判错（转置）时这四个数会明显对调，可当自检
    {
        double lo0 = m_lat0 + 0.05 * (m_lat1 - m_lat0), lo1 = m_lat0 + 0.15 * (m_lat1 - m_lat0);
        double hi0 = m_lat0 + 0.85 * (m_lat1 - m_lat0), hi1 = m_lat0 + 0.95 * (m_lat1 - m_lat0);
        double we0 = m_lon0 + 0.05 * (m_lon1 - m_lon0), we1 = m_lon0 + 0.15 * (m_lon1 - m_lon0);
        double ea0 = m_lon0 + 0.85 * (m_lon1 - m_lon0), ea1 = m_lon0 + 0.95 * (m_lon1 - m_lon0);
        ncdfLog("[landmask] corner land frac: SW=%.2f SE=%.2f NW=%.2f NE=%.2f\n",
                landFracInBox(lo0, lo1, we0, we1), landFracInBox(lo0, lo1, ea0, ea1),
                landFracInBox(hi0, hi1, we0, we1), landFracInBox(hi0, hi1, ea0, ea1));
    }
    return true;
}
