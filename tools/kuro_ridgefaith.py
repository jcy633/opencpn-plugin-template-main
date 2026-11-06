# -*- coding: utf-8 -*-
"""
kuro_ridgefaith.py — 「谁更贴最大流速脊」的客观度量：沿轴 |u| 的均值/中位。
Wang2026 的方法定义是「流线约束**最大流速**」，所以沿轴流速高 = 更忠于方法。
用法: python -P tools/kuro_ridgefaith.py <day:tag:glorys...> -- <前缀1> <前缀2>
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

args = sys.argv[1:]
sep = args.index("--")
trip = [a.split(":") for a in args[:sep]]
p1, p2 = args[sep+1], args[sep+2]

from kuro_diag_axis import read_ref, read_ours
BANDS = [("A 16-19.8",16,19.8),("19.8-23.8",19.8,23.8),("C 23.8-24.5",23.8,24.5),
         ("24.5-26.4",24.5,26.4),("D 26.4-29",26.4,29.0),("E 29-31.3",29.0,31.3)]

print("沿轴 |u|（m/s）—— 越高越贴「最大流速」脊")
print(f"{'带':>12} | {'参考轴':>16} | {p1.strip('_'):>16} | {p2.strip('_'):>16}")
acc = {k: [] for k in ("ref", "a", "b")}
for bn, y0, y1 in BANDS:
    v = {"ref": [], "a": [], "b": []}
    for day, tag, gl in trip:
        os.environ["KURO_GLORYS"] = f"E:/nc1/glorys_{gl}.nc"
        import importlib, kuro_diag5
        importlib.reload(kuro_diag5)
        K = kuro_diag5
        rlon, rlat = read_ref(int(day))
        for name, path in (("ref", None), ("a", f"E:/nc1/{p1}{tag}.txt"), ("b", f"E:/nc1/{p2}{tag}.txt")):
            lo, la = (rlon, rlat) if path is None else read_ours(path)
            for x, y in zip(lo, la):
                if not (y0 <= y <= y1): continue
                r = K.sample(x, y)
                if r: v[name].append(math.hypot(*r))
    line = f"{bn:>12} |"
    for k in ("ref", "a", "b"):
        arr = np.array(v[k]) if v[k] else np.array([np.nan])
        line += f"  mean {np.nanmean(arr):5.3f} med {np.nanmedian(arr):5.3f} |"
        acc[k].extend(v[k])
    print(line)
print(f"{'全场':>12} |" + "".join(
    f"  mean {np.nanmean(np.array(acc[k])):5.3f} med {np.nanmedian(np.array(acc[k])):5.3f} |"
    for k in ("ref", "a", "b")))
