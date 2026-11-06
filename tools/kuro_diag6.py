# -*- coding: utf-8 -*-
"""kuro_diag6.py — 逐条追踪：从参考轴/我们的轴的 25°N 交点出发，流线到底去哪、
候选为什么被拒。
用法: PYTHONIOENCODING=utf-8 python -P tools/kuro_diag6.py [day] [glorys.nc] [ours.txt]
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ours, read_ref

DAY = K.DAY
rlon, rlat = read_ref(DAY)
olon, olat = read_ours(sys.argv[3] if len(sys.argv) > 3 and sys.argv[3]
                       else "E:/nc1/kuro_ours_20240101.txt")
SECTIONS = K.SECTIONS; SEGD = K.SEGD

def nearest(lon, lat, la):
    i = int(np.argmin(np.abs(lat - la)))
    return lon[i], lat[i], i

def trace(tag, lo0, la0):
    print(f"\n===== {tag}  种子 ({lo0:.3f},{la0:.3f}) =====")
    fwd = K.integrate(lo0, la0, +1.0)
    bwd = K.integrate(lo0, la0, -1.0)
    full = list(reversed(bwd)) + fwd[1:]
    print(f"  前向终点 ({fwd[-1][0]:.3f},{fwd[-1][1]:.3f})  n={len(fwd)}   后向终点 ({bwd[-1][0]:.3f},{bwd[-1][1]:.3f})  n={len(bwd)}")
    print(f"  轨迹（每 6 点）: " + " ".join(f"({p[0]:.2f},{p[1]:.2f})" for p in full[::6]))
    hits = [K.on_section(full, s) for s in SECTIONS]
    hs = " ".join(f"{SECTIONS[i][0]}={'Y' if hits[i] else '-'}" for i in range(11))
    print(f"  断面命中: {hs}")
    for tag2, iA, iB, back, brk in [("main+", SEGD['iS'], SEGD['iN'], True, True),
                                    ("south->mid", SEGD['iS'], SEGD['iM'], False, False),
                                    ("mid->north", SEGD['iM'], SEGD['iN'], False, False)]:
        cs, st = K.collect(full, hits[iA], hits[iB], back, brk)
        print(f"    [{tag2}] {st}  -> {len(cs)} 条" +
              (f"  首条 ({cs[0]['xy'][0][0]:.2f},{cs[0]['xy'][0][1]:.2f})->({cs[0]['xy'][-1][0]:.2f},{cs[0]['xy'][-1][1]:.2f}) sp={cs[0]['sp']:.3f}" if cs else ""))

print(f"\n########## day{DAY:03d} ##########")
print("参考轴 25°N 交点:", nearest(rlon, rlat, 25.0)[:2])
print("我们的轴 25°N 交点:", nearest(olon, olat, 25.0)[:2])
trace("参考轴 25°N 交点", *nearest(rlon, rlat, 25.0)[:2])
trace("我们的轴 25°N 交点", *nearest(olon, olat, 25.0)[:2])
