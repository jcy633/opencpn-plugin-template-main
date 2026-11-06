# -*- coding: utf-8 -*-
"""kuro_plot_mech.py — 机制图：25–27°N 的弯道里，参考轴走的是速度脊，
而穿过参考轴 25°N 交点的流线根本到不了 D3/D2/E1 断面。
用法: PYTHONIOENCODING=utf-8 python -P tools/kuro_plot_mech.py [day] [glorys.nc] [ours.txt] [out.png]
"""
import sys, os, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ours, read_ref

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
GLORYS = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else "E:/nc1/glorys_20240101.nc"
OURS = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else "E:/nc1/kuro_ours_20240101.txt"
OUT = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else "E:/nc1/kuro_mech.png"

rlon, rlat = read_ref(DAY)
olon, olat = read_ours(OURS)

def nearest(lon, lat, la):
    i = int(np.argmin(np.abs(lat - la)))
    return float(lon[i]), float(lat[i])

r0 = nearest(rlon, rlat, 25.0); o0 = nearest(olon, olat, 25.0)

def streamline(p0):
    fwd = K.integrate(p0[0], p0[1], +1.0)
    bwd = K.integrate(p0[0], p0[1], -1.0)
    return list(reversed(bwd)) + fwd[1:]

tr_ref = streamline(r0); tr_our = streamline(o0)

# 速度场
glon, glat = K.glon, K.glat
SP = np.full_like(K.U, np.nan)
ok = np.isfinite(K.U) & np.isfinite(K.V)
SP[ok] = np.hypot(K.U[ok], K.V[ok])

fig, ax = plt.subplots(figsize=(13, 10))
x0, x1, y0, y1 = 120.5, 128.5, 24.0, 29.5
m = (glon >= x0-1) & (glon <= x1+1); n = (glat >= y0-1) & (glat <= y1+1)
ext = [glon[m][0], glon[m][-1], glat[n][0], glat[n][-1]]
ax.imshow(SP[np.ix_(n, m)], origin="lower", extent=ext, aspect="auto",
          cmap="turbo", vmin=0, vmax=1.2, alpha=0.85)
Lon, Lat = np.meshgrid(glon[m], glat[n])
ax.contour(Lon, Lat, SP[np.ix_(n, m)], levels=[0.3, 0.5, 0.7, 0.9],
           colors="k", linewidths=0.5, alpha=0.5)

# 断面
for name, a, b, c, d in K.SECTIONS:
    if name in ("C2/D1", "D3", "D2/E1", "C3"):
        ax.plot([a, c], [b, d], "w--", lw=2.0, alpha=0.9)
        ax.text((a+c)/2, (b+d)/2, name, color="w", fontsize=11, weight="bold",
                ha="center", va="center",
                bbox=dict(fc="k", alpha=0.55, pad=1.5, edgecolor="none"))

ax.plot(rlon, rlat, "-", color="#00e5ff", lw=3.2, label=f"ref axis (dataset)  avg|u|={np.mean([math.hypot(*K.sample(lo,la)) for lo,la in zip(rlon,rlat) if K.sample(lo,la) and 24.9<la<29.2]):.2f}")
ax.plot(olon, olat, "-", color="#ff4081", lw=3.2, label="our axis")
ax.plot([p[0] for p in tr_ref], [p[1] for p in tr_ref], "-", color="lime", lw=2.2,
        label=f"streamline thru ref 25N crossing {r0[0]:.2f}E")
ax.plot([p[0] for p in tr_our], [p[1] for p in tr_our], "-", color="orange", lw=2.2,
        label=f"streamline thru our 25N crossing {o0[0]:.2f}E")
ax.plot(*r0, "o", color="lime", ms=11, mec="k", mew=1.5)
ax.plot(*o0, "o", color="orange", ms=11, mec="k", mew=1.5)
# 流线终点
for tr, c in ((tr_ref, "lime"), (tr_our, "orange")):
    ax.plot(tr[-1][0], tr[-1][1], "X", color=c, ms=13, mec="k", mew=1.5)

ax.set_xlim(x0, x1); ax.set_ylim(y0, y1)
ax.set_xlabel("lon"); ax.set_ylabel("lat")
ax.grid(alpha=0.25, color="w")
ax.legend(loc="upper left", fontsize=10, facecolor="k", labelcolor="w", framealpha=0.7)
ax.set_title(f"day{DAY:03d}  |u| (GLORYS) — the 24.5-26.4N corner\n"
             "cyan = published axis (follows the speed ridge);  pink = ours (cuts the corner)\n"
             "green/orange = streamlines through each axis' 25N crossing",
             fontsize=11)
plt.tight_layout()
plt.savefig(OUT, dpi=115)
print("saved", OUT)
print(f"ref 25N crossing {r0}  our 25N crossing {o0}")
print(f"ref-streamline  ends at ({tr_ref[-1][0]:.2f},{tr_ref[-1][1]:.2f})  n={len(tr_ref)}")
print(f"our-streamline  ends at ({tr_our[-1][0]:.2f},{tr_our[-1][1]:.2f})  n={len(tr_our)}")
