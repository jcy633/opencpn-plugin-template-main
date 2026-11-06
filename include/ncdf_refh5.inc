// ===========================================================================
// ncdf_refh5.inc — 参考轴文件（kuroshio_YYYY.nc）的 HDF5 **直读**实现
//
// 【为什么不用 nc_open】
//   kuroshio_2021.nc 是 365 组 x 30 层 x 6 变量 = 65700 个数据集、10950 个组。
//   netCDF-4 在 nc_open 时**递归读全部分组的元数据**，实测工作集 2693 MB
//   （峰值 3091 MB）；Win32 进程地址空间只有 2 GB，插件里调 nc_open 当场崩溃
//   （2026-11-04 实测：OpenCPN 在 [seamtauto] auto test start 之后出 crash dump）。
//   同一个文件用 HDF5 C API 打开只用 25 MB —— HDF5 是按需取对象头的。
//   所以参考轴文件一律走 hdf5.lib 直读，绝不走 netCDF 路径。
//   （这正是 CLAUDE.md 里「Win32 上 nc_open 打开每天每层一组的 HDF5 文件会吃掉
//     GB 级内存」那条坑的另一种表现形式。）
//
// 【ABI】本工程链的 hdf5.dll 是 HDF5 **1.12.1**，hid_t = int64_t（1.10 起改的）。
//   工程里没有 hdf5.h，所以这里手写声明；只用 C API 里最基础那几个，
//   H5S_ALL / H5P_DEFAULT 在 HDF5 里都定义成 (hid_t)0。
//   tools/h5reftest.cpp 与本文件同一份代码编译，用来先在进程外验证 ABI。
//
// 【数据】/dayNNN/depth01/ 下是 main_axis_lon, main_axis_lat, left_bdy_lon,
//   left_bdy_lat, right_bdy_lon, right_bdy_lat，都是 1 维 float64（'point'）。
//   **没有 _FillValue**，缺口就是成对的 NaN —— 拼折线时 NaN 要**断开**，
//   跳过会把缺口两端直接连起来，画出跨陆地的假边界。
// ===========================================================================
#ifndef NCDF_REFH5_INC
#define NCDF_REFH5_INC

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <limits>

// ---- HDF5 1.12 C API 最小声明（hid_t = int64_t，herr_t = int）----
typedef long long          rh_hid_t;
typedef int                rh_herr_t;
typedef unsigned long long rh_hsize_t;

extern "C" {
    rh_hid_t  H5Fopen(const char *name, unsigned flags, rh_hid_t fapl);
    rh_herr_t H5Fclose(rh_hid_t f);
    rh_hid_t  H5Gopen2(rh_hid_t loc, const char *name, rh_hid_t gapl);
    rh_herr_t H5Gclose(rh_hid_t g);
    rh_herr_t H5Gget_num_objs(rh_hid_t loc, rh_hsize_t *num);
    long      H5Gget_objname_by_idx(rh_hid_t loc, rh_hsize_t idx, char *name, unsigned long size);
    rh_hid_t  H5Dopen2(rh_hid_t loc, const char *name, rh_hid_t dapl);
    rh_herr_t H5Dclose(rh_hid_t d);
    rh_hid_t  H5Dget_space(rh_hid_t d);
    int       H5Sget_simple_extent_ndims(rh_hid_t s);
    int       H5Sget_simple_extent_dims(rh_hid_t s, rh_hsize_t *dims, rh_hsize_t *maxdims);
    rh_herr_t H5Sclose(rh_hid_t s);
    rh_hid_t  H5Dget_type(rh_hid_t d);
    int       H5Tget_class(rh_hid_t t);
    unsigned long H5Tget_size(rh_hid_t t);
    int       H5Tget_order(rh_hid_t t);
    rh_herr_t H5Tclose(rh_hid_t t);
    rh_herr_t H5Dread(rh_hid_t d, rh_hid_t mem_type, rh_hid_t mem_space,
                      rh_hid_t file_space, rh_hid_t dxpl, void *buf);
}

#define RH_H5P_DEFAULT     ((rh_hid_t)0)
#define RH_H5S_ALL         ((rh_hid_t)0)
#define RH_H5F_ACC_RDONLY  (0x0000u)

enum { RH_H5T_INTEGER = 0, RH_H5T_FLOAT = 1 };
enum { RH_H5T_ORDER_LE = 0, RH_H5T_ORDER_BE = 1 };

// 参考轴文件里的一天：折线按 lon,lat 成对存储，NaN 断点表示缺口
struct RefH5PolyDay {
    int dayOfYear;
    std::vector<double> axis, left, right;
    int nAxis;                 // 主轴**有效**点数（不含 NaN 断点）
    RefH5PolyDay() : dayOfYear(0), nAxis(0) {}
};

