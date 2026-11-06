# -*- coding: utf-8 -*-
"""kuro_cmp.py — 分带比对若干份 our 轴输出与参考轴。用法:
   PYTHONIOENCODING=utf-8 python -P tools/kuro_cmp.py <day> <ours1.txt> [ours2.txt ...]"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, point_seg_d

DAY = int(sys.argv[1])
files = sys.argv[2:]
rlon, rlat = read_ref(DAY)
BANDS = [("A 16-19.8",16,19.8),("19.8-23.8",19.8,23.8),("C 23.8-24.5",23.8,24.5),
         ("24.5-26.4",24.5,26.4),("D 26.4-29",26.4,29.0),("E 29-31.3",29.0,31.3)]

def xtrack(p, poly):
    return min(point_seg_d(p, poly[i], poly[i+1])[0] for i in range(len(poly)-1))

for f in files:
    olon, olat = read_ours(f)
    opoly = list(zip(olon, olat))
    d = np.array([xtrack(p, opoly) for p in zip(rlon, rlat)])
    name = os.path.basename(f)
    print(f"\n{name}   n={len(olon)}  总中位 {np.median(d):.2f}  均值 {d.mean():.2f}  max {d.max():.1f}")
    print("   带            中位     P90     max")
    for bn, y0, y1 in BANDS:
        m = (rlat >= y0) & (rlat <= y1)
        if not m.sum(): continue
        dd = d[m]
        print(f"   {bn:12s} {np.median(dd):7.2f} {np.percentile(dd,90):7.2f} {dd.max():7.2f}")
