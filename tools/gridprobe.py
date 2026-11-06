# -*- coding: utf-8 -*-
"""gridprobe.py — 报 nc 文件的格距 / 变量名 / 时间步数，用于核对"采样精度"该设多少。
用法: python -P tools/gridprobe.py <file.nc> [...]
"""
import sys
import netCDF4 as nc
import numpy as np


def probe(path):
    ds = nc.Dataset(path, "r")
    print("==", path)
    for name in ("lat", "latitude", "y"):
        if name in ds.variables:
            a = np.array(ds.variables[name][:], float)
            d = np.abs(np.diff(a))
            print("   %s: n=%d  %.5f..%.5f  step median=%.5f deg (%.2f km lat)"
                  % (name, a.size, a[0], a[-1], np.median(d), np.median(d) * 111.32))
            break
    for name in ("lon", "longitude", "x"):
        if name in ds.variables:
            a = np.array(ds.variables[name][:], float)
            d = np.abs(np.diff(a))
            print("   %s: n=%d  %.5f..%.5f  step median=%.5f deg" % (name, a.size, a[0], a[-1], np.median(d)))
            break
    if "time" in ds.variables:
        print("   time: n=%d" % ds.variables["time"].size)
    for v in ds.variables:
        if v in ("lat", "lon", "latitude", "longitude", "time", "x", "y", "depth", "z"):
            continue
        var = ds.variables[v]
        print("   var %-14s %s %s" % (v, var.dtype, getattr(var, "standard_name", "")))
    ds.close()


for p in sys.argv[1:]:
    try:
        probe(p)
    except Exception as e:
        print("!!", p, e)
