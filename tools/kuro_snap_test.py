# -*- coding: utf-8 -*-
"""
kuro_snap_test.py — 离线试 correctAxisTransect 的半径敏感性。
把我们最终轴当骨架，按论文口径（垂线 = 流向+90°、沿流投影 argmax、alpha=1 硬吸附）
用不同半径重吸附，看能不能补上 day001 的 26.4–29 段。**不改插件、不重编译。**
"""
import sys, os, math
import numpy as np

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
_TAG = {1: '20240101', 100: '20240409', 200: '20240718',
        300: '20241026', 366: '20241231'}.get(DAY, '20240101')
os.environ.setdefault('KURO_GLORYS', f'E:/nc1/glorys_{_TAG}.nc')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, read_uv, point_seg_d
import kuro_diag5 as K

OURS = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else "E:/nc1/kuro_newcap_20240101.txt"
NSECT = 21

rlon, rlat = read_ref(DAY)
olon, olat = read_ours(OURS)

def snap(lon, lat, half_km):
    """correctAxisTransect 等价：垂线取自**流向**（atan2(v,u)+90°），沿流投影 argmax"""
    out = list(zip(lon, lat))
    for i in range(len(lon)):
        r = K.sample(lon[i], lat[i])
        if r is None or (r[0] == 0 and r[1] == 0):
            continue
        th = math.atan2(r[1], r[0])
        degLat = half_km / 110.54
        cLat = max(math.cos(math.radians(lat[i])), 1e-6)
        degLon = half_km / (111.32 * cLat)
        perp = th + math.pi / 2
        dLon = degLon * math.cos(perp); dLat = degLat * math.sin(perp)
        best, bl, bt = -1e300, lon[i], lat[i]
        for k in range(NSECT):
            t = k / (NSECT - 1)
            lo = (lon[i] - dLon) + 2 * dLon * t
            la = (lat[i] - dLat) + 2 * dLat * t
            rr = K.sample(lo, la)
            if rr is None: continue
            vp = rr[0] * math.cos(th) + rr[1] * math.sin(th)
            if vp > best: best, bl, bt = vp, lo, la
        out[i] = (bl, bt)
    return out

def bands(poly, tag):
    op = poly
    d = np.array([min(point_seg_d(p, op[i], op[i+1])[0] for i in range(len(op)-1))
                  for p in zip(rlon, rlat)])
    B = [("A 16-19.8",16,19.8),("19.8-23.8",19.8,23.8),("C 23.8-24.5",23.8,24.5),
         ("24.5-26.4",24.5,26.4),("D 26.4-29",26.4,29.0),("E 29-31.3",29.0,31.3)]
    s = f"  {tag:16s} 总中位 {np.median(d):5.2f} |"
    for bn, y0, y1 in B:
        m = (rlat >= y0) & (rlat <= y1)
        s += f" {np.median(d[m]) if m.sum() else float('nan'):7.2f}"
    print(s)

print(f"day{DAY:03d}   半径敏感性（把最终轴当骨架重吸附）")
print(f"{'':18s}{'':11s} | " + " ".join(f"{b[0]:>8}" for b in
      [("A 16-19.8",0,0),("19.8-23.8",0,0),("C 23.8-24.5",0,0),
       ("24.5-26.4",0,0),("D 26.4-29",0,0),("E 29-31.3",0,0)]))
bands(list(zip(olon, olat)), "R=0 (原轴)")
for R in (70, 110, 150, 200, 250):
    bands(snap(olon, olat, R), f"R={R} km")
