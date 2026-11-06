# -*- coding: utf-8 -*-
"""
kuro_check_delta.py — 独立交叉验证文档里 §15.2 的 Δ 列。
三套算法互相比：haversine(R=6371) / Vincenty 反解(WGS84) / 等距圆柱投影近似。
只有三套一致才算数。
"""
import math

ROWS = [  # (断面, 参考交点, 我们的交点, 文档写的 km)
    ("A3",    (122.910,17.500), (122.885,17.500), 2.7),
    ("A2/B1", (121.660,19.000), (121.802,19.000), 14.9),
    ("B3",    (121.038,20.500), (120.697,20.500), 35.5),
    ("B2/C1", (121.642,22.000), (121.200,22.000), 45.6),
    ("C3",    (122.028,23.500), (121.950,23.500), 8.0),
    ("C2/D1", (122.125,25.000), (122.653,25.000), 53.2),
    ("D3",    (125.876,26.793), (125.895,26.780), 2.4),
    ("D2/E1", (126.904,28.399), (126.899,28.403), 0.7),
    ("E3",    (128.027,29.942), (128.023,29.945), 0.5),
]

def haversine(p, q, R=6371.0):
    p1, p2 = math.radians(p[1]), math.radians(q[1])
    dp = p2 - p1; dl = math.radians(q[0] - p[0])
    h = math.sin(dp/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dl/2)**2
    return 2*R*math.asin(math.sqrt(h))

def equirect(p, q):
    """等距圆柱近似：中纬度处用 cos(平均纬度) 缩放经差"""
    la = math.radians((p[1]+q[1])/2)
    dx = (q[0]-p[0]) * 111.320 * math.cos(la)
    dy = (q[1]-p[1]) * 110.574
    return math.hypot(dx, dy)

def vincenty(p, q, a=6378137.0, f=1/298.257223563):
    """Vincenty 反解（WGS84），返回米"""
    L = math.radians(q[0]-p[0])
    U1 = math.atan((1-f)*math.tan(math.radians(p[1])))
    U2 = math.atan((1-f)*math.tan(math.radians(q[1])))
    sU1, cU1 = math.sin(U1), math.cos(U1)
    sU2, cU2 = math.sin(U2), math.cos(U2)
    lam = L
    for _ in range(200):
        sl, cl = math.sin(lam), math.cos(lam)
        ss = math.hypot(cU2*sl, cU1*sU2 - sU1*cU2*cl)
        if ss == 0: return 0.0
        cs = sU1*sU2 + cU1*cU2*cl
        sig = math.atan2(ss, cs)
        sa = cU1*cU2*sl/ss
        c2a = 1 - sa*sa
        c2sm = cs - 2*sU1*sU2/c2a if c2a != 0 else 0.0
        C = f/16*c2a*(4 + f*(4 - 3*c2a))
        lam_prev = lam
        lam = L + (1-C)*f*sa*(sig + C*ss*(c2sm + C*cs*(-1 + 2*c2sm**2)))
        if abs(lam - lam_prev) < 1e-12: break
    u2 = c2a*(a*a - (a*(1-f))**2) / (a*(1-f))**2
    A = 1 + u2/16384*(4096 + u2*(-768 + u2*(320 - 175*u2)))
    B = u2/1024*(256 + u2*(-128 + u2*(74 - 47*u2)))
    dsig = B*ss*(c2sm + B/4*(cs*(-1 + 2*c2sm**2) - B/6*c2sm*(-3 + 4*ss**2)*(-3 + 4*c2sm**2)))
    return (a*(1-f))*A*(sig - dsig) / 1000.0

# GLORYS 1/12° 网格距（用于「几个网格距」的说法）
def grid_km(lat, cell=1/12):
    return cell*111.320*math.cos(math.radians(lat)), cell*110.574

print(f"{'断面':>7} {'haversine':>10} {'Vincenty':>10} {'圆柱近似':>10} {'文档':>7}  {'三套互差km':>10}  判定")
allok = True
for n, p, q, doc in ROWS:
    h, v, e = haversine(p, q), vincenty(p, q), equirect(p, q)
    spread = max(h, v, e) - min(h, v, e)
    # 判据两层：①文档值必须与它声明的算法（haversine）一致（容 0.06，一位小数舍入）
    #          ②三套不同大地线算法互差 <0.2 km，证明数值稳健（球 vs WGS84 椭球本就差 ~0.2%）
    ok = abs(h - doc) < 0.06 and spread < 0.2
    allok &= ok
    print(f"{n:>7} {h:10.2f} {v:10.2f} {e:10.2f} {doc:7.1f}  {spread:10.3f}  {'✓' if ok else '✗'}")

print(f"\n=== {'Δ 列全部正确 ✓（与声明算法一致，且跨算法稳健）' if allok else '存在偏差 ✗'} ===")

print("\n[换算复核]  GLORYS 1/12° 网格距与『几个网格距』的说法")
for lat in (25.0, 26.8, 28.4):
    dlon, dlat = grid_km(lat)
    print(f"  {lat:4.1f}°N  网格 {dlon:.2f} × {dlat:.2f} km")
print("\n  D3/D2/E1/E3 的 Δ 相对网格距：")
for n, p, q, doc in ROWS[-3:]:
    dlon, dlat = grid_km(p[1])
    cell = min(dlon, dlat)
    print(f"    {n:6s} {doc:4.1f} km = {doc/cell:.2f} 格（最小格距 {cell:.2f} km）")
