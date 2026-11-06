#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GLORYS12V1 表层日均流速下载（供 ncdf 插件两条主轴算法使用）

数据集   : cmems_mod_glo_phy_my_0.083deg_P1D-m   (GLOBAL_MULTIYEAR_PHY_001_030)
           = MERCATOR GLORYS12V1 再分析，1/12°，逐日平均
时间覆盖 : 1993-01-01 ~ 2026-09-22   （2026-11-02 实测；后者是服务器当前上限）
变量     : uo / vo，仅表层（depth = 0.494 m）
存储格式 : int16 + scale_factor=0.00061 + add_offset=0.0 + _FillValue=-32767
           （与 E:\\nc1\\glorys_YYYYMMDD.nc 完全同构，插件的 CF 解包逻辑直接可用）

两个下载框（对应插件里硬编码的两条主轴）：

  wang   114~135°E / 12~36°N      Wang2026 流线约束最大流速法（台湾东 / 黑潮上游）
         恰好盖住 ncdf_kuroshio.cpp 的 kuroTraceBounds()：
         A–E 断面 118~130.38°E / 16~31.25°N 外扩 4° -> 114~134.38 / 12~35.25

  seamt  129~161°E / 23~42°N      SEAMT 流线集合最小 T 参数法（日本以南 / 延伸体）
         同时盖住 cdf_seamt.cpp 的两个预设：
           presetJapanSouth   lonIn=130 lonOut=142  latDom 24~38
           presetPaperDomain  lonIn=130 lonOut=160  latDom 27~41

每月下 DAYS 天（默认 8 天，均匀铺开）。**下载是按整月下**，再在本地切出这 8 天——
因为 copernicusmarine.subset 每次调用有约 15 s 固定开销，逐天下 6352 次要 28 小时，
整月一次只要 794 次。切完立刻删掉整月临时文件，不占盘。

用法
----
  python glorys_download.py --probe                 # 只下一个 1×1 格点的小样，验证凭据
  python glorys_download.py --dry-run               # 只列出要下的月份，不联网
  python glorys_download.py --shard 0 --nshards 3   # 并行分片（开几个终端就跑几片）
  python glorys_download.py --region wang --start 2024-01 --end 2024-03
  python glorys_download.py --keep-full-month       # 连整月文件也留着（见下）

凭据走 ~/.copernicusmarine/.copernicusmarine-credentials，不需要在本文件里写账号密码。

