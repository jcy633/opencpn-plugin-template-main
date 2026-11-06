# -*- coding: utf-8 -*-
"""
kuro_diag4.py — 流线一致性判别
  从两条轴上取种子，RK4 积分流线（物理正确的单位切向、步长 1 km），
  看流线是否贴着该轴走 —— 判定"谁是流线、谁不是"。
用法: PYTHONIOENCODING=utf-8 python -P tools/kuro_diag4.py [day] [glorys.nc] [ours.txt]
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
ds.close()
olon, olat = read_ours(OURS)
glat, glon, U, V = read_uv(GLORYS)

def vel(lo, la):
    u = bilin(glat, glon, U, la, lo); v = bilin(glat, glon, V, la, lo)
    if not (np.isfinite(u) and np.isfinite(v)): return None
    return u, v

def dir_phys(lo, la):
    """物理正确的单位方向（东/北 的 km 分量归一）"""
    p = vel(lo, la)
    if p is None: return None
    u, v = p
    kmx = 111.32*math.cos(math.radians(la)); kmy = 110.54
    ex, ey = u*kmx, v*kmy          # km/s
    n = math.hypot(ex, ey)
    if n < 1e-9: return None
    return ex/n, ey/n, u, v

def integrate(lo, la, sign, nstep=100, dkm=1.0):
    """RK4 沿流向(sign=+1)/逆流向(sign=-1)积分，返回轨迹"""
    tr = [(lo, la)]
    for _ in range(nstep):
        def d(p):
            r = dir_phys(p[0], p[1])
            return None if r is None else (sign*r[0], sign*r[1])
        k1 = d((lo, la))
        if k1 is None: break
        k2 = d((lo + k1[0]*dkm/2/(111.32*math.cos(math.radians(la))), la + k1[1]*dkm/2/110.54))
        if k2 is None: break
        k3 = d((lo + k2[0]*dkm/2/(111.32*math.cos(math.radians(la))), la + k2[1]*dkm/2/110.54))
        if k3 is None: break
        k4 = d((lo + k3[0]*dkm/(111.32*math.cos(math.radians(la))), la + k3[1]*dkm/110.54))
        if k4 is None: break
        ax = (k1[0] + 2*k2[0] + 2*k3[0] + k4[0])/6
        ay = (k1[1] + 2*k2[1] + 2*k3[1] + k4[1])/6
        kmx = 111.32*math.cos(math.radians(la)); kmy = 110.54
        lo = lo + ax*dkm/kmx
        la = la + ay*dkm/kmy
        if not (113 <= lo <= 134 and 13 <= la <= 34): break
        tr.append((lo, la))
    return tr

def xtrack(p, poly):
    best = 1e18
    for i in range(len(poly)-1):
        a, b = poly[i], poly[i+1]
        la = math.radians(a[1])
        mx = 111320.0*math.cos(la); my = 110540.0
        px, py = (p[0]-a[0])*mx, (p[1]-a[1])*my
        bx, by = (b[0]-a[0])*mx, (b[1]-a[1])*my
        L2 = bx*bx+by*by
        t = 0.0 if L2 < 1e-12 else max(0.0, min(1.0, (px*bx+py*by)/L2))
        d = math.hypot(px-t*bx, py-t*by)/1000.0
        if d < best: best = d
    return best

rpoly = list(zip(rlon, rlat)); opoly = list(zip(olon, olat))

print(f"=== day{DAY:03d}  流线一致性 ===")
print("从轴上取种子 -> 积分流线(±400 km) -> 量流线离另一条/自己轴的垂距")
print("\n种子纬度   种子轴   流线->参考轴垂距(km)      流线->我们的轴垂距(km)")
print("                    中位   P90   max           中位   P90   max")
for la in [17.5, 19.0, 20.5, 21.5, 22.5, 23.5, 24.5, 25.0, 25.5, 26.0, 26.3, 26.8, 27.5, 29.0, 30.5]:
    for tag, lon_, lat_ in [("ref", rlon, rlat), ("our", olon, olat)]:
        i = int(np.argmin(np.abs(lat_ - la)))
        if abs(lat_[i] - la) > 0.35: continue
        p0 = (lon_[i], lat_[i])
        fwd = integrate(*p0, +1); bwd = integrate(*p0, -1)
        tr = list(reversed(bwd)) + fwd
        if len(tr) < 20:
            print(f"  {la:6.2f}  {tag}    (流线太短 n={len(tr)})"); continue
        dr = np.array([xtrack(p, rpoly) for p in tr])
        do = np.array([xtrack(p, opoly) for p in tr])
        print(f"  {la:6.2f}  {tag}   {np.median(dr):7.1f} {np.percentile(dr,90):6.1f} {dr.max():6.1f}"
              f"        {np.median(do):7.1f} {np.percentile(do,90):6.1f} {do.max():6.1f}"
              f"   (n={len(tr)})")
