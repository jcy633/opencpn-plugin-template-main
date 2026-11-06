// tools/h5reftest.cpp — 参考轴 HDF5 直读的**进程外**验证程序
//
// 目的：ncdf_refh5.inc 里是手写的 HDF5 1.12 ABI 声明（工程里没有 hdf5.h）。
// 一旦 hid_t 大小、调用约定或某个签名猜错，在 OpenCPN 里就是一次闪退，很难查。
// 所以先用这个 32 位控制台程序把「能不能打开 / 能不能读对数 / 吃多少内存」验掉，
// 再进插件。32 位很关键：和插件一样受 2 GB 地址空间约束，量出来的内存才作数。
//
// 编译（x86，必须与 hdf5.dll 一致）见 tools/build_h5reftest.bat。
// 用法：h5reftest.exe <kuroshio_YYYY.nc>
//
// 校验点是 netCDF4 读出的 day001/depth01/main_axis_lon 前三个值（逐年不同），
// 用来证明 ABI 与类型/字节序转换都对，不是「能跑起来」就算过。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>

#include "../include/ncdf_refh5.inc"

// 进程内存水位：nc_open 那条路是 2693 MB，直读应当只有几十 MB
static void reportMem(const char *tag)
{
    PROCESS_MEMORY_COUNTERS pm;
    memset(&pm, 0, sizeof(pm));
    pm.cb = sizeof(pm);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm))) return;
    printf("[mem] %-22s workingSet=%6.0fMB  peakWorkingSet=%6.0fMB\n", tag,
           pm.WorkingSetSize / (1024.0 * 1024.0),
           pm.PeakWorkingSetSize / (1024.0 * 1024.0));
}

struct Expect { const char *name; double v[3]; };

