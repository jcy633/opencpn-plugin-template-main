# -*- coding: utf-8 -*-
"""
kuro_diag5.py — 离线复现 Wang2026 的「段 D 候选生成 + 选优」，
回答一个决定性问题：**沿着速度脊绕过 25–26.4°N 那个弯的候选，到底进没进候选池？**

逐行对齐 src/ncdf_kuroshio.cpp：
  integrateOneWay  : RK2 中点法，步长 = 1 网格距（(u,v) 当 (dlon,dlat) 速率，不除 cos(lat)）
  onSection        : 投影到线段 + dist <= 0.5
  validate         : 盒约束(D: lat 25-29, lon 122-127.85) + detectLoops(8, 0.15)
  avgSpeed         : mean(hypot(u,v))，无效点跳过（kuroAvgZero=0 口径）
  pickBest         : score = 0.5*dN + 0.5*(1-sN)，dN 用「度」为单位的欧氏距离

用法: PYTHONIOENCODING=utf-8 python -P tools/kuro_diag5.py [day] [glorys.nc]
"""
import sys, os, math
import numpy as np

def _argv(i, d):
    try:
        v = sys.argv[i]
        return d if v == '' else v
    except IndexError:
        return d
_a1 = _argv(1, '1')
DAY = int(_a1) if str(_a1).lstrip('-').isdigit() else 1
ROOT = "E:/nc1"
# 只认 .nc 结尾的 argv[2]（本模块常被 import，argv 可能是别的脚本的）；也可用 KURO_GLORYS 显式指定
_g = os.environ.get("KURO_GLORYS") or _argv(2, "")
GLORYS = _g if _g.lower().endswith(".nc") else f"{ROOT}/glorys_20240101.nc"
KURO = f"{ROOT}/kuroshio_2024.nc"

import netCDF4 as nc
ds = nc.Dataset(GLORYS, "r")
glat = np.array(ds.variables[[n for n in ("latitude", "lat") if n in ds.variables][0]][:], float)
glon = np.array(ds.variables[[n for n in ("longitude", "lon") if n in ds.variables][0]][:], float)
def rd(name):
    v = ds.variables[name]
    a = np.ma.filled(v[:].astype(float), np.nan)
    while a.ndim > 2:
        a = a[0]
    return a
U = rd("uo"); V = rd("vo")
ds.close()
CELL = float(np.abs(glon[1] - glon[0]))
print(f"grid {len(glat)}x{len(glon)}  cell={CELL:.6f} deg  lat {glat[0]}..{glat[-1]}  lon {glon[0]}..{glon[-1]}")

def sample(lo, la):
    """UVField::sample 语义：2x2 双线性，任一角无效即失败"""
    if la < glat[0] or la > glat[-1] or lo < glon[0] or lo > glon[-1]:
        return None
    dj = int(np.searchsorted(glat, la)) - 1
    di = int(np.searchsorted(glon, lo)) - 1
    dj = max(0, min(dj, len(glat)-2)); di = max(0, min(di, len(glon)-2))
    t = (la - glat[dj]) / (glat[dj+1]-glat[dj])
    s = (lo - glon[di]) / (glon[di+1]-glon[di])
    corners = [U[dj, di], U[dj, di+1], U[dj+1, di], U[dj+1, di+1],
               V[dj, di], V[dj, di+1], V[dj+1, di], V[dj+1, di+1]]
    if any(not np.isfinite(c) for c in corners): return None
    u = (1-t)*(1-s)*U[dj,di] + (1-t)*s*U[dj,di+1] + t*(1-s)*U[dj+1,di] + t*s*U[dj+1,di+1]
    v = (1-t)*(1-s)*V[dj,di] + (1-t)*s*V[dj,di+1] + t*(1-s)*V[dj+1,di] + t*s*V[dj+1,di+1]
    return float(u), float(v)

def isSea(lo, la):
    return sample(lo, la) is not None

TB = (114.0, 134.38, 12.0, 35.25)

