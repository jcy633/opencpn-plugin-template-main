# -*- coding: utf-8 -*-
"""
kuro_diag10.py — 「参考路径到底在不在段 D 候选池里」逐条量。
上一轮只追了参考轴 25.0°N 交点那一条流线就下了结论，太强：
参考轴 25.5°N 交点的流线其实是能产出候选的。这里把池子里每一条都和参考路径比。
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ref

DAY = K.DAY
rlon, rlat = read_ref(DAY)
SECTIONS = K.SECTIONS; SEGD = K.SEGD

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
    return d if False else best

# 参考路径（段 D 附近）
refseg = [(float(lo), float(la)) for lo, la in zip(rlon, rlat) if 24.8 <= la <= 29.3]
print(f"day{DAY:03d} 参考段 D 路径 {len(refseg)} 点  "
      f"({refseg[0][0]:.2f},{refseg[0][1]:.2f}) -> ({refseg[-1][0]:.2f},{refseg[-1][1]:.2f})")

# 种子：沿 25°N 从 121.0 到 125.0（覆盖 C2/D1 命中容差 ±0.5° 的全部范围）
seeds = [(121.0 + k*K.CELL, 25.0) for k in range(int(4.0/K.CELL)+1)]
print(f"种子 {len(seeds)} 个（25°N, 121.0–125.0）")

allc = []
for (sl, sla) in seeds:
    fwd = K.integrate(sl, sla, +1.0)
    bwd = K.integrate(sl, sla, -1.0)
    if len(fwd)+len(bwd) < 4: continue
    full = list(reversed(bwd)) + fwd[1:]
    hits = [K.on_section(full, s) for s in SECTIONS]
    for tag, iA, iB, back, brk in [("main+", SEGD['iS'], SEGD['iN'], True, True),
                                   ("south->mid", SEGD['iS'], SEGD['iM'], False, False),
                                   ("mid->north", SEGD['iM'], SEGD['iN'], False, False)]:
        cs, st = K.collect(full, hits[iA], hits[iB], back, brk)
        for c in cs: c['tag'] = tag; c['seed'] = (sl, sla)
        allc.extend(cs)

main = [c for c in allc if c['tag'] == 'main+']
print(f"\nmain+ 候选 {len(main)} 条（降级 south->mid / mid->north 另有 "
      f"{sum(1 for c in allc if c['tag']=='south->mid')} / {sum(1 for c in allc if c['tag']=='mid->north')} 条）")

# 逐条量：候选 -> 参考路径 的垂距
for c in main:
    ds = np.array([xtrack(p, refseg) for p in c['xy']])
    c['d_med'] = float(np.median(ds)); c['d_max'] = float(ds.max()); c['d_p90'] = float(np.percentile(ds, 90))

print(f"\n{'#':>3} {'候选起点':>18} {'avgSp':>6} {'->参考 中位':>11} {'P90':>7} {'max':>7}   seed")
for i, c in enumerate(sorted(main, key=lambda c: c['d_med'])):
    s0, s1 = c['xy'][0], c['xy'][-1]
    print(f"{i:3d} ({s0[0]:7.3f},{s0[1]:6.3f}) {c['sp']:6.3f} "
          f"{c['d_med']:11.1f} {c['d_p90']:7.1f} {c['d_max']:7.1f}   ({c['seed'][0]:.3f},{c['seed'][1]:.2f})")

if main:
    best = min(main, key=lambda c: c['d_med'])
    print(f"\n最近的候选轨迹（每 5 点）: " +
          " ".join(f"({p[0]:.2f},{p[1]:.2f})" for p in best['xy'][::5]))
    print(f"参考路径（每 3 点）      : " +
          " ".join(f"({p[0]:.2f},{p[1]:.2f})" for p in refseg[::3]))