// ---------------------------------------------------------------------------
// lon/lat 成对拼折线：任一为 NaN 即**断开**（不是跳过）。
// ---------------------------------------------------------------------------
static void refH5BuildPoly(const std::vector<double> &lo, const std::vector<double> &la,
                           std::vector<double> &out, int *nValid)
{
    out.clear();
    if (nValid) *nValid = 0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    size_t n = (lo.size() < la.size()) ? lo.size() : la.size();
    for (size_t i = 0; i < n; i++) {
        if (!std::isfinite(lo[i]) || !std::isfinite(la[i])) {
            if (!out.empty() && std::isfinite(out[out.size() - 2])) {
                out.push_back(nan); out.push_back(nan);
            }
            continue;
        }
        out.push_back(lo[i]); out.push_back(la[i]);
        if (nValid) (*nValid)++;
    }
}

// ---------------------------------------------------------------------------
// 读一个数据集 -> double。支持 IEEE float32/float64 与整型（含大小端）。
// 失败返回 false（变量不存在 / 类型不认识 / 读失败）。
// ---------------------------------------------------------------------------
static bool refH5ReadVar(rh_hid_t grp, const char *name, std::vector<double> &v)
{
    v.clear();
    rh_hid_t d = H5Dopen2(grp, name, RH_H5P_DEFAULT);
    if (d < 0) return false;

    bool ok = false;
    rh_hid_t sp = H5Dget_space(d);
    rh_hid_t tp = H5Dget_type(d);
    if (sp >= 0 && tp >= 0) {
        rh_hsize_t len = 0;
        int nd = H5Sget_simple_extent_ndims(sp);
        if (nd == 0) {
            len = 1;                                   // 标量
        } else if (nd > 0 && nd <= 8) {
            rh_hsize_t dims[8] = {0,0,0,0,0,0,0,0};
            if (H5Sget_simple_extent_dims(sp, dims, NULL) == nd) {
                len = 1;
                for (int i = 0; i < nd; i++) len *= dims[i];
            }
        }

        int cls = H5Tget_class(tp);
        unsigned long sz = H5Tget_size(tp);
        int order = H5Tget_order(tp);
        // 上限 2^26 个元素：参考轴一天顶多几百点，超出就是维度算错了
        if (len > 0 && len < ((rh_hsize_t)1 << 26) &&
            (cls == RH_H5T_FLOAT || cls == RH_H5T_INTEGER) && (sz == 4 || sz == 8)) {
            std::vector<unsigned char> raw((size_t)len * sz, 0);
            // 用**文件自身类型**当内存类型，读原始字节后自己转 —— 免掉对
            // H5T_NATIVE_*_g 这些 DLL 导出全局变量的依赖（它们的下划线后缀
            // 随版本变，手写声明最容易在这里翻车）。
            if (H5Dread(d, tp, RH_H5S_ALL, RH_H5S_ALL, RH_H5P_DEFAULT, &raw[0]) >= 0) {
                v.resize((size_t)len);
                bool be = (order == RH_H5T_ORDER_BE);
                for (size_t i = 0; i < (size_t)len; i++) {
                    unsigned char buf[8];
                    memcpy(buf, &raw[i * sz], sz);
                    if (be) std::reverse(buf, buf + sz);
                    if (cls == RH_H5T_FLOAT && sz == 8) {
                        double x; memcpy(&x, buf, 8); v[i] = x;
                    } else if (cls == RH_H5T_FLOAT && sz == 4) {
                        float x;  memcpy(&x, buf, 4); v[i] = (double)x;
                    } else if (sz == 8) {
                        long long x; memcpy(&x, buf, 8); v[i] = (double)x;
                    } else {
                        int x;    memcpy(&x, buf, 4); v[i] = (double)x;
                    }
                }
                ok = true;
            }
        }
    }
    if (tp >= 0) H5Tclose(tp);
    if (sp >= 0) H5Sclose(sp);
    H5Dclose(d);
    return ok;
}

