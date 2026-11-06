# -*- coding: utf-8 -*-
"""
kuro_plot_axis.py — 画 GLORYS 流速 + 两条轴线，看中间段到底怎么分岔

    PYTHONIOENCODING=utf-8 python -P tools/kuro_plot_axis.py [day] [glorys.nc] [ours.txt] [out.png]
"""
import sys, math, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kuro_diag_axis import read_ref, read_ours, read_uv, point_seg_d

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
ROOT = "E:/nc1"
GLORYS = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else f"{ROOT}/glorys_20240101.nc"
OURS = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else f"{ROOT}/kuro_ours_20240101.txt"
OUT = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else f"{ROOT}/kuro_diag_map.png"

rlon, rlat = read_ref(DAY)
olon, olat = read_ours(OURS)
glat, glon, U, V = read_uv(GLORYS)
SP = np.hypot(U, V)

def ridge_lon(la, lo0, lo1):
    """在 [lo0,lo1] 内沿该纬度找速度最大的经度（0.02° 步长）"""
    best, bl = -1.0, np.nan
    for lo in np.arange(lo0, lo1, 0.02):
        from kuro_diag_axis import bilin
        s = bilin(glat, glon, SP, la, lo)
        if np.isfinite(s) and s > best:
            best, bl = s, lo
    return bl, best

fig, axes = plt.subplots(1, 3, figsize=(21, 8))
wins = [(118, 131, 15.5, 32.0, "full 16-32N"),
        (119.5, 126.5, 19.0, 24.0, "Luzon Strait (eye 1)"),
        (121.0, 128.0, 24.0, 28.0, "NE of Taiwan (eye 2)")]

for ax, (x0, x1, y0, y1, title) in zip(axes, wins):
    m = (glon >= x0-1) & (glon <= x1+1)
    n = (glat >= y0-1) & (glat <= y1+1)
    ext = [glon[m][0], glon[m][-1], glat[n][0], glat[n][-1]]
    im = ax.imshow(SP[np.ix_(n, m)], origin="lower", extent=ext, aspect="auto",
                   cmap="turbo", vmin=0, vmax=1.6)
    # 等流速线 0.3/0.6/0.9
    Lon, Lat = np.meshgrid(glon[m], glat[n])
    cs = ax.contour(Lon, Lat, SP[np.ix_(n, m)], levels=[0.3, 0.6, 0.9, 1.2],
                    colors="k", linewidths=0.5, alpha=0.45)
    ax.plot(rlon, rlat, "w-", lw=2.2, label="ref (Wang2026 dataset)")
    ax.plot(rlon, rlat, "k-", lw=1.0)
    ax.plot(olon, olat, "m--", lw=2.2, label="ours")
    # 每 10 个点标个数，看参数化/折返
    ax.plot(rlon[::10], rlat[::10], "ko", ms=3)
    ax.plot(olon[::10], olat[::10], "m^", ms=3)
    ax.set_xlim(x0, x1); ax.set_ylim(y0, y1)
    ax.set_title(f"{title}\n|u| GLORYS day{DAY:03d}")
    ax.set_xlabel("lon"); ax.set_ylabel("lat")
    ax.legend(loc="lower right", fontsize=8)
    ax.grid(alpha=0.3)

plt.tight_layout()
plt.savefig(OUT, dpi=110)
print("saved", OUT)

# ---- 纬向剖面：ridge 在哪，ref/our 在哪
print("\nlat    ref_lon  our_lon   ridge_lon  |u|ref |u|our |u|rid")
for la in np.arange(19.0, 27.6, 0.25):
    from kuro_diag_axis import bilin
    # 取最近的 ref / our 点（按纬度）
    ri = int(np.argmin(np.abs(rlat - la))); oi = int(np.argmin(np.abs(olat - la)))
    if abs(rlat[ri] - la) > 0.2: continue
    rl, ol_ = rlon[ri], olon[oi]
    lo0, lo1 = min(rl, ol_) - 1.5, max(rl, ol_) + 1.5
    bl, bs = ridge_lon(la, lo0, lo1)
    sr = bilin(glat, glon, SP, la, rl)
    so = bilin(glat, glon, SP, la, ol_)
    print(f"{la:5.2f}  {rl:8.3f} {ol_:8.3f}   {bl:8.3f}  {sr:6.3f} {so:6.3f} {bs:6.3f}")
