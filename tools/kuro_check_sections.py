# -*- coding: utf-8 -*-
"""kuro_check_sections.py — 程序化核对 src/ncdf_kuroshio.cpp 的 kSections/kSegs
与官方 MATLAB seg_def 是否逐字一致。"""
import re, sys

MAT = ("E:/my paper/Wang2026_黑潮轴与边界三维日尺度数据集_流线约束最大流速法_"
       "1993-2024东海与吕宋海峡日尺度三维轴线及左右边界/官方代码_Kuroshio-axis-and-boundaries/"
       "main_Kuroshio_axis_and_boundaries.m")
CPP = "src/ncdf_kuroshio.cpp"

mat = open(MAT, encoding="utf-8", errors="replace").read()
blk = mat[mat.index("seg_def = ["):mat.index("];", mat.index("seg_def = ["))]
# struct('name','A', 'cond',..., 'lat_lim',[16,19], 'south_sec',[...], 'north_sec',[...], 'lon_range',[...], 'mid_sec',[...])
segs = []
for m in re.finditer(r"struct\('name','(\w)'.*?'lat_lim',\[([\d.,\-]+)\].*?"
                     r"'south_sec',\[([\d.,\-]+)\].*?'north_sec',\[([\d.,\-]+)\].*?"
                     r"'lon_range',\[([\d.,\-]+)\].*?'mid_sec',\[([\d.,\-]+)\]", blk, re.S):
    name = m.group(1)
    nums = [list(map(float, m.group(i).split(","))) for i in range(2, 7)]
    segs.append((name, *nums))   # lat_lim, south, north, lon_range, mid
print(f"MATLAB seg_def 解析出 {len(segs)} 段: {[s[0] for s in segs]}")

cpp = open(CPP, encoding="utf-8", errors="replace").read()
sec_blk = cpp[cpp.index("kSections[11]"):]
sec_blk = sec_blk[:sec_blk.index("};")]
secs = re.findall(r'\{\s*"([^"]+)",\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\s*\}', sec_blk)
secs = [(n, tuple(map(float, v))) for n, *v in secs]
print(f"C++ kSections 解析出 {len(secs)} 条: {[s[0] for s in secs]}")

seg_blk = cpp[cpp.index("kSegs[5]"):]
seg_blk = seg_blk[:seg_blk.index("};")]
csegs = re.findall(r"\{\s*'(\w)',\s*\{([\d.,\- ]+)\},\s*\{([\d.,\- ]+)\},\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", seg_blk)
print(f"C++ kSegs 解析出 {len(csegs)} 段: {[s[0] for s in csegs]}")

# 建索引：MATLAB 的每条断面 -> 我们的 kSections 下标
def find_sec(coords):
    want = tuple(coords)
    for i, (_, v) in enumerate(secs):
        if v == want: return i
    return -1

ok = True
print(f"\n{'段':>3} {'lat_lim':>14} {'lon_range':>18}  {'south':>6}{'mid':>6}{'north':>6}  结果")
for name, lat_lim, south, north, lon_range, mid in segs:
    i_s, i_m, i_n = find_sec(south), find_sec(mid), find_sec(north)
    c = next(s for s in csegs if s[0] == name)
    c_lat = tuple(map(float, c[1].split(",")))
    c_lon = tuple(map(float, c[2].split(",")))
    c_idx = (int(c[3]), int(c[4]), int(c[5]))
    same = (c_lat == tuple(lat_lim) and c_lon == tuple(lon_range)
            and c_idx == (i_s, i_m, i_n) and -1 not in (i_s, i_m, i_n))
    ok &= same
    print(f"{name:>3} {str(tuple(lat_lim)):>14} {str(tuple(lon_range)):>18}  "
          f"{i_s:>6}{i_m:>6}{i_n:>6}  {'✓ 逐字一致' if same else '✗ 不一致'}")
    if not same:
        print(f"     C++: lat_lim={c_lat} lon_range={c_lon} idx={c_idx}")
print(f"\n=== {'全部一致 ✓' if ok else '存在不一致 ✗'} ===")
sys.exit(0 if ok else 1)