// ---------------------------------------------------------------------------
// 读整份参考轴文件。
// pathUtf8 必须是 **UTF-8** —— 这台机器上的 hdf5.dll 内部按 UTF-8 解释 char*
// 路径（tools/h5reftest.cpp --probe 实测：CP_UTF8 能开，CP_ACP 报 errno=2）。
// 这与 nc_open 那边的 mb_str()（本地代码页）约定**相反**，别照抄。
// ---------------------------------------------------------------------------
static bool refH5Load(const char *pathUtf8, std::vector<RefH5PolyDay> &out, std::string &err)
{
    out.clear();
    err.clear();

    rh_hid_t f = H5Fopen(pathUtf8, RH_H5F_ACC_RDONLY, RH_H5P_DEFAULT);
    if (f < 0) {
        err = "H5Fopen failed (file is not HDF5/NetCDF-4, or path is wrong)";
        return false;
    }

    rh_hsize_t nobj = 0;
    if (H5Gget_num_objs(f, &nobj) < 0) {
        err = "H5Gget_num_objs failed";
        H5Fclose(f);
        return false;
    }

    for (rh_hsize_t gi = 0; gi < nobj; gi++) {
        char gname[256];
        memset(gname, 0, sizeof(gname));
        long nl = H5Gget_objname_by_idx(f, gi, gname, (unsigned long)(sizeof(gname) - 1));
        if (nl <= 0) continue;
        gname[sizeof(gname) - 1] = 0;

        if (strncmp(gname, "day", 3) != 0) continue;
        char *endp = NULL;
        long dayNo = strtol(gname + 3, &endp, 10);
        if (dayNo <= 0 || dayNo > 366) continue;

        rh_hid_t g = H5Gopen2(f, gname, RH_H5P_DEFAULT);
        if (g < 0) continue;

        // 子组 depth01；有的文件直接放在 dayNNN 下
        rh_hid_t sg = H5Gopen2(g, "depth01", RH_H5P_DEFAULT);
        bool ownSg = (sg >= 0);
        if (!ownSg) sg = g;

        std::vector<double> alo, ala, llo, lla, rlo, rla;
        bool has = refH5ReadVar(sg, "main_axis_lon", alo) &&
                   refH5ReadVar(sg, "main_axis_lat", ala);
        if (has) {
            RefH5PolyDay day;
            day.dayOfYear = (int)dayNo;
            refH5BuildPoly(alo, ala, day.axis, &day.nAxis);
            if (day.nAxis >= 2) {
                if (refH5ReadVar(sg, "left_bdy_lon", llo) &&
                    refH5ReadVar(sg, "left_bdy_lat", lla))
                    refH5BuildPoly(llo, lla, day.left, NULL);
                if (refH5ReadVar(sg, "right_bdy_lon", rlo) &&
                    refH5ReadVar(sg, "right_bdy_lat", rla))
                    refH5BuildPoly(rlo, rla, day.right, NULL);
                out.push_back(day);
            }
        }

        if (ownSg) H5Gclose(sg);
        H5Gclose(g);
    }

    H5Fclose(f);

    std::sort(out.begin(), out.end(),
              [](const RefH5PolyDay &a, const RefH5PolyDay &b) { return a.dayOfYear < b.dayOfYear; });

    if (out.empty()) {
        err = "no dayNNN/depth01 axis data found in this file";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 便宜地判断「这是不是参考轴文件」：只看根组下有没有成片的 dayNNN 分组。
//
// 【为什么必须有这一层】目录树展开文件节点时走 nc_read_metadata -> nc_open，
// 而 nc_open 对这类「每天每层一组」的 HDF5 文件会递归读全部分组元数据：
// kuroshio_2021.nc 实测 2693 MB，Win32 上抛 std::bad_alloc，事后
// availVirtual 只剩 111 MB —— 下一个文件再展开就直接闪退
// （2026-11-04 实测，E:\ 下的 kuroshio_2021.nc + kuroshio_2022.nc 连炸两次）。
// 所以任何要 nc_open 的入口之前都先问这一句。
// ---------------------------------------------------------------------------
bool refH5LooksLikeRefFile(const char *pathUtf8)
{
    rh_hid_t f = H5Fopen(pathUtf8, RH_H5F_ACC_RDONLY, RH_H5P_DEFAULT);
    if (f < 0) return false;          // 不是 HDF5 -> 必然不是参考轴文件
    bool ok = false;
    rh_hsize_t nobj = 0;
    if (H5Gget_num_objs(f, &nobj) >= 0) {
        int nDay = 0;
        rh_hsize_t lim = (nobj < 32) ? nobj : 32;
        for (rh_hsize_t gi = 0; gi < lim; gi++) {
            char gname[64];
            memset(gname, 0, sizeof(gname));
            if (H5Gget_objname_by_idx(f, gi, gname, (unsigned long)(sizeof(gname) - 1)) <= 0)
                continue;
            gname[sizeof(gname) - 1] = 0;
            if (strncmp(gname, "day", 3) != 0) continue;
            char *endp = NULL;
            long d = strtol(gname + 3, &endp, 10);
            if (d > 0 && d <= 366 && endp && *endp == 0) nDay++;
        }
        ok = (nDay >= 3);            // 参考轴文件是 365/366 个 dayNNN，成片出现
    }
    H5Fclose(f);
    return ok;
}

#endif // NCDF_REFH5_INC
