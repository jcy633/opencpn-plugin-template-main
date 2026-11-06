# -*- coding: utf-8 -*-
"""kuro_diag8.py — 5 天横向对照：两个"眼睛"是不是稳定的系统性差异。"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, point_seg_d

DAYS = [(1, "20240101"), (100, "20240409"), (200, "20240718"), (300, "20241026"), (366, "20241231")]
BANDS = [("A  16-19.8", 16, 19.8), ("eye1 19.8-23.8", 19.8, 23.8),
         ("C  23.8-24.5", 23.8, 24.5), ("eye2 24.5-26.4", 24.5, 26.4),
         ("D  26.4-29", 26.4, 29.0), ("E  29-31.3", 29.0, 31.3)]

print(f"{'day':>5} {'band':16s} {'n':>4} {'中位km':>8} {'P90km':>8} {'maxkm':>8} {'ref_lon跨度':>12} {'our_lon跨度':>12}")
res = {}
for d, tag in DAYS:
    rlon, rlat = read_ref(d)
    olon, olat = read_ours(f"E:/nc1/kuro_ours_{tag}.txt")
    opoly = list(zip(olon, olat))
    dist = np.array([point_seg_d(p, opoly[i], opoly[i+1])[0]
                     for p in zip(rlon, rlat) for i in [0]] )  # placeholder
    # 正确算：点到整条折线
    def xtrack(p, poly):
        best = 1e18
        for i in range(len(poly)-1):
            dd, _ = point_seg_d(p, poly[i], poly[i+1])
            if dd < best: best = dd
        return best
    dist = np.array([xtrack(p, opoly) for p in zip(rlon, rlat)])
    res[d] = (rlon, rlat, olon, olat, dist)
    for name, y0, y1 in BANDS:
        m = (rlat >= y0) & (rlat <= y1)
        if m.sum() == 0: continue
        dd = dist[m]
        print(f"{d:5d} {name:16s} {m.sum():4d} {np.median(dd):8.2f} "
              f"{np.percentile(dd,90):8.2f} {dd.max():8.2f}")

print("\n--- 逐带中位偏差（5 天）---")
print(f"{'band':16s} " + "".join(f"{d:>9}" for d, _ in DAYS) + f"{'均值':>9}")
for name, y0, y1 in BANDS:
    vals = []
    for d, tag in DAYS:
        rlon, rlat, olon, olat, dist = res[d]
        m = (rlat >= y0) & (rlat <= y1)
        vals.append(np.median(dist[m]) if m.sum() else np.nan)
    print(f"{name:16s} " + "".join(f"{v:9.2f}" for v in vals) + f"{np.mean(vals):9.2f}")

print("\n--- 参考轴/我们的轴在 25°N 与 26.3°N 的经度（5 天）---")
for la in [25.0, 25.5, 26.0, 26.3]:
    line = f"  {la:5.2f}N  "
    for d, tag in DAYS:
        rlon, rlat, olon, olat, dist = res[d]
        i = int(np.argmin(np.abs(rlat - la))); j = int(np.argmin(np.abs(olat - la)))
        line += f" ref{rlon[i]:7.3f}/our{olon[j]:7.3f}  "
    print(line)

print("\n--- 5 天最大偏差位置（纬度）---")
for d, tag in DAYS:
    rlon, rlat, olon, olat, dist = res[d]
    i = int(np.argmax(dist))
    print(f"  day{d:03d}: max {dist.max():6.1f} km @ lat {rlat[i]:.3f} (ref {rlon[i]:.3f})")
