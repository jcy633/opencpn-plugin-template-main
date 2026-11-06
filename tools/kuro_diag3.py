# -*- coding: utf-8 -*-
"""
kuro_diag3.py — 第三轮判别：
  1) 发布数据集的左/右边界间距沿轴变化 —— 判定参考轴在眼睛区是不是"真射流"
  2) 24–27°N 逐纬度的横向速度剖面（含脊位置、参考、我们的）
  3) 参考轴"折返"（hairpin）的量化
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ours, read_uv, bilin, hav

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
ROOT = "E:/nc1"
GLORYS = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else f"{ROOT}/glorys_20240101.nc"
OURS = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else f"{ROOT}/kuro_ours_20240101.txt"
KURO = f"{ROOT}/kuroshio_2024.nc"

import netCDF4 as nc
ds = nc.Dataset(KURO, "r")
g = ds.groups[f"day{DAY:03d}"].groups["depth01"]
rlon = np.array(g.variables["main_axis_lon"][:], float)
rlat = np.array(g.variables["main_axis_lat"][:], float)
llon = np.array(g.variables["left_bdy_lon"][:], float)
llat = np.array(g.variables["left_bdy_lat"][:], float)
rblon = np.array(g.variables["right_bdy_lon"][:], float)
rblat = np.array(g.variables["right_bdy_lat"][:], float)
ds.close()
print(f"day{DAY:03d}: axis {len(rlon)} pts, left {len(llon)}, right {len(rblon)}")
print(f"  axis  lon {rlon.min():.2f}..{rlon.max():.2f}  lat {rlat.min():.2f}..{rlat.max():.2f}")
print(f"  left  lon {llon.min():.2f}..{llon.max():.2f}  lat {llat.min():.2f}..{llat.max():.2f}")
print(f"  right lon {rblon.min():.2f}..{rblon.max():.2f}  lat {rblat.min():.2f}..{rblat.max():.2f}")

# 左右边界间距（逐点）
w = np.array([hav((llon[i], llat[i]), (rblon[i], rblat[i])) for i in range(len(rlon))])
print(f"\n[左-右边界间距 km]  中位 {np.median(w):.1f}  均值 {w.mean():.1f}  "
      f"P90 {np.percentile(w,90):.1f}  max {w.max():.1f}  min {w.min():.1f}")

print("\n[逐点]  i    lat     axis_lon  width(km)   left_lon  right_lon")
for i in range(len(rlat)):
    if i % 3 == 0 or 20.0 <= rlat[i] <= 26.5:
        print(f"  {i:3d} {rlat[i]:7.3f} {rlon[i]:9.3f} {w[i]:9.1f}   {llon[i]:8.3f} {rblon[i]:9.3f}")

# 横向剖面
olon, olat = read_ours(OURS)
glat, glon, U, V = read_uv(GLORYS)
SP = np.hypot(U, V)

print("\n[24–27°N 横向剖面]  粗体=该纬度窗内极大；* = ref；+ = ours")
print("  lat   " + "".join(f"{l:5.1f}" for l in np.arange(121.5, 126.51, 0.25)))
for la in np.arange(24.25, 26.76, 0.125):
    lons = np.arange(121.5, 126.51, 0.25)
    vals = np.array([bilin(glat, glon, SP, la, lo) for lo in lons])
    ok = np.isfinite(vals)
    imax = int(np.nanargmax(np.where(ok, vals, -1)))
    ri = int(np.argmin(np.abs(rlat - la))); oi = int(np.argmin(np.abs(olat - la)))
    line = f"{la:6.2f}  "
    for k, lo in enumerate(lons):
        v = vals[k]
        s = "  .  " if not np.isfinite(v) else f"{v:5.2f}"
        if np.isfinite(v):
            if abs(lo - rlon[ri]) < 0.13: s = f"{v:4.2f}*"
            elif abs(lo - olon[oi]) < 0.13: s = f"{v:4.2f}+"
            elif k == imax: s = f"[{v:4.2f}]"[:5]
        line += s
    print(line)

# 折返量化
print("\n[参考轴折返] lat 非单调处：")
for i in range(1, len(rlat)-1):
    d1 = rlat[i]-rlat[i-1]; d2 = rlat[i+1]-rlat[i]
    if d1*d2 < 0:
        print(f"   拐点 i={i}  ({rlon[i]:.3f},{rlat[i]:.3f})  转向 dlat {d1:+.3f} -> {d2:+.3f}")
print("[我们的轴折返]")
for i in range(1, len(olat)-1):
    d1 = olat[i]-olat[i-1]; d2 = olat[i+1]-olat[i]
    if d1*d2 < 0:
        print(f"   拐点 i={i}  ({olon[i]:.3f},{olat[i]:.3f})  转向 dlat {d1:+.3f} -> {d2:+.3f}")
