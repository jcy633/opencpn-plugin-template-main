# -*- coding: utf-8 -*-
"""
kuro_diag_axis.py — Wang2026 主轴：我们的轴 vs 发布数据集轴 的偏差诊断

用法（从任意目录，用 -I 跑）：
    python -I tools/kuro_diag_axis.py <day> [glorys.nc] [out_prefix]

输出：
    <out_prefix>_profile.csv  逐纬度剖面（ref lon / our lon / Δkm / 三个速度）
    <out_prefix>_refscan.csv  参考轴每点的「垂断面极大」偏离量
    控制台摘要

注意：netCDF4 打不开含中文的路径 —— 所有输入路径必须是纯 ASCII。
"""
import sys, os, math
import numpy as np

# ---------------------------------------------------------------- 配置
# 解析容错：本模块会被其它脚本 import，argv 不一定是本脚本的参数。
def _argv(i, default):
    try:
        v = sys.argv[i]
        return default if v == "" else v
    except IndexError:
        return default

DAY = int(_argv(1, 1)) if str(_argv(1, 1)).lstrip("-").isdigit() else 1
ROOT = "E:/nc1"
KURO = f"{ROOT}/kuroshio_2024.nc"
OURS = f"{ROOT}/kuro_ours_20240101.txt"
GLORYS = f"{ROOT}/glorys_20240101.nc"
OUT = f"{ROOT}/kuro_diag"
if len(sys.argv) > 2 and sys.argv[2]:
    GLORYS = sys.argv[2]
if len(sys.argv) > 3 and sys.argv[3]:
    OURS = sys.argv[3]
if len(sys.argv) > 4 and sys.argv[4]:
    OUT = sys.argv[4]

RE = 6371.0
SIN = math.sin; COS = math.cos; ASIN = math.asin; ATAN2 = math.atan2; SQRT = math.sqrt

def hav(a, b):
    """haversine, a/b = (lon,lat) 度"""
    p1, p2 = math.radians(a[1]), math.radians(b[1])
    dp = p2 - p1
    dl = math.radians(b[0] - a[0])
    h = SIN(dp/2)**2 + COS(p1)*COS(p2)*SIN(dl/2)**2
    return 2*RE*ASIN(SQRT(h))

# ---------------------------------------------------------------- 读参考轴
def read_ref(day):
    import netCDF4 as nc
    ds = nc.Dataset(KURO, "r")
    dayk = f"day{day:03d}"
    if dayk not in ds.groups:
        print("!! no group", dayk, "available:", sorted(ds.groups)[:5], "...")
        sys.exit(1)
    gday = ds.groups[dayk]
    depk = "depth01"
    if depk not in gday.groups:
        print("!! no subgroup", depk, "available:", sorted(gday.groups)[:5], "...")
        sys.exit(1)
    grp = gday.groups[depk]
    lon = np.array(grp.variables["main_axis_lon"][:], dtype=float)
    lat = np.array(grp.variables["main_axis_lat"][:], dtype=float)
    ds.close()
    ok = np.isfinite(lon) & np.isfinite(lat)
    return lon[ok], lat[ok]

# ---------------------------------------------------------------- 读我们的轴
def read_ours(path):
    lon, lat = [], []
    started = False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line == "lon,lat":
                started = True
                continue
            if not started or not line:
                continue
            parts = line.split(",")
            if len(parts) != 2:
                continue
            try:
                lon.append(float(parts[0])); lat.append(float(parts[1]))
            except ValueError:
                continue
    return np.array(lon), np.array(lat)

# ---------------------------------------------------------------- 点到折线垂距
def point_seg_d(p, a, b):
    """p=(lon,lat) 到折线段 a-b 的垂距（km），返回 (距离, 段内最近点)"""
    la = math.radians(a[1])
    # 局部平面近似（米/度）
    mx = 111320.0 * COS(la); my = 110540.0
    px, py = (p[0]-a[0])*mx, (p[1]-a[1])*my
    bx, by = (b[0]-a[0])*mx, (b[1]-a[1])*my
    L2 = bx*bx + by*by
    if L2 < 1e-12:
        t = 0.0
    else:
        t = (px*bx + py*by) / L2
        t = max(0.0, min(1.0, t))
    qx, qy = t*bx, t*by
    d = SQRT((px-qx)**2 + (py-qy)**2)
    return d/1000.0, (a[0] + (b[0]-a[0])*t, a[1] + (b[1]-a[1])*t)