def integrate(lo0, la0, sign, maxpts=400):
    out = [(lo0, la0)]
    lo, la = lo0, la0
    for _ in range(1, maxpts):
        r = sample(lo, la)
        if r is None: break
        u1, v1 = r
        sp = math.hypot(u1, v1)
        if sp < 1e-9: break
        h = CELL / sp
        mlo = lo + sign*u1*h*0.5; mla = la + sign*v1*h*0.5
        r2 = sample(mlo, mla)
        u2, v2 = (u1, v1) if r2 is None else r2
        if r2 is not None and math.hypot(u2, v2) < 1e-9: break
        nlo = lo + sign*u2*h; nla = la + sign*v2*h
        if not (TB[0] <= nlo <= TB[1] and TB[2] <= nla <= TB[3]): break
        if not isSea(nlo, nla): break
        lo, la = nlo, nla
        out.append((lo, la))
    return out

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
TOL = 0.5

def on_section(xy, sec):
    _, x1, y1, x2, y2 = sec
    vx, vy = x2-x1, y2-y1
    vv = vx*vx + vy*vy
    out = []
    for i, (lo, la) in enumerate(xy):
        wx, wy = lo-x1, la-y1
        b = (wx*vx + wy*vy)/vv
        b = max(0.0, min(1.0, b))
        dx = lo - (x1 + b*vx); dy = la - (y1 + b*vy)
        if math.hypot(dx, dy) <= TOL: out.append(i)
    return out

def detect_loops(xy, minpts=8, thr=0.15):
    n = len(xy)
    if n < minpts*2: return False
    d2 = thr*thr
    for i in range(n-minpts):
        for j in range(i+minpts, n):
            dx = xy[i][0]-xy[j][0]; dy = xy[i][1]-xy[j][1]
            if dx*dx + dy*dy < d2: return True
    return False

SEGD = dict(name='D', lat=(25.0, 29.0), lon=(122.0, 127.85), iS=6, iM=7, iN=8)

def collect(xy, iA, iB, backward, breakafter, seg=SEGD):
    """返回 (候选列表, 拒绝统计)"""
    cands = []; st = dict(seeds=0, noB=0, box=0, loop=0, added=0)
    if not iA or not iB: return cands, st
    nA = len(iA)
    for ii in range(nA):
        s = iA[nA-1-ii] if backward else iA[ii]
        e = -1
        for k in iB:
            if k > s: e = k; break
        st['seeds'] += 1
        if e < 0: st['noB'] += 1; continue
        c = xy[s:e+1]
        ok = all(seg['lon'][0] <= lo <= seg['lon'][1] and seg['lat'][0] <= la <= seg['lat'][1]
                 for lo, la in c)
        if not ok: st['box'] += 1; continue
        if detect_loops(c): st['loop'] += 1; continue
        sumsp = 0.0; nvalid = 0
        for lo, la in c:
            r = sample(lo, la)
            if r is not None:
                sumsp += math.hypot(*r); nvalid += 1
        avg = sumsp/nvalid if nvalid else 0.0
        cands.append(dict(xy=c, sp=avg, s=s, e=e))
        st['added'] += 1
        if breakafter: break
    return cands, st

def pick_best(cands, prev):
    if not cands: return None
    dist = [math.hypot(c['xy'][0][0]-prev[0], c['xy'][0][1]-prev[1]) for c in cands]
    sp = [c['sp'] for c in cands]
    dmin, dmax = min(dist), max(dist)
    smin, smax = min(sp), max(sp)
    dSpan = max(dmax-dmin, 1e-12); sSpan = max(smax-smin, 1e-12)
    best, bs = None, 1e300
    for i, c in enumerate(cands):
        dN = (dist[i]-dmin)/dSpan if len(cands) > 1 else 0.0
        sN = (sp[i]-smin)/sSpan if len(cands) > 1 else 1.0
        sc = 0.5*dN + 0.5*(1.0-sN)
        if sc < bs: bs, best = sc, c
    return best, bs, dmin, dmax, smin, smax

