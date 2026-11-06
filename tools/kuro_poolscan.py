# -*- coding: utf-8 -*-
"""
kuro_poolscan.py — 逐条量候选池：离参考轴多远？评分为什么输给赢家？
回答用户的问题：**有没有离参考轴较近但就是没被选中的候选？**
用法: python -P tools/kuro_poolscan.py <day> <cand_D.csv> [glorys.nc]
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, point_seg_d

DAY = int(sys.argv[1])
CSV = sys.argv[2]
rlon, rlat = read_ref(DAY)
refseg = [(float(a), float(b)) for a, b in zip(rlon, rlat) if 24.8 <= b <= 29.4]

def xtrack_max_med(p, poly):
    ds = []
    for pt in p:
        best = 1e18
        for i in range(len(poly)-1):
            d, _ = point_seg_d(pt, poly[i], poly[i+1])
            if d < best: best = d
        ds.append(best)
    ds = np.array(ds)
    return float(ds.max()), float(np.median(ds)), float(np.percentile(ds, 90))

# 读池
cands = []
prev = None; have = 0
with open(CSV, encoding="utf-8") as f:
    cur = None
    for line in f:
        line = line.strip()
        if line.startswith("# seg="):
            parts = line.split()
            xy = parts[2].split("=", 1)[1].split(",")
            prev = (float(xy[0]), float(xy[1])); have = int(parts[3].split("=")[1])
            continue
        if line.startswith("# cand "):
            q = line.split()
            cur = dict(i=int(q[2]), sp=float(q[3].split("=")[1]), pts=[])
            cands.append(cur); continue
        if cur is not None and "," in line:
            a, b = line.split(",")
            cur["pts"].append((float(a), float(b)))

print(f"day{DAY:03d}  {len(cands)} 条候选   prev_end={prev}  havePrev={have}")
if prev is None:
    print("!! 没有 prev_end，无法复算评分"); sys.exit(1)

# 评分复算（与 pickBest 一致：score = 0.5*dN + 0.5*(1-sN)，min-max 归一）
dist = np.array([math.hypot(c["pts"][0][0]-prev[0], c["pts"][0][1]-prev[1]) for c in cands])
sp   = np.array([c["sp"] for c in cands])
dmin, dmax = dist.min(), dist.max()
smin, smax = sp.min(), sp.max()
dSpan = max(dmax-dmin, 1e-12); sSpan = max(smax-smin, 1e-12)
dN = (dist-dmin)/dSpan; sN = (sp-smin)/sSpan
score = 0.5*dN + 0.5*(1.0-sN)
win = int(np.argmin(score))
print(f"  dSpan(deg) {dmin:.4f}..{dmax:.4f}   sSpan {smin:.3f}..{smax:.3f} m/s")
print(f"  复算赢家 #{win}  score={score[win]:.3f} sp={sp[win]:.3f}  "
      f"({cands[win]['pts'][0][0]:.3f},{cands[win]['pts'][0][1]:.3f})->"
      f"({cands[win]['pts'][-1][0]:.3f},{cands[win]['pts'][-1][1]:.3f})")

# 逐条量离参考轴多远
print("\n  量每条候选 -> 参考路径的垂距（这一步较慢）...")
rec = []
for k, c in enumerate(cands):
    mx, md, p9 = xtrack_max_med(c["pts"], refseg)
    rec.append((mx, md, p9, k))
rec.sort()
print(f"\n{'#':>5} {'max偏差':>9} {'P90':>8} {'中位':>8} {'avgSp':>7} {'score':>7} {'rank':>6}   起点")
for mx, md, p9, k in rec[:12]:
    r = int(np.where(np.argsort(score) == k)[0][0])
    p = cands[k]["pts"]
    print(f"{k:5d} {mx:9.1f} {p9:8.1f} {md:8.1f} {sp[k]:7.3f} {score[k]:7.3f} {r:6d}   ({p[0][0]:.3f},{p[0][1]:.3f})->({p[-1][0]:.3f},{p[-1][1]:.3f})")

print("\n[关键判据] 离参考轴 max<70km 的候选有几条？")
n70 = sum(1 for mx,_,_,_ in rec if mx < 70)
n50 = sum(1 for mx,_,_,_ in rec if mx < 50)
n30 = sum(1 for mx,_,_,_ in rec if mx < 30)
print(f"  max<30km: {n30} 条   max<50km: {n50} 条   max<70km: {n70} 条   （总 {len(rec)}）")
if n70:
    print("  -> 存在「吸附半径内」的候选！看它们的评分/排名：")
    for mx, md, p9, k in rec[:40]:
        if mx >= 70: break
        r = int(np.where(np.argsort(score) == k)[0][0])
        p = cands[k]["pts"]
        print(f"     #{k} max={mx:.1f} sp={sp[k]:.3f} score={score[k]:.3f} rank={r}  "
              f"({p[0][0]:.3f},{p[0][1]:.3f})->({p[-1][0]:.3f},{p[-1][1]:.3f})")
else:
    print("  -> **没有任何候选在 70 km 内**，最接近的 max 偏差见上表")
