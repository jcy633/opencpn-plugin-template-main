# -*- coding: utf-8 -*-
"""kuro_diag7.py — 严格取样（四角全有效）复核关键剖面，排除海岸附近的假极大。"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ours

rlon = None
import netCDF4 as nc
ds = nc.Dataset("E:/nc1/kuroshio_2024.nc", "r")
g = ds.groups["day001"].groups["depth01"]
rlon = np.array(g.variables["main_axis_lon"][:], float)
rlat = np.array(g.variables["main_axis_lat"][:], float)
ds.close()
olon, olat = read_ours("E:/nc1/kuro_ours_20240101.txt")

def strict(lo, la):
    r = K.sample(lo, la)
    return None if r is None else math.hypot(*r)

print("严格取样（UVField::sample 语义：四角全有效）")
for la in [21.50, 22.12, 22.54, 24.50, 25.00, 25.50, 25.75, 26.00, 26.17, 26.25, 26.50]:
    ri = int(np.argmin(np.abs(rlat - la))); oi = int(np.argmin(np.abs(olat - la)))
    lons = np.arange(119.0, 127.01, 0.1667)
    vals = [(lo, strict(lo, la)) for lo in lons]
    good = [(lo, v) for lo, v in vals if v is not None]
    mx = max(good, key=lambda t: t[1])
    sr = strict(rlon[ri], rlat[ri]); so = strict(olon[oi], olat[oi])
    print(f"\n lat {la}:  ref({rlon[ri]:.3f},{rlat[ri]:.3f}) |u|={sr}   our({olon[oi]:.3f},{olat[oi]:.3f}) |u|={so}")
    print(f"   严格极大 = {mx[1]:.3f} @ lon {mx[0]:.3f}   （有效点 {len(good)}/{len(vals)}）")
    # 打印 120.5..126 的剖面
    s = ""
    for lo in np.arange(120.5, 126.51, 0.25):
        v = strict(lo, la)
        s += f"{lo:6.2f}:" + ("  .  " if v is None else f"{v:5.2f}") + " "
    print("   " + s)
