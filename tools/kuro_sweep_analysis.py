# -*- coding: utf-8 -*-
"""
kuro_sweep_analysis.py — 分析 kuro_sweep.sh 的参数扫描结果。

用法:
  python -P tools/kuro_sweep_analysis.py <结果目录> [kuroshio_2024.nc]

输出：
  · 每组参数的分带中位（跨天聚合：均值 / 中位 / P90）
  · 逐天总中位矩阵（参数 × 天），并标出**失败案例**
  · 综合评分与推荐参数

距离用向量化点到折线垂距（等距圆柱近似，中纬度误差 <0.1%）。
"""
import sys, os, re, glob
import numpy as np

RES = sys.argv[1] if len(sys.argv) > 1 else "E:/kuro_sweep2"
KURO = sys.argv[2] if len(sys.argv) > 2 else "E:/nc1/kuroshio_2024.nc"

BANDS = [("A 16-19.8", 16, 19.8), ("19.8-23.8", 19.8, 23.8), ("C 23.8-24.5", 23.8, 24.5),
         ("24.5-26.4", 24.5, 26.4), ("D 26.4-29", 26.4, 29.0), ("E 29-31.3", 29.0, 31.3)]


def read_axis(path):
    lon, lat = [], []
    started = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line == "lon,lat":
                started = True; continue
            if not started or not line:
                continue
            p = line.split(",")
            if len(p) != 2:
                continue
            try:
                lon.append(float(p[0])); lat.append(float(p[1]))
            except ValueError:
                pass
    return np.array(lon), np.array(lat)


def xtrack_all(pts, poly):
    """pts (P,2), poly (M,2) -> 每个 pts 点到折线 poly 的垂距（km），向量化"""
    if len(poly) < 2:
        return np.full(len(pts), np.nan)
    a = poly[:-1]; b = poly[1:]                       # (M,2)
    la = np.radians(a[:, 1])
    mx = 111320.0 * np.cos(la)                       # (M,)
    my = 110540.0                                    # 标量（每度纬度的公里数）
    bx = (b[:, 0] - a[:, 0]) * mx; by = (b[:, 1] - a[:, 1]) * my
    L2 = bx * bx + by * by
    L2 = np.where(L2 < 1e-12, 1e-12, L2)
    px = (pts[:, None, 0] - a[None, :, 0]) * mx[None, :]
    py = (pts[:, None, 1] - a[None, :, 1]) * my
    t = (px * bx[None, :] + py * by[None, :]) / L2[None, :]
    np.clip(t, 0.0, 1.0, out=t)
    dx = px - t * bx[None, :]; dy = py - t * by[None, :]
    return np.sqrt(dx * dx + dy * dy).min(axis=1) / 1000.0


# ---- 参考轴 ----
import netCDF4 as nc
ds = nc.Dataset(KURO, "r")
refs = {}
for k in ds.groups:
    m = re.match(r"day(\d+)", k)
    if not m:
        continue
    g = ds.groups[k].groups.get("depth01")
    if g is None:
        continue
    lo = np.array(g.variables["main_axis_lon"][:], float)
    la = np.array(g.variables["main_axis_lat"][:], float)
    ok = np.isfinite(lo) & np.isfinite(la)
    refs[int(m.group(1))] = (lo[ok], la[ok])
ds.close()
print(f"参考轴 {len(refs)} 天")

# ---- 结果 ----
pat = re.compile(r"s(.+)_d(\d+)\.txt$")
groups = {}
for p in sorted(glob.glob(os.path.join(RES, "s*_d*.txt"))):
    m = pat.search(os.path.basename(p))
    if not m:
        continue
    groups.setdefault(m.group(1), {})[int(m.group(2))] = p
# 扫描窗：off=论文评分, 70/110/150/200/250=kuroRidgeScanKm（权重默认 0.5）
# 扫权重：  w01/w02/w03 = w 0.1/0.2/0.3（窗口固定 250）
params = ["off", "70", "110", "150", "200", "250", "w01", "w02", "w03", "w05"]
params = [p for p in params if p in groups] + [p for p in groups if p not in params]
print(f"参数组：{params}   各组天数：{{{', '.join(f'{p}:{len(groups[p])}' for p in params)}}}")

data = {}     # data[(param, day)] = (band medians list, overall median)
for p in params:
    for day, path in groups[p].items():
        if day not in refs:
            continue
        olon, olat = read_axis(path)
        if len(olon) < 3:
            continue
        rlon, rlat = refs[day]
        d = xtrack_all(np.column_stack([rlon, rlat]), np.column_stack([olon, olat]))
        bm = [float(np.median(d[(rlat >= y0) & (rlat <= y1)]))
              if ((rlat >= y0) & (rlat <= y1)).sum() else float("nan")
              for _, y0, y1 in BANDS]
        data[(p, day)] = (bm, float(np.median(d)))

# ---- 分带聚合 ----
print(f"\n{'带':>12} | " + "".join(f"{p:>22}" for p in params))
print(f"{'':>12} | " + "".join(f"{'均值    中位    P90':>22}" for _ in params))
for bi, (bn, _, _) in enumerate(BANDS):
    line = f"{bn:>12} | "
    for p in params:
        v = np.array([data[(p, d)][0][bi] for d in groups[p] if (p, d) in data], float)
        v = v[np.isfinite(v)]
        line += (f"{np.mean(v):7.2f} {np.median(v):7.2f} {np.percentile(v,90):7.2f} " if len(v) else " " * 23)
    print(line)

print(f"\n{'总中位':>12} | " + "".join(f"{p:>22}" for p in params))
line = f"{'':>12} | "
for p in params:
    v = np.array([data[(p, d)][1] for d in groups[p] if (p, d) in data], float)
    v = v[np.isfinite(v)]
    line += f"{np.mean(v):7.2f} {np.median(v):7.2f} {np.percentile(v,90):7.2f} "
print(line)

# ---- 逐天矩阵（总中位）----
days = sorted({d for p in params for d in groups[p]})
print(f"\n=== 逐天总中位（{len(days)} 天）===")
hdr = "day  " + "".join(f"{p:>9}" for p in params) + "   最优"
print(hdr)
worst = {p: [] for p in params}
for d in days:
    row = f"{d:4d}  "
    vals = {}
    for p in params:
        v = data.get((p, d), (None, None))[1]
        vals[p] = v
        row += f"{v:9.2f}" if v is not None else f"{'--':>9}"
    ok = {p: v for p, v in vals.items() if v is not None}
    if ok:
        best = min(ok, key=ok.get)
        row += f"   {best}"
        for p, v in ok.items():
            if v > 20:
                worst[p].append((d, v))
    print(row)

print("\n=== 失败案例（总中位 > 20 km）===")
for p in params:
    print(f"  scan={p:>5}: {len(worst[p]):3d} 天  " +
          (" ".join(f"d{d}:{v:.0f}" for d, v in worst[p][:12]) if worst[p] else "无"))

# ---- 综合评分：均值 + 失败天数惩罚 ----
print("\n=== 综合评分（总中位均值 + >20km 天数）===")
print(f"{'参数':>8} {'均值':>8} {'中位':>8} {'P90':>8} {'>20km':>7} {'>50km':>7} {'最大':>8}")
for p in params:
    v = np.array([data[(p, d)][1] for d in groups[p] if (p, d) in data], float)
    v = v[np.isfinite(v)]
    print(f"{p:>8} {np.mean(v):8.2f} {np.median(v):8.2f} {np.percentile(v,90):8.2f} "
          f"{int((v>20).sum()):7d} {int((v>50).sum()):7d} {v.max():8.2f}")
