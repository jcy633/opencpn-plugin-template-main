# -*- coding: utf-8 -*-
r"""
kuro_check_tables.py — 校验 Markdown 表格列数一致。
GFM 规则：单元格里的 | 必须写成 \|，否则会被当成列分隔符。
只按「未被反斜杠转义」的 | 切分。
"""
import re, sys

def cells(line):
    s = line
    if s.startswith("|"): s = s[1:]
    if s.endswith("|"): s = s[:-1]
    out, cur = [], []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            cur.append(s[i]); cur.append(s[i+1]); i += 2; continue   # 转义：整段跳过
        if s[i] == "|":
            out.append("".join(cur)); cur = []; i += 1; continue
        cur.append(s[i]); i += 1
    out.append("".join(cur))
    return out

def main(paths):
    bad = total = 0
    for f in paths:
        lines = open(f, encoding="utf-8").read().split("\n")
        i = 0
        while i < len(lines):
            is_tbl = (lines[i].startswith("|") and i + 1 < len(lines)
                      and re.match(r"^\|[\s:\-\|]+\|\s*$", lines[i + 1]))
            if not is_tbl:
                i += 1; continue
            hdr = cells(lines[i]); n = len(hdr)
            # 分隔行也要数（它不含转义）
            sep = len(cells(lines[i + 1]))
            if sep != n:
                print(f"  ✗ {f}:{i+2}  分隔行 {sep} 列 ≠ 表头 {n} 列"); bad += 1
            j = i + 2; rows = 0
            while j < len(lines) and lines[j].startswith("|"):
                c = len(cells(lines[j]))
                if c != n:
                    print(f"  ✗ {f}:{j+1}  {c} 列 ≠ 表头 {n} 列  ::  {lines[j][:80]}")
                    bad += 1
                rows += 1; j += 1
            total += 1
            print(f"  ✓ {f}:{i+1}  {n} 列 / {rows} 行")
            i = j
    print(f"\n=== 共 {total} 张表，" + ("全部列数一致 ✓" if bad == 0 else f"{bad} 处不一致 ✗") + " ===")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or ["CLAUDE.md", "docs/Wang2026主轴移植验证-排查记录.md"]))
