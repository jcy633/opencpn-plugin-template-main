# -*- coding: utf-8 -*-
"""
kuro_diag9.py — 步长语义扫描：MATLAB `stream2(LonGrid,LatGrid,u,v,xp,yp,1)` 的
`stepsize=1` 到底是「1 个网格距(0.0833°)」还是「1 个数据单位(1°)」？

判据不是文档，是**结果**：把同一条流线用不同步长积分，看它能不能跨过 25–26.5°N 那个
分离线、能不能到达 D3/D2/E1 断面、离参考轴多远。

用法: PYTHONIOENCODING=utf-8 python -P tools/kuro_diag9.py [day] [glorys.nc]
"""
import sys, os, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ref, read_ours

DAY = K.DAY
rlon, rlat = read_ref(DAY)
SECTIONS = K.SECTIONS; SEGD = K.SEGD

def integrate_step(lo0, la0, sign, stepdeg, maxpts=400):
    """与 integrateOneWay 同构，但步长 = stepdeg（度，数据单位）"""
    out = [(lo0, la0)]
    lo, la = lo0, la0
    for _ in range(1, maxpts):
        r = K.sample(lo, la)
        if r is None: break
        u1, v1 = r
        sp = math.hypot(u1, v1)
        if sp < 1e-9: break
        h = stepdeg / sp
        mlo = lo + sign*u1*h*0.5; mla = la + sign*v1*h*0.5
        r2 = K.sample(mlo, mla)
        u2, v2 = (u1, v1) if r2 is None else r2
        if r2 is not None and math.hypot(u2, v2) < 1e-9: break
        nlo = lo + sign*u2*h; nla = la + sign*v2*h
        if not (K.TB[0] <= nlo <= K.TB[1] and K.TB[2] <= nla <= K.TB[3]): break
        if not K.isSea(nlo, nla): break
        lo, la = nlo, nla
        out.append((lo, la))
    return out

def xtrack(p, poly):
    best = 1e18
    for i in range(len(poly)-1):
        a, b = poly[i], poly[i+1]
        la = math.radians(a[1])
        mx = 111320.0*math.cos(la); my = 110540.0
        px, py = (p[0]-a[0])*mx, (p[1]-a[1])*my
        bx, by = (b[0]-a[0])*mx, (b[1]-a[1])*my
        L2 = bx*bx+by*by
        t = 0.0 if L2 < 1e-12 else max(0.0, min(1.0, (px*bx+py*by)/L2))
        d = math.hypot(px-t*bx, py-t*by)/1000.0
        if d < best: best = d
    return best

rpoly = list(zip(rlon, rlat))
r0 = (float(rlon[int(np.argmin(np.abs(rlat-25.0)))]), float(rlat[int(np.argmin(np.abs(rlat-25.0)))]))
print(f"day{DAY:03d}   参考轴 25°N 交点 = ({r0[0]:.3f},{r0[1]:.3f})")
print(f"参考轴在 24.9–29.2°N 的 avgSpeed = 0.68（day001）/0.70（day366）")

print(f"\n{'步长(°)':>8} {'≈km':>6} {'正向终点':>20} {'反向终点':>20} "
      f"{'D3':>4} {'D2/E1':>6} {'段D候选':>8} {'流线->参考轴垂距 km':>22}")
print(f"{'':>8} {'':>6} {'':>20} {'':>20} {'':>4} {'':>6} {'':>8} {'  中位 / P90 / max':>22}")
for stepdeg in [0.0833, 0.125, 0.25, 0.5, 1.0]:
    km = stepdeg * 110.54
    fwd = integrate_step(*r0, +1.0, stepdeg)
    bwd = integrate_step(*r0, -1.0, stepdeg)
    full = list(reversed(bwd)) + fwd[1:]
    hits = [K.on_section(full, s) for s in SECTIONS]
    cs, st = K.collect(full, hits[SEGD['iS']], hits[SEGD['iN']], True, True)
    d = np.array([xtrack(p, rpoly) for p in full]) if full else np.array([np.nan])
    # 只看 24.9–29.2 那段
    sel = [xtrack(p, rpoly) for p in full if 24.5 <= p[1] <= 29.5]
    sel = np.array(sel) if sel else np.array([np.nan])
    print(f"{stepdeg:8.4f} {km:6.1f} ({fwd[-1][0]:8.3f},{fwd[-1][1]:6.3f}) "
          f"({bwd[-1][0]:8.3f},{bwd[-1][1]:6.3f}) "
          f"{'Y' if hits[SEGD['iM']] else '-':>4} {'Y' if hits[SEGD['iN']] else '-':>6} "
          f"{len(cs):8d}    {np.nanmedian(sel):6.1f} / {np.nanpercentile(sel,90):5.1f} / {np.nanmax(sel):5.1f}"
          f"   n={len(full)}")

print("\n[同一流线在 25–27.5°N 的落点]  （看是否跨过分离线）")
for stepdeg in [0.0833, 0.5, 1.0]:
    fwd = integrate_step(*r0, +1.0, stepdeg)
    pts = [p for p in fwd if 24.5 <= p[1] <= 28.0]
    print(f"  步长{stepdeg:6.4f}: " + " ".join(f"({p[0]:.2f},{p[1]:.2f})" for p in pts[:14]))