// ---------------------------------------------------------------------------
// 路径编码探针：HDF5 的 H5Fopen 只收 char*，中文路径下「它到底认哪种编码」
// 直接决定插件里要不要转码。路径用**源码里的宽字符字面量**（\u 转义），
// 一个字节都不经过 bash/cmd —— 命令行传参的编码是另一个坑，会把结论搅浑。
//   先建硬链接： mklink /H "E:\测试中文目录\黑潮轴.nc" "E:\kuroshio_2021.nc"
// ---------------------------------------------------------------------------
static void probePathEncodings()
{
    // 目标路径 = E:\测试中文目录\黑潮轴.nc，逐个码点拼。
    // 不用 \u 转义：各种工具链会把它折叠掉，写出来反而看不出真假。
    std::wstring wp;
    wp += L'E'; wp += L':'; wp += L'\\';
    wp += (wchar_t)0x6D4B; wp += (wchar_t)0x8BD5; wp += (wchar_t)0x4E2D;
    wp += (wchar_t)0x6587; wp += (wchar_t)0x76EE; wp += (wchar_t)0x5F55;
    wp += L'\\';
    wp += (wchar_t)0x9ED1; wp += (wchar_t)0x6F6E; wp += (wchar_t)0x8F74;
    wp += L'.'; wp += L'n'; wp += L'c';

    // 自己把测试文件摆好（中文路径的硬链接），不借 shell —— shell 传中文
    // 路径本身就会被编码搅乱，那就分不清是被测对象坏还是测量方法坏。
    {
        std::wstring dir = wp.substr(0, wp.find_last_of(L'\\'));
        CreateDirectoryW(dir.c_str(), NULL);          // 已存在则失败，无妨
        std::wstring src = L"E:\\kuroshio_2021.nc";
        DeleteFileW(wp.c_str());
        if (!CreateHardLinkW(wp.c_str(), src.c_str(), NULL))
            printf("[probe] CreateHardLinkW failed err=%lu (continuing)\n", GetLastError());
    }
    printf("[probe] wide path = %ls\n", wp.c_str());

    const int cps[2] = {CP_ACP, CP_UTF8};
    const char *cpn[2] = {"CP_ACP (local codepage)", "CP_UTF8"};
    for (int i = 0; i < 2; i++) {
        int n = WideCharToMultiByte(cps[i], 0, wp.c_str(), (int)wp.size(), NULL, 0, NULL, NULL);
        std::string s((size_t)n, 0);
        WideCharToMultiByte(cps[i], 0, wp.c_str(), (int)wp.size(), &s[0], n, NULL, NULL);
        rh_hid_t f = H5Fopen(s.c_str(), RH_H5F_ACC_RDONLY, RH_H5P_DEFAULT);
        printf("[probe] %-24s -> %s\n", cpn[i], (f >= 0) ? "OPEN OK" : "failed");
        if (f >= 0) H5Fclose(f);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--probe") == 0) { probePathEncodings(); return 0; }

    const char *path = (argc > 1) ? argv[1] : "E:\\kuroshio_2021.nc";
    printf("h5reftest: sizeof(void*)=%u  (must be 4 for the Win32 plugin)\n",
           (unsigned)sizeof(void*));
    printf("h5reftest: file = %s\n", path);

    // netCDF4 读出的真值（day001/depth01/main_axis_lon[0..2]）
    static const Expect kExp[] = {
        {"kuroshio_2021.nc", {122.31011963, 122.39548970, 122.43583573}},
        {"kuroshio_2022.nc", {123.78354645, 123.74656831, 123.66914818}},
        {"kuroshio_2024.nc", {123.77737427, 123.73277087, 123.65028159}},
    };
    const Expect *exp = NULL;
    for (size_t i = 0; i < sizeof(kExp) / sizeof(kExp[0]); i++)
        if (strstr(path, kExp[i].name)) { exp = &kExp[i]; break; }

    reportMem("start");

    std::vector<RefH5PolyDay> days;
    std::string err;
    bool ok = refH5Load(path, days, err);
    printf("load_ok=%d  err=%s  n_days=%u\n", (int)ok, err.c_str(), (unsigned)days.size());
    reportMem("after load");
    if (!ok) return 2;

    printf("\nidx day  nAxis  axisPts  leftPts  rightPts   axis_start        axis_end\n");
    size_t show = days.size() < 6 ? days.size() : 6;
    for (size_t i = 0; i < show; i++) {
        const RefH5PolyDay &d = days[i];
        size_t na = d.axis.size() / 2, nl = d.left.size() / 2, nr = d.right.size() / 2;
        double s0 = (na ? d.axis[0] : 0), s1 = (na ? d.axis[1] : 0);
        double e0 = (na ? d.axis[d.axis.size() - 2] : 0), e1 = (na ? d.axis[d.axis.size() - 1] : 0);
        printf("%3u %03d  %5d  %7u  %7u  %8u   %8.4f,%7.4f  %8.4f,%7.4f\n",
               (unsigned)i, d.dayOfYear, d.nAxis, (unsigned)na, (unsigned)nl, (unsigned)nr,
               s0, s1, e0, e1);
    }
    if (days.size() > show) {
        const RefH5PolyDay &d = days[days.size() - 1];
        printf("... last: idx=%u day=%03d nAxis=%d axisPts=%u\n",
               (unsigned)(days.size() - 1), d.dayOfYear, d.nAxis,
               (unsigned)(d.axis.size() / 2));
    }

    // ---- 与 netCDF4 的对照值逐位比 ----
    if (!exp) {
        printf("\n[check] SKIP: no expected values registered for this file name\n");
        return 0;
    }
    if (days.empty() || days[0].axis.size() < 6) {
        printf("\n[check] FAIL: day001 axis too short\n");
        return 3;
    }
    double maxd = 0;
    for (int i = 0; i < 3; i++) {
        double dd = days[0].axis[2 * i] - exp->v[i];
        if (dd < 0) dd = -dd;
        if (dd > maxd) maxd = dd;
    }
    printf("\n[check] day001 main_axis_lon[0..2] vs netCDF4: max|diff|=%.3e  %s\n",
           maxd, (maxd < 1e-6) ? "PASS" : "FAIL");
    return (maxd < 1e-6) ? 0 : 3;
}
