# -*- coding: utf-8 -*-
"""
kuro_diag2.py — 第二轮判别：
  1) 参考轴是不是等弧长重采样到 180 点
  2) 两条轴分别在哪穿过 11 条断面（判断差异属于哪一段）
  3) 两条轴的切向 vs 局部流向夹角（谁是流线、谁横切）
  4) 24–27°N 的横向速度剖面（看"拐角"是流场里的真实结构还是算法产物）
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, read_uv, bilin, hav

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
ROOT = "E:/nc1"
GLORYS = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else f"{ROOT}/glorys_20240101.nc"
OURS = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else f"{ROOT}/kuro_ours_20240101.txt"

rlon, rlat = read_ref(DAY)
olon, olat = read_ours(OURS)
glat, glon, U, V = read_uv(GLORYS)
SP = np.hypot(U, V)

# ---------------------------------------------------------------- 1) 弧长间距
def spacing(lon, lat):
    d = [hav((lon[i], lat[i]), (lon[i+1], lat[i+1])) for i in range(len(lon)-1)]
    return np.array(d)

for name, (lo, la) in [("ref", (rlon, rlat)), ("ours", (olon, olat))]:
    d = spacing(lo, la)
    print(f"[{name}] n={len(lo)} 弧长间距 km: min={d.min():.2f} 中位={np.median(d):.2f} "
          f"max={d.max():.2f}  std/mean={d.std()/d.mean():.4f}  总长={d.sum():.1f}")

# ---------------------------------------------------------------- 2) 断面穿越
SECTIONS = [
    ("A1",    122.00, 16.000, 124.00, 16.000),
    ("A3",    122.22, 17.500, 123.50, 17.500),
    ("A2/B1", 120.85, 19.000, 123.00, 19.000),
    ("B3",    118.00, 20.500, 123.00, 20.500),
    ("B2/C1", 120.85, 22.000, 123.00, 22.000),
    ("C3",    121.49, 23.500, 123.50, 23.500),
    ("C2/D1", 122.00, 25.000, 124.00, 25.000),
    ("D3",    125.00, 27.375, 126.85, 26.145),
    ("D2/E1", 126.00, 29.000, 127.85, 27.770),
    ("E3",    127.00, 30.625, 128.85, 29.395),
    ("E2",    130.38, 28.250, 130.38, 31.250),
]

def crossings(lon, lat, sec):
    """返回折线与线段 sec 的交点列表（参数 u 沿 sec 0..1）"""
    name, x1, y1, x2, y2 = sec
    out = []
    for i in range(len(lon)-1):
        ax, ay = lon[i], lat[i]; bx, by = lon[i+1], lat[i+1]
        # 解 ax + t(bx-ax) = x1 + u(x2-x1) ; ay + t(by-ay) = y1 + u(y2-y1)
        d = (bx-ax)*(y2-y1) - (by-ay)*(x2-x1)
        if abs(d) < 1e-12: continue
        t = ((x1-ax)*(y2-y1) - (y1-ay)*(x2-x1)) / d
        u = ((x1-ax)*(by-ay) - (y1-ay)*(bx-ax)) / d
        if -1e-9 <= t <= 1+1e-9 and -1e-9 <= u <= 1+1e-9:
            out.append((ax + t*(bx-ax), ay + t*(by-ay), u, i))
    return out

print("\n[断面穿越]  断面      参考轴交点                我们的交点")
for sec in SECTIONS:
    a = crossings(rlon, rlat, sec)
    b = crossings(olon, olat, sec)
    fa = "  ".join(f"({x:.3f},{y:.3f})u={u:.2f}" for x, y, u, _ in a) or "—"
    fb = "  ".join(f"({x:.3f},{y:.3f})u={u:.2f}" for x, y, u, _ in b) or "—"
    print(f"  {sec[0]:7s}  {fa:<46s} {fb}")

# ---------------------------------------------------------------- 3) 夹角
def ang_vs_flow(lon, lat, tag):
    print(f"\n[{tag} 切向 vs 流向]   i    lat     lon    夹角°   |u|   沿程(km)")
    s = 0.0
    rows = []
    for i in range(len(lat)):
        j, k = max(0, i-1), min(len(lat)-1, i+1)
        if i > 0:
            s += hav((lon[i-1], lat[i-1]), (lon[i], lat[i]))
        tx, ty = lon[k]-lon[j], lat[k]-lat[j]
        n = math.hypot(tx, ty) + 1e-18
        tx, ty = tx/n, ty/n
        u = bilin(glat, glon, U, lat[i], lon[i])
        v = bilin(glat, glon, V, lat[i], lon[i])
        sp = math.hypot(u, v) if (np.isfinite(u) and np.isfinite(v)) else np.nan
        if not np.isfinite(sp) or sp < 1e-3:
            rows.append((lat[i], lon[i], np.nan, sp, s)); continue
        fx, fy = u/sp, v/sp
        dot = max(-1.0, min(1.0, tx*fx + ty*fy))
        ang = math.degrees(math.acos(dot))
        rows.append((lat[i], lon[i], ang, sp, s))
    for i in range(0, len(rows), 6):
        la, lo, ang, sp, s = rows[i]
        a = f"{ang:7.1f}" if np.isfinite(ang) else "    nan"
        print(f"  {i:4d} {la:7.3f} {lo:8.3f} {a} {sp:6.3f} {s:8.1f}")
    A = np.array([[r[2], r[3]] for r in rows], dtype=float)
    ok = np.isfinite(A[:, 0])
    print(f"   有效 {ok.sum()}/{len(rows)}  夹角中位 {np.nanmedian(A[ok,0]):.2f}°  "
          f"均值 {np.nanmean(A[ok,0]):.2f}°   |u| 中位 {np.nanmedian(A[ok,1]):.3f}")
    return rows

ra = ang_vs_flow(rlon, rlat, "参考轴")
oa = ang_vs_flow(olon, olat, "我们的轴")

# 眼睛区分别统计
def band(rows, y0, y1):
    sel = [r for r in rows if y0 <= r[0] <= y1]
    a = np.array([r[2] for r in sel], dtype=float)
    s = np.array([r[3] for r in sel], dtype=float)
    return (np.nanmedian(a) if np.isfinite(a).any() else np.nan,
            np.nanmedian(s) if np.isfinite(s).any() else np.nan, len(sel))

print("\n[分带夹角/流速中位]   带            参考夹角 参考|u|  n | 我们夹角 我们|u|  n")
for tag, y0, y1 in [("A 16-19", 16, 19), ("eye1 20-23", 20, 23), ("C 23-24.5", 23, 24.5),
                    ("eye2 24.5-26.4", 24.5, 26.4), ("D 26.5-29", 26.5, 29), ("E 29-31", 29, 31)]:
    ar, sr, nr = band(ra, y0, y1); ao, so, no = band(oa, y0, y1)
    print(f"   {tag:15s} {ar:8.1f} {sr:8.3f} {nr:3d} | {ao:8.1f} {so:8.3f} {no:3d}")

# ---------------------------------------------------------------- 4) 横向剖面
print("\n[横向速度剖面 |u|(lon)]  行 = 纬度；列 = 经度 121.0..127.0 step 0.25")
lons = np.arange(121.0, 127.01, 0.25)
hdr = "  lat  " + "".join(f"{l:6.2f}" for l in lons) + "    ref    our"
print(hdr)
for la in np.arange(24.0, 27.01, 0.125):
    ri = int(np.argmin(np.abs(rlat - la))); oi = int(np.argmin(np.abs(olat - la)))
    line = f"{la:6.2f}  "
    for lo in lons:
        s = bilin(glat, glon, SP, la, lo)
        line += f"{s:6.2f}" if np.isfinite(s) else "   ---"
    line += f"  {rlon[ri]:7.3f} {olon[oi]:7.3f}"
    print(line)
