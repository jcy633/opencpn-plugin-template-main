# -*- coding: utf-8 -*-
"""kuro_span.py — 两条轴在指定纬度带的逐点对照。"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours
DAY = int(sys.argv[1]); files = sys.argv[2:]
rlon, rlat = read_ref(DAY)
print(" lat    ref_lon " + "".join(f"  ours:{os.path.basename(f)[:14]:>14}" for f in files))
for la in np.arange(25.0, 29.6, 0.25):
    i = int(np.argmin(np.abs(rlat - la)))
    if abs(rlat[i]-la) > 0.18: continue
    line = f"{rlat[i]:6.2f} {rlon[i]:9.3f} "
    for f in files:
        olon, olat = read_ours(f)
        j = int(np.argmin(np.abs(olat - la)))
        line += f"   {olon[j]:8.3f}({(olon[j]-rlon[i])*101*math.cos(math.radians(la)):6.0f}km)" if False else f"   {olon[j]:8.3f}"
    print(line)
import math
