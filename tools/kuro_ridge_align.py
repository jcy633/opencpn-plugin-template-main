# -*- coding: utf-8 -*-
"""
kuro_ridge_align.py — 回答「参考轴到底贴不贴速度脊」。

对每条轴的每个点，沿「垂直于该点流向」的断面找 `V_proj`（沿流投影）极大 = 局部速度脊，
量该点到脊的距离。窗宽可调（论文吸附是 ±70 km）。

若参考轴的中位偏移只有几公里 => 它就是速度脊，「直接拉到脊上」可行；
若偏移几十公里 => 它不是脊，硬拉反而会拉偏。

用法: python -P tools/kuro_ridge_align.py <day:tag:glorys...> [--ours 前缀]
"""
import sys, os, math
import numpy as np

GDIR = os.environ.get("KURO_GLORYS_DIR", "E:/nc1")
os.environ.setdefault("KURO_GLORYS", GDIR + "/glorys_20240101.nc")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kuro_diag5 as K
from kuro_diag_axis import read_ref, read_ours

args = [a for a in sys.argv[1:]]
ours_pre = None
if "--ours" in args:
    i = args.index("--ours"); ours_pre = args[i+1]; args = args[:i] + args[i+2:]
trip = [a.split(":") for a in args]      # day:tag:glorys

kKmPerDeg = 110.54
kKmLonPerDeg = 111.32

def ridge_offset(lo, la, half_km, nsect):
    r = K.sample(lo, la)
    if r is None or (r[0] == 0.0 and r[1] == 0.0):
        return np.nan
    th = math.atan2(r[1], r[0])
    degLat = half_km / kKmPerDeg
    cLat = max(math.cos(math.radians(la)), 1e-6)
    degLon = half_km / (kKmLonPerDeg * cLat)
    perp = th + math.pi / 2
    dLon = degLon * math.cos(perp); dLat = degLat * math.sin(perp)
    best, bl, bt = -1e300, lo, la
    for k in range(nsect):
        t = k / (nsect - 1)
        p = (lo - dLon) + 2 * dLon * t
        q = (la - dLat) + 2 * dLat * t
        rr = K.sample(p, q)
        vp = (rr[0]*math.cos(th) + rr[1]*math.sin(th)) if rr else -1e300
        if vp > best: best, bl, bt = vp, p, q
    # 距离
    mx = kKmLonPerDeg * math.cos(math.radians(la))
    return math.hypot((bl-lo)*mx, (bt-la)*kKmPerDeg)

def scan(lon, lat, half_km, label):
    step = float(os.environ.get('KURO_RIDGE_STEP_KM', '2.0'))
    nsect = max(21, int(round(2*half_km/step)) + 1)
    out = []
    for lo, la in zip(lon, lat):
        out.append(ridge_offset(lo, la, half_km, nsect))
    a = np.array(out, float)
    fin = a[np.isfinite(a)]
    if not len(fin):
        print(f"  {label:<18} 无有效点"); return
    print(f"  {label:<18} 中位 {np.median(fin):6.2f} km   均值 {np.mean(fin):6.2f}   "
          f"P90 {np.percentile(fin,90):6.2f}   最大 {fin.max():7.2f}   <5km占 {(fin<5).mean()*100:4.0f}%")

print("窗宽 = 论文吸附半宽 ±70 km（断面总宽 140 km）")
print(f"{'':<18} {'':1}")
for day, tag, gl in trip:
    os.environ["KURO_GLORYS"] = f"{GDIR}/glorys_{gl}.nc"
    import importlib, kuro_diag5
    importlib.reload(kuro_diag5); K = kuro_diag5
    rlon, rlat = read_ref(int(day))
    print(f"\n=== day{int(day):03d} ===")
    scan(rlon, rlat, 70.0, "参考轴 R=70")
    scan(rlon, rlat, 150.0, "参考轴 R=150")
    scan(rlon, rlat, 250.0, "参考轴 R=250")
    if ours_pre:
        olon, olat = read_ours(ours_pre + str(int(day)) + ".txt")
        scan(olon, olat, 70.0, "我们的轴 R=70")
        scan(olon, olat, 250.0, "我们的轴 R=250")