def xtrack(p, poly):
    """点到折线的最小垂距（km）"""
    best = 1e18
    for i in range(len(poly)-1):
        d, _ = point_seg_d(p, poly[i], poly[i+1])
        if d < best: best = d
    return best

# ---------------------------------------------------------------- 读 GLORYS
def read_uv(path):
    import netCDF4 as nc
    ds = nc.Dataset(path, "r")
    def find(*names):
        for n in names:
            if n in ds.variables: return n
        return None
    vu = find("uo", "u", "eastward_sea_water_velocity", "water_u")
    vv = find("vo", "v", "northward_sea_water_velocity", "water_v")
    vl = find("latitude", "lat")
    vn = find("longitude", "lon")
    lat = np.array(ds.variables[vl][:], dtype=float)
    lon = np.array(ds.variables[vn][:], dtype=float)

    def unpack(var):
        # netCDF4 默认 set_auto_maskandscale=True，会自动做 CF 解包
        # （scale_factor/add_offset/_FillValue）——不要再手动乘一遍。
        arr = np.ma.filled(var[:].astype(float), np.nan)
        # 去掉时间/深度等长度为 1 的前导维，只留 (lat, lon)
        while arr.ndim > 2:
            assert arr.shape[0] == 1, f"unexpected leading dim {arr.shape}"
            arr = arr[0]
        return arr

    U = unpack(ds.variables[vu]); V = unpack(ds.variables[vv])
    ds.close()
    print(f"   uv shapes U{U.shape} V{V.shape}  lat{lat.shape} lon{lon.shape}  "
          f"|u|max={np.nanmax(np.hypot(U,V)):.3f}")
    return lat, lon, U, V

def bilin(lat, lon, A, la, lo):
    """A[j,i]，lat/lon 一维。越界返回 nan"""
    if la < lat[0] or la > lat[-1] or lo < lon[0] or lo > lon[-1]:
        return np.nan
    dj = np.searchsorted(lat, la) - 1
    di = np.searchsorted(lon, lo) - 1
    dj = max(0, min(dj, len(lat)-2)); di = max(0, min(di, len(lon)-2))
    t = (la - lat[dj]) / (lat[dj+1]-lat[dj] + 1e-18)
    s = (lo - lon[di]) / (lon[di+1]-lon[di] + 1e-18)
    a = A[dj, di]; b = A[dj, di+1]; c = A[dj+1, di]; d = A[dj+1, di+1]
    vals = np.array([a, b, c, d], dtype=float)
    w = np.array([(1-t)*(1-s), (1-t)*s, t*(1-s), t*s])
    if np.all(np.isnan(vals)): return np.nan
    m = ~np.isnan(vals)
    return float(np.sum(vals[m]*w[m]) / (np.sum(w[m]) + 1e-18))