def main():
    # ---------------------------------------------------------------- 主实验
    # 种子撒在 C2/D1 断面上（25°N，122–124），网格距 CELL —— 流线唯一性保证
    # 这能覆盖「所有穿过 C2/D1 的流线」，等价于全域撒种子后再按段 D 收候选。
    print(f"\n=== 段 D 候选生成（种子撒在 C2/D1 = 25°N, 122-124°E）===")
    seeds = [(122.0 + k*CELL, 25.0) for k in range(int(2.0/CELL)+1)]
    print(f"种子 {len(seeds)} 个")

    allc = []
    st_tot = dict(seeds=0, noB=0, box=0, loop=0, added=0)
    for (sl, sla) in seeds:
        fwd = integrate(sl, sla, +1.0)
        bwd = integrate(sl, sla, -1.0)
        if len(fwd)+len(bwd) < 4: continue
        full = list(reversed(bwd)) + fwd[1:]
        hits = [on_section(full, s) for s in SECTIONS]
        for tag, iA, iB, back, brk in [("main+", SEGD['iS'], SEGD['iN'], True, True),
                                       ("south->mid", SEGD['iS'], SEGD['iM'], False, False),
                                       ("mid->north", SEGD['iM'], SEGD['iN'], False, False)]:
            cs, st = collect(full, hits[iA], hits[iB], back, brk)
            for c in cs: c['tag'] = tag; c['seed'] = (sl, sla)
            allc.extend(cs)
            for k in st_tot: st_tot[k] += st[k]

    print(f"拒绝统计: {st_tot}")
    print(f"候选总数 {len(allc)}  （main+ 只取每条流线 1 条，south->mid / mid->north 是降级拼接用）")

    # 只看主路径（决定轴线的是它）
    main = [c for c in allc if c['tag'] == 'main+']
    print(f"\n--- 主路径候选 main+ : {len(main)} 条 ---")
    print("起(25°N 交点)      终(D2/E1 交点)     avgSpeed  点数   起点经度")
    for c in sorted(main, key=lambda c: -c['sp']):
        s0 = c['xy'][0]; s1 = c['xy'][-1]
        print(f"  ({s0[0]:7.3f},{s0[1]:6.3f})  ({s1[0]:7.3f},{s1[1]:6.3f})  "
              f"{c['sp']:7.3f}  {len(c['xy']):4d}   seed=({c['seed'][0]:.3f},{c['seed'][1]:.2f})")

    if main:
        prev = (122.653, 25.0)   # 我们实际的 C 段终点（C2/D1 交点）
        r = pick_best(main, prev)
        best, bs, dmin, dmax, smin, smax = r
        print(f"\n--- pickBest (prev_end={prev}) ---")
        print(f"  dSpan(deg) {dmin:.4f}..{dmax:.4f}   sSpan {smin:.3f}..{smax:.3f}")
        b0, b1 = best['xy'][0], best['xy'][-1]
        print(f"  赢家: ({b0[0]:.3f},{b0[1]:.3f}) -> ({b1[0]:.3f},{b1[1]:.3f})  "
              f"sp={best['sp']:.3f}  score={bs:.3f}")
        print(f"  赢家轨迹（每 10 点）: " +
              " ".join(f"({p[0]:.2f},{p[1]:.2f})" for p in best['xy'][::10]))

    # 对照：参考轴在段 D 的实际路径
    ds2 = nc.Dataset(KURO, "r")
    g = ds2.groups[f"day{DAY:03d}"].groups["depth01"]
    rlon = np.array(g.variables["main_axis_lon"][:], float)
    rlat = np.array(g.variables["main_axis_lat"][:], float)
    ds2.close()
    seg_ref = [(lo, la) for lo, la in zip(rlon, rlat) if 24.9 <= la <= 29.2]
    print(f"\n--- 参考轴在 24.9–29.2°N 的实际路径（{len(seg_ref)} 点）---")
    print("  " + " ".join(f"({lo:.2f},{la:.2f})" for lo, la in seg_ref[::4]))
    sp_ref = []
    for lo, la in seg_ref:
        r = sample(lo, la)
        if r: sp_ref.append(math.hypot(*r))
    print(f"  参考路径 avgSpeed = {np.mean(sp_ref):.3f}  (n={len(sp_ref)})")



if __name__ == "__main__":
    main()
