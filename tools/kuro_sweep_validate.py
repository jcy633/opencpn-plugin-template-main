# -*- coding: utf-8 -*-
"""
kuro_sweep_validate.py — 校验扫描结果是否"张冠李戴"。

背景：`seamt_autotest.sh` 跑完后如果 `ncdf_seamt_auto_result.txt` 没被新结果覆盖
（例如插件被拒载、自动测试没跑），`kuro_sweep.sh` 会把**上一轮残留的结果**
当成这一轮拷走 —— 文件名是今天的，内容却是别的日子的。

判据：每个结果文件头的 `file,` 必须等于该 day 对应的输入文件名。

用法: python -P tools/kuro_sweep_validate.py <结果目录> [天列表]
"""
import sys, os, re

RES = sys.argv[1] if len(sys.argv) > 1 else "E:/kuro_sweep2"
LIST = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RES, "days_all.txt")

day2f = {}
for line in open(LIST, encoding="utf-8"):
    p = line.split()
    if len(p) == 2:
        day2f[int(p[0])] = p[1]

bad, ok, total = [], 0, 0
for fn in sorted(os.listdir(RES)):
    m = re.match(r"s(.+)_d(\d+)\.txt$", fn)
    if not m:
        continue
    total += 1
    day = int(m.group(2))
    with open(os.path.join(RES, fn), encoding="utf-8", errors="replace") as f:
        head = f.read(400)
    mm = re.search(r"^file,(.+)$", head, re.M)
    got = mm.group(1).strip().replace("\\", "/").split("/")[-1] if mm else ""
    exp = day2f.get(day, "")
    if got != exp:
        bad.append((fn, got, exp))
    else:
        ok += 1

print(f"总文件 {total}   file, 匹配 {ok}   不匹配 {len(bad)}")
for f_, g, e in bad[:25]:
    print(f"  ✗ {f_}   实际={g}   应为={e}")
if len(bad) > 25:
    print(f"  ... 另有 {len(bad)-25} 个")
print()
print("不匹配 = 张冠李戴（插件没跑、拷了上一轮残留），必须删掉重跑：")
print("  rm -f " + " ".join(os.path.join(RES, b[0]) for b in bad[:5]) + (" ..." if len(bad) > 5 else ""))
