# -*- coding: utf-8 -*-
"""
kuro_ab.py — 两套配置的分带对照（每份只算一次距离，别在带循环里重算）。
用法:
  PYTHONIOENCODING=utf-8 python -P tools/kuro_ab.py <day:tag...> -- <旧前缀> <新前缀>
例:
  python -P tools/kuro_ab.py 1:20240101 100:20240409 -- kuro_ours_ kuro_newcap_
"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, point_seg_d

args = sys.argv[1:]
sep = args.index("--")
pairs = [a.split(":") for a in args[:sep]]
oldp, newp = args[sep + 1], args[sep + 2]
BANDS = [("A 16-19.8", 16, 19.8), ("19.8-23.8", 19.8, 23.8), ("C 23.8-24.5", 23.8, 24.5),
         ("24.5-26.4", 24.5, 26.4), ("D 26.4-29", 26.4, 29.0), ("E 29-31.3", 29.0, 31.3)]


def dists(day, path):
    """返回 (参考轴纬度, 每个参考点到我们折线的垂距 km)"""
    rlon, rlat = read_ref(int(day))
    olon, olat = read_ours(path)
    op = list(zip(olon, olat))
    d = np.empty(len(rlon))
    for k, p in enumerate(zip(rlon, rlat)):
        d[k] = min(point_seg_d(p, op[i], op[i + 1])[0] for i in range(len(op) - 1))
    return rlat, d


# 每份只算一次
cache = {}
for day, tag in pairs:
    cache[(day, "old")] = dists(day, f"E:/nc1/{oldp}{tag}.txt")
    cache[(day, "new")] = dists(day, f"E:/nc1/{newp}{tag}.txt")

hdr = " ".join(f"{d:>8}" for d, _ in pairs)
print(f"{'带':>12} | {'旧（cap=1500 按速度剪枝）':^52} | {'新（不剪枝）':^52}")
print(f"{'':>12} | {hdr}{'均值':>8} | {hdr}{'均值':>8}")
for bn, y0, y1 in BANDS:
    ro, rn = [], []
    for day, _ in pairs:
        la, d = cache[(day, "old")]; m = (la >= y0) & (la <= y1)
        ro.append(float(np.median(d[m])) if m.sum() else float("nan"))
        la, d = cache[(day, "new")]; m = (la >= y0) & (la <= y1)
        rn.append(float(np.median(d[m])) if m.sum() else float("nan"))
    mo, mn = np.nanmean(ro), np.nanmean(rn)
    mark = "  <- 变好" if mn < mo else ("  <- 变差" if mn > mo else "")
    print(f"{bn:>12} | " + " ".join(f"{v:8.2f}" for v in ro) + f"{mo:8.2f} | "
          + " ".join(f"{v:8.2f}" for v in rn) + f"{mn:8.2f}{mark}")

print("\n[总中位]")
ro = [float(np.median(cache[(d, "old")][1])) for d, _ in pairs]
rn = [float(np.median(cache[(d, "new")][1])) for d, _ in pairs]
print("  旧: " + " ".join(f"{v:6.2f}" for v in ro) + f"   均值 {np.mean(ro):.2f}")
print("  新: " + " ".join(f"{v:6.2f}" for v in rn) + f"   均值 {np.mean(rn):.2f}")
