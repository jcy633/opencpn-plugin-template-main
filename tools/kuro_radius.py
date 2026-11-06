# -*- coding: utf-8 -*-
"""kuro_radius.py — 同一天多个半径配置的分带对照。
用法: python -P tools/kuro_radius.py <day> <ours1.txt> <ours2.txt> ..."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, point_seg_d

DAY = int(sys.argv[1]); files = sys.argv[2:]
rlon, rlat = read_ref(DAY)
BANDS = [("A 16-19.8",16,19.8),("19.8-23.8",19.8,23.8),("C 23.8-24.5",23.8,24.5),
         ("24.5-26.4",24.5,26.4),("D 26.4-29",26.4,29.0),("E 29-31.3",29.0,31.3)]

rows = []
for f in files:
    olon, olat = read_ours(f)
    op = list(zip(olon, olat))
    d = np.array([min(point_seg_d(p, op[i], op[i+1])[0] for i in range(len(op)-1))
                  for p in zip(rlon, rlat)])
    row = [np.median(d)]
    for bn, y0, y1 in BANDS:
        m = (rlat >= y0) & (rlat <= y1)
        row.append(float(np.median(d[m])) if m.sum() else float("nan"))
    rows.append((os.path.basename(f).replace("kuro_", "").replace("_20240101.txt", ""), row))

print(f"{'配置':>10} {'总中位':>8} " + " ".join(f"{b[0]:>10}" for b in BANDS))
best = [min(r[1][i] for r in rows) for i in range(7)]
for name, row in rows:
    s = f"{name:>10} {row[0]:8.2f} "
    for i in range(1, 7):
        star = " *" if abs(row[i] - best[i]) < 1e-9 else "  "
        s += f"{row[i]:8.2f}{star}"
    print(s)
print(f"{'':>10} {'(*=该带最优)':>8}")