# ---------------------------------------------------------------- 主流程
def main():
    print(f"=== day{DAY:03d} ===")
    rlon, rlat = read_ref(DAY)
    olon, olat = read_ours(OURS)
    print(f"参考轴 {len(rlon)} 点  ({rlon[0]:.3f},{rlat[0]:.3f}) -> ({rlon[-1]:.3f},{rlat[-1]:.3f})")
    print(f"我们的 {len(olon)} 点  ({olon[0]:.3f},{olat[0]:.3f}) -> ({olon[-1]:.3f},{olat[-1]:.3f})")
    rpoly = list(zip(rlon, rlat)); opoly = list(zip(olon, olat))

    # 参考点 -> 我们的折线 垂距
    d_ref2our = np.array([xtrack(p, opoly) for p in rpoly])
    # 我们的点 -> 参考折线 垂距
    d_our2ref = np.array([xtrack(p, rpoly) for p in opoly])

    print(f"\n[参考->我们] 中位 {np.median(d_ref2our):.2f}  均值 {d_ref2our.mean():.2f}"
          f"  P90 {np.percentile(d_ref2our,90):.2f}  max {d_ref2our.max():.2f}")
    print(f"[我们->参考] 中位 {np.median(d_our2ref):.2f}  均值 {d_our2ref.mean():.2f}"
          f"  P90 {np.percentile(d_our2ref,90):.2f}  max {d_our2ref.max():.2f}")

    # 沿参考轴的误差剖面
    if os.environ.get("VERBOSE"):
        print("\n[误差剖面 —— 沿参考轴]  lat      d(km)")
        for i in range(len(rlat)):
            bar = "#" * int(min(60, d_ref2our[i]/1.5))
            print(f"  {i:3d} {rlat[i]:7.3f} {d_ref2our[i]:8.2f} {bar}")

    # 读流场
    glat, glon, U, V = read_uv(GLORYS)
    SP = np.hypot(U, V)
    print(f"\nGLORYS: lat {glat[0]:.3f}..{glat[-1]:.3f} (n={len(glat)})  "
          f"lon {glon[0]:.3f}..{glon[-1]:.3f} (n={len(glon)})")
    print(f"        speed: valid {np.isfinite(SP).sum()} / {SP.size}  max {np.nanmax(SP):.3f}")

    # 沿参考轴逐点：垂直于切向的剖面 ±70 km，找极大
    print("\n[参考轴是不是速度脊]  i    lat    ref_lon  our_lon   d(km)  |u|@ref |u|@our |u|max  max偏离ref(km)")
    rows = []
    for i in range(len(rlat)):
        j = max(0, i-1); k = min(len(rlat)-1, i+1)
        tl = (rlon[k]-rlon[j], rlat[k]-rlat[j])
        n = SQRT(tl[0]**2 + tl[1]**2) + 1e-18
        # 垂线方向（度方向），按度数走，换算成 km 时用局部比例
        nx, ny = -tl[1]/n, tl[0]/n
        la = rlat[i]
        kmx = 111.32*COS(math.radians(la)); kmy = 110.54
        # 用 km 参数化：dx_km = nx_km, dy_km = ny_km
        lx, ly = nx*kmx, ny*kmy   # 每度的方向向量换算成 km/deg*方向
        nn = SQRT(lx*lx + ly*ly) + 1e-18
        lx, ly = lx/nn, ly/nn
        best = -1.0; bestoff = 0.0
        for t in np.arange(-70.0, 70.01, 1.0):
            la2 = rlat[i] + ly*t/kmy
            lo2 = rlon[i] + lx*t/kmx
            s = bilin(glat, glon, SP, la2, lo2)
            if np.isfinite(s) and s > best:
                best = s; bestoff = t
        sref = bilin(glat, glon, SP, rlat[i], rlon[i])
        # 我们的轴在同一纬度的经度
        oidx = int(np.argmin(np.abs(olat - rlat[i])))
        sour = bilin(glat, glon, SP, olat[oidx], olon[oidx])
        rows.append((rlat[i], d_ref2our[i], sref, sour, best, bestoff, rlon[i], olon[oidx]))
        if i % 3 == 0 or d_ref2our[i] > 20:
            print(f"  {i:3d} {rlat[i]:7.3f} {rlon[i]:8.3f} {olon[oidx]:8.3f} "
                  f"{d_ref2our[i]:7.1f}  {sref:6.3f}  {sour:6.3f} {best:6.3f}   {bestoff:+8.1f}")
    A = np.array(rows)
    np.savetxt(f"{OUT}_refscan.csv", A, delimiter=",",
               header="lat,d_ref2our_km,sp_ref,sp_our,sp_max,offset_of_max_km,ref_lon,our_lon", comments="")
    np.savetxt(f"{OUT}_profile.csv",
               np.column_stack([rlat, rlon, d_ref2our]), delimiter=",",
               header="lat,ref_lon,d_ref2our_km", comments="")
    print(f"\n写出 {OUT}_refscan.csv / {OUT}_profile.csv")

    # 汇总：偏差大的地方，参考轴离脊多远
    big = A[A[:,1] > 20]
    if len(big):
        print(f"\n偏差>20km 的点：{len(big)} 个，参考轴离脊中位 {np.median(np.abs(big[:,5])):.1f} km，"
              f"参考|u|中位 {np.median(big[:,2]):.3f}，剖面max中位 {np.median(big[:,4]):.3f}")
    small = A[A[:,1] < 5]
    if len(small):
        print(f"偏差<5km 的点：{len(small)} 个，参考轴离脊中位 {np.median(np.abs(small[:,5])):.1f} km，"
              f"参考|u|中位 {np.median(small[:,2]):.3f}，剖面max中位 {np.median(small[:,4]):.3f}")

if __name__ == "__main__":
    main()