--keep-full-month：既然整月文件反正要下，留着它几乎不增加流量，只是多占盘
（约 8 GB，两个框合计）。默认**不留**，只留每月 8 天（约 1.6 GB）。
"""

import argparse
import datetime as dt
import os
import sys
import time
import traceback

# Windows 控制台默认 GBK，中文日志会变乱码；统一按 UTF-8 输出
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

# --------------------------------------------------------------------------
# 配置
# --------------------------------------------------------------------------

DATASET_ID = "cmems_mod_glo_phy_my_0.083deg_P1D-m"

OUT_ROOT = r"E:\glorys_dl"

# 两个算法的覆盖框（lon0, lon1, lat0, lat1），边界都落在 1/12° 格点上
REGIONS = {
    "wang":  dict(lon=(114.0, 135.0), lat=(12.0, 36.0),
                  desc="Wang2026 流线约束最大流速法（台湾东 / 黑潮上游）"),
    "seamt": dict(lon=(129.0, 161.0), lat=(23.0, 42.0),
                  desc="SEAMT 流线集合最小 T 参数法（日本以南 / 延伸体）"),
}

# 每月下载哪几天。8 天均匀铺开，保证每月都有采样。
# 2 月只有 28 天，28 号仍有效；29/30/31 号在小月里会被自动跳过。
DAYS = [1, 5, 9, 13, 17, 21, 25, 28]

DATA_START = dt.date(1993, 1, 1)     # 数据集起点
DATA_END   = dt.date(2026, 9, 22)    # 数据集当前终点（服务器上限，实测）

SURFACE_DEPTH = 0.5                  # nearest -> 0.494 m
VARS = ["uo", "vo"]

MAX_RETRY = 3
RETRY_BACKOFF_S = [10, 60, 180]


# --------------------------------------------------------------------------
# 小工具
# --------------------------------------------------------------------------

LOG_NAME = "download.log"   # 分片并行时在 run() 里改成 download_shardN.log，避免并发写坏


def log(msg):
    line = "[%s] %s" % (dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), msg)
    print(line, flush=True)
    try:
        with open(os.path.join(OUT_ROOT, LOG_NAME), "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass


def month_iter(start_ym, end_ym):
    """(year, month) 生成器，含端点。"""
    y, m = start_ym
    ey, em = end_ym
    while (y, m) <= (ey, em):
        yield y, m
        m += 1
        if m == 13:
            y, m = y + 1, 1


def ym_str(y, m):
    return "%04d%02d" % (y, m)


def days_in_month(y, m):
    if m == 12:
        nxt = dt.date(y + 1, 1, 1)
    else:
        nxt = dt.date(y, m + 1, 1)
    return (nxt - dt.date(y, m, 1)).days


def wanted_days(y, m):
    """该月实际要保留的日子：DAYS 里落在本月、且不超过数据集终点的那些。"""
    nd = days_in_month(y, m)
    out = []
    for d in DAYS:
        if 1 <= d <= nd:
            day = dt.date(y, m, d)
            if DATA_START <= day <= DATA_END:
                out.append(day)
    return out


def daily_path(region, day):
    return os.path.join(OUT_ROOT, region, "glorys_%s.nc" % day.strftime("%Y%m%d"))


def month_done(region, y, m):
    """该月所有目标日子都已有文件 -> 跳过（断点续传）。"""
    days = wanted_days(y, m)
    if not days:
        return True
    return all(os.path.exists(daily_path(region, d)) and
               os.path.getsize(daily_path(region, d)) > 0 for d in days)


# --------------------------------------------------------------------------
# 下载：整月一次 subset，落到临时文件
# --------------------------------------------------------------------------

def download_month(region, y, m, tmp_dir):
    import copernicusmarine as cm

    lo0, lo1 = REGIONS[region]["lon"]
    la0, la1 = REGIONS[region]["lat"]

    nd = days_in_month(y, m)
    # 起：本月 1 号 00:00；止：本月最后一天 00:00（实测 end 传 23:59:59 会多抓一天）
    start = dt.datetime(y, m, 1, 0, 0, 0)
    end = dt.datetime(y, m, nd, 0, 0, 0)

    # 月尾被数据集终点截断
    if dt.date(y, m, nd) > DATA_END:
        end = dt.datetime(DATA_END.year, DATA_END.month, DATA_END.day, 0, 0, 0)

    tmp = os.path.join(tmp_dir, "%s_%s.nc" % (region, ym_str(y, m)))
    if os.path.exists(tmp):
        os.remove(tmp)

    last_err = None
    for attempt in range(MAX_RETRY):
        try:
            r = cm.subset(
                dataset_id=DATASET_ID,
                variables=VARS,
                minimum_depth=SURFACE_DEPTH,
                maximum_depth=SURFACE_DEPTH,
                coordinates_selection_method="nearest",
                minimum_longitude=lo0, maximum_longitude=lo1,
                minimum_latitude=la0, maximum_latitude=la1,
                start_datetime=start.strftime("%Y-%m-%dT%H:%M:%S"),
                end_datetime=end.strftime("%Y-%m-%dT%H:%M:%S"),
                output_directory=tmp_dir,
                output_filename=os.path.basename(tmp),
                overwrite=True,
                disable_progress_bar=True,
            )
            return tmp, (r.file_size or 0.0)
        except Exception as e:
            last_err = e
            if attempt < MAX_RETRY - 1:
                wait = RETRY_BACKOFF_S[attempt]
                log("  retry %d/%d after %ss: %s" %
                    (attempt + 1, MAX_RETRY - 1, wait, type(e).__name__))
                time.sleep(wait)
    raise RuntimeError("subset failed for %s %s: %s" %
                       (region, ym_str(y, m), last_err))


# --------------------------------------------------------------------------
# 本地切分：整月文件 -> 每日文件
# --------------------------------------------------------------------------

def split_month(tmp_path, region, y, m):
    import netCDF4 as nc
    import numpy as np

    days = wanted_days(y, m)
    out_dir = os.path.join(OUT_ROOT, region)
    os.makedirs(out_dir, exist_ok=True)

    src = nc.Dataset(tmp_path, "r")
    try:
        tvar = src.variables["time"]
        tunits = tvar.units
        tcal = getattr(tvar, "calendar", "gregorian")
        tdates = nc.num2date(tvar[:], tunits, tcal,
                             only_use_cftime_datetimes=False,
                             only_use_python_datetimes=True)
        idx_of = {}
        for i, d in enumerate(tdates):
            key = dt.date(d.year, d.month, d.day)
            idx_of.setdefault(key, i)

        written = []
        for day in days:
            if day not in idx_of:
                log("  WARN %s 不在下载到的时间轴里，跳过" % day)
                continue
            i = idx_of[day]
            out = daily_path(region, day)
            write_one_day(src, i, out, tunits, tcal)
            written.append(out)
        return written
    finally:
        src.close()


def write_one_day(src, ti, out_path, tunits, tcal):
    import netCDF4 as nc

    # 先写临时再改名，避免半截文件被当成"已完成"
    tmp_out = out_path + ".part"
    if os.path.exists(tmp_out):
        os.remove(tmp_out)

    dst = nc.Dataset(tmp_out, "w", format="NETCDF4_CLASSIC")
    try:
        for k in src.ncattrs():
            v = src.getncattr(k)
            if isinstance(v, str) and len(v) > 32000:
                v = v[:32000]
            dst.setncattr(k, v)

        for name, dim in src.dimensions.items():
            if name == "time":
                dst.createDimension("time", 1)
            else:
                dst.createDimension(name, len(dim))

        for name, svar in src.variables.items():
            attrs = {k: svar.getncattr(k) for k in svar.ncattrs()}
            fill = attrs.pop("_FillValue", None)

            if name == "time":
                dvar = dst.createVariable("time", svar.dtype, ("time",),
                                          **({"fill_value": fill} if fill is not None else {}))
                dvar[0] = svar[ti]
            elif svar.dimensions == ("time",):
                dvar = dst.createVariable(name, svar.dtype, ("time",),
                                          **({"fill_value": fill} if fill is not None else {}))
                dvar.set_auto_maskandscale(False)
                dvar[0] = svar[ti]
            elif "time" in svar.dimensions:
                # uo/vo: (time, depth, lat, lon) -> 取第 ti 个时间步的**原始**打包值
                # 维度结构原样保留（time 只是缩成 1），保证与
                # E:\nc1\glorys_YYYYMMDD.nc 的 uo(time,depth,lat,lon) 完全同构
                svar.set_auto_maskandscale(False)
                dvar = dst.createVariable(name, svar.dtype, svar.dimensions,
                                          **({"fill_value": fill} if fill is not None else {}))
                dvar.set_auto_maskandscale(False)
                dvar[:] = svar[ti:ti + 1]
            else:
                # 坐标变量（depth/latitude/longitude），整段拷
                dvar = dst.createVariable(name, svar.dtype, svar.dimensions,
                                          **({"fill_value": fill} if fill is not None else {}))
                dvar.set_auto_maskandscale(False)
                dvar[:] = svar[:]

            for k, v in attrs.items():
                try:
                    dvar.setncattr(k, v)
                except Exception:
                    pass
    finally:
        dst.close()

    os.replace(tmp_out, out_path)


# --------------------------------------------------------------------------
# 主流程
# --------------------------------------------------------------------------

def run(args):
    import shutil

    global LOG_NAME
    if args.nshards > 1:
        LOG_NAME = "download_shard%d.log" % args.shard

    os.makedirs(OUT_ROOT, exist_ok=True)
    os.makedirs(os.path.join(OUT_ROOT, "_tmp"), exist_ok=True)
    tmp_dir = os.path.join(OUT_ROOT, "_tmp")

    regions = [args.region] if args.region else list(REGIONS.keys())
    for r in regions:
        if r not in REGIONS:
            sys.exit("未知区域 %r，可选：%s" % (r, ", ".join(REGIONS)))

    start_ym = parse_ym(args.start, (DATA_START.year, DATA_START.month))
    end_ym = parse_ym(args.end, (DATA_END.year, DATA_END.month))

    jobs = []
    for r in regions:
        for (y, m) in month_iter(start_ym, end_ym):
            if month_done(r, y, m):
                continue
            jobs.append((r, y, m))

    # 分片：--shard i --nshards N 只处理 jobs 里下标 % N == i 的那些
    if args.nshards > 1:
        jobs = [j for k, j in enumerate(jobs) if k % args.nshards == args.shard]

    log("=== GLORYS 表层流速下载 ===")
    log("区域 %s" % ", ".join("%s=%s" % (r, REGIONS[r]["desc"]) for r in regions))
    log("框 %s" % "; ".join("%s %s" % (r, REGIONS[r]["lon"] + REGIONS[r]["lat"]) for r in regions))
    log("时间 %s ~ %s，每月保留 %d 天 %s" %
        (start_ym, end_ym, len(DAYS), DAYS))
    log("待办 %d 个月份%s" % (len(jobs), "（分片 %d/%d）" % (args.shard, args.nshards)
                             if args.nshards > 1 else ""))

    if args.dry_run:
        for (r, y, m) in jobs:
            print("  %s %s" % (r, ym_str(y, m)))
        return 0

    t_start = time.time()
    done = failed = 0
    for n, (r, y, m) in enumerate(jobs, 1):
        tag = "%s %s" % (r, ym_str(y, m))
        t0 = time.time()
        try:
            tmp, size_mb = download_month(r, y, m, tmp_dir)
            written = split_month(tmp, r, y, m)
            if not args.keep_full_month:
                try:
                    os.remove(tmp)
                except OSError:
                    pass
            done += 1
            log("[%d/%d] %s  ok  %.1f MB  %d 个日文件  %.0f s" %
                (n, len(jobs), tag, size_mb, len(written), time.time() - t0))
        except Exception as e:
            failed += 1
            log("[%d/%d] %s  FAIL  %s: %s" % (n, len(jobs), tag, type(e).__name__, e))
            traceback.print_exc()
            # 清掉半截临时文件，下次重来
            tmp = os.path.join(tmp_dir, "%s_%s.nc" % (r, ym_str(y, m)))
            for p in (tmp, tmp + ".part"):
                if os.path.exists(p):
                    try:
                        os.remove(p)
                    except OSError:
                        pass

    log("=== 结束：成功 %d，失败 %d，耗时 %.1f 分钟 ===" %
        (done, failed, (time.time() - t_start) / 60.0))
    return 1 if failed else 0


def parse_ym(s, default):
    if not s:
        return default
    y, m = s.split("-")[:2]
    return (int(y), int(m))


def main():
    ap = argparse.ArgumentParser(description="GLORYS12V1 表层日均流速下载")
    ap.add_argument("--region", choices=sorted(REGIONS.keys()),
                    help="只下某个区域（默认两个都下）")
    ap.add_argument("--start", help="起始月 YYYY-MM")
    ap.add_argument("--end", help="结束月 YYYY-MM")
    ap.add_argument("--shard", type=int, default=0)
    ap.add_argument("--nshards", type=int, default=1)
    ap.add_argument("--dry-run", action="store_true", help="只列月份，不联网")
    ap.add_argument("--keep-full-month", action="store_true",
                    help="保留整月临时文件（默认切完就删）")
    ap.add_argument("--probe", action="store_true",
                    help="只下一小块验证凭据和连通性")
    args = ap.parse_args()

    if args.probe:
        import copernicusmarine as cm
        os.makedirs(os.path.join(OUT_ROOT, "_probe"), exist_ok=True)
        t0 = time.time()
        r = cm.subset(
            dataset_id=DATASET_ID, variables=VARS,
            minimum_depth=SURFACE_DEPTH, maximum_depth=SURFACE_DEPTH,
            coordinates_selection_method="nearest",
            minimum_longitude=130, maximum_longitude=131,
            minimum_latitude=30, maximum_latitude=31,
            start_datetime="2024-01-01T00:00:00",
            end_datetime="2024-01-01T00:00:00",
            output_directory=os.path.join(OUT_ROOT, "_probe"),
            output_filename="probe.nc", overwrite=True, disable_progress_bar=True)
        print("probe ok  %.1f s  %.2f MB" % (time.time() - t0, r.file_size or 0))
        return 0

    return run(args)


if __name__ == "__main__":
    sys.exit(main() or 0)
