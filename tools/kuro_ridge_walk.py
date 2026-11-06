# -*- coding: utf-8 -*-
"""
kuro_ridge_walk.py — 从零生成「纯速度脊线」，看它会不会被涡劫走。

走法：从起点开始，反复「垂直于流向做断面 -> 取沿流投影极大 -> 移过去 -> 沿脊方向进 5 km」。
不使用任何流线骨架 —— 正是「光用速度脊线」的做法。

用法: python -P tools/kuro_ridge_walk.py <day> <glorys.nc> [起点纬度]
"""
import sys, os, math
import numpy as np

GDIR = os.environ.get("KURO_GLORYS_DIR", "E:/nc1")
os.environ.setdefault("KURO_GLORYS", GDIR + "/glorys_20240101.nc")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ref

DAY = int(sys.argv[1]) if len(sys.argv) > 1 else 1
if len(sys.argv) > 2 and sys.argv[2]:
    os.environ["KURO_GLORYS"] = sys.argv[2]
    import importlib, kuro_diag5
    importlib.reload(kuro_diag5); K = kuro_diag5
LA0 = float(sys.argv[3]) if len(sys.argv) > 3 else 17.0

rlon, rlat = read_ref(DAY)
ref = list(zip(rlon, rlat))

kM = 110.54; kMLon = 111.32
HALF = 70.0          # 论文吸附半宽
NSEC = 71            # 2 km 步长
STEP = 5.0           # 沿脊前进的步长 km

def snap_ridge(lo, la):
    """返回 (脊点, 该点沿流速度, 断面上速度剖面的形态)"""
    r = K.sample(lo, la)
    if r is None or (r[0] == 0.0 and r[1] == 0.0):
        return None
    th = math.atan2(r[1], r[0])
    cLat = max(math.cos(math.radians(la)), 1e-6)
    dLon = (HALF / (kMLon * cLat)) * math.cos(th + math.pi/2)
    dLat = (HALF / kM) * math.sin(th + math.pi/2)
    vals, pts = [], []
    for k in range(NSEC):
        t = k/(NSEC-1)
        p = (lo - dLon) + 2*dLon*t
        q = (la - dLat) + 2*dLat*t
        rr = K.sample(p, q)
        vals.append((rr[0]*math.cos(th) + rr[1]*math.sin(th)) if rr else -1e300)
        pts.append((p, q))
    vals = np.array(vals)
    i = int(np.argmax(vals))
    # 剖面里有几个"像样的"极大？（涡/多支会让剖面多峰）
    pk = 0
    for k in range(1, len(vals)-1):
        if vals[k] > vals[k-1] and vals[k] > vals[k+1] and vals[k] > 0.15*vals[i]:
            pk += 1
    return pts[i], float(vals[i]), pk

def walk(lo0, la0, nmax=600):
    out = [(lo0, la0)]
    lo, la = lo0, la0
    for _ in range(nmax):
        s = snap_ridge(lo, la)
        if s is None: break
        (bl, bt), v, pk = s
        lo, la = bl, bt
        out.append((lo, la))
        # 沿脊前进：取该点流向转 90° 之一为"脊方向"，朝纬度增大的一侧走
        r = K.sample(lo, la)
        if r is None: break
        # 沿**流向**前进（脊的切向≈流向；垂直方向是横断面，走错了会原地绕）
        th = math.atan2(r[1], r[0])
        cLat = max(math.cos(math.radians(la)), 1e-6)
        for sgn in (+1, -1):
            nlo = lo + sgn*(STEP/(kMLon*cLat))*math.cos(th)
            nla = la + sgn*(STEP/kM)*math.sin(th)
            if nla > la:
                lo, la = nlo, nla
                break
        else:
            break
        if la > 31.5: break
    return out

def xtrack(p, poly):
    best = 1e18
    for i in range(len(poly)-1):
        a, b = poly[i], poly[i+1]
        mla = math.radians(a[1]); mx = 111320.0*math.cos(mla); my = 110540.0
        px, py = (p[0]-a[0])*mx, (p[1]-a[1])*my
        bx, by = (b[0]-a[0])*mx, (b[1]-a[1])*my
        L2 = bx*bx+by*by
        t = 0.0 if L2 < 1e-12 else max(0.0, min(1.0, (px*bx+py*by)/L2))
        d = math.hypot(px-t*bx, py-t*by)/1000.0
        if d < best: best = d
    return best

i0 = int(np.argmin(np.abs(rlat - LA0)))
print(f"day{DAY:03d}  起点 = 参考轴 ({rlon[i0]:.3f},{rlat[i0]:.3f})")
curve = walk(rlon[i0], rlat[i0])
print(f"纯脊线走了 {len(curve)} 点，终点 ({curve[-1][0]:.2f},{curve[-1][1]:.2f})")
print("\n纯脊线 vs 参考轴：")
print(f"{'lat':>7} {'脊线lon':>9} {'参考lon':>9} {'偏差km':>8} {'该点剖面峰数':>10}")
for la in np.arange(17.0, 31.01, 0.5):
    j = int(np.argmin(np.abs(np.array([c[1] for c in curve]) - la)))
    if abs(curve[j][1]-la) > 0.3: continue
    k = int(np.argmin(np.abs(rlat - la)))
    if abs(rlat[k]-la) > 0.3: continue
    d = xtrack(curve[j], ref)
    s = snap_ridge(*curve[j])
    pk = s[2] if s else -1
    flag = "  <-- 涡/多支" if pk >= 2 else ""
    print(f"{la:7.2f} {curve[j][0]:9.3f} {rlon[k]:9.3f} {d:8.1f} {pk:10d}{flag}")
