#!/usr/bin/env bash
# ============================================================================
# kuro_sweep.sh —— 扫「贴脊度扫描窗」参数，覆盖 2024 全年可用天
#
#   bash tools/kuro_sweep.sh <输出目录> [天列表文件]
#
# 天列表每行 "<day号> <nc文件名>"（相对 E:/glorys_dl/wang/）。
# 每天跑 6 组参数：ridgeOff（论文基线）+ scanKm=70/110/150/200/250
# 结果存 <输出目录>/s<scan>_d<day>.txt，scan=off 表示论文评分。
#
# 判据（docs/Wang2026…md §14.4）：报告头的 `file,` 必须与输入一致，否则那轮没真跑。
# ============================================================================
set -u
OUT="${1:?用法: kuro_sweep.sh <输出目录> [天列表]}"
LIST="${2:-/tmp/days_all.txt}"
ROOT="D:/opencpn-plugin-template-main"
RES="C:/ProgramData/opencpn/ncdf_seamt_auto_result.txt"
FLAGS="C:/ProgramData/opencpn/ncdf_flags.txt"
DATA="E:/glorys_dl/wang"
S='\'
mkdir -p "$OUT"

# 参数名规则：
#   off          -> kuroRidgeOff=1（论文原评分，对照）
#   wNN          -> kuroRidgeW1000=NN*100 + kuroRidgeScanKm=250（扫权重）
#   其它数字      -> kuroRidgeScanKm=<数字>（扫扫描窗，权重用默认 0.5）
params=(w01 w02 w03)
n=0; bad=0; skip=0
while read -r day fn; do
    [ -z "${day:-}" ] && continue
    p="${DATA}${S}${fn}"
    for pr in "${params[@]}"; do
        case "$pr" in
            off)  printf 'kuroRidgeOff=1\n' > "$FLAGS" ;;
            w*)   w="${pr#w}"; w=$((10#$w))
                  printf 'kuroRidgeW1000=%d\nkuroRidgeScanKm=250\n' "$((w*100))" > "$FLAGS" ;;
            *)    printf 'kuroRidgeScanKm=%s\n' "$pr" > "$FLAGS" ;;
        esac
        dst="$OUT/s${pr}_d${day}.txt"
        # 续跑：已有有效结果就跳过，不重跑
        if [ -s "$dst" ] && grep -q '^ok,1' "$dst" 2>/dev/null; then
            skip=$((skip+1))
            continue
        fi
        MODE=kuro WAIT=60 bash "$ROOT/seamt_autotest.sh" "$p" 0 >/dev/null 2>&1
        if [ -f "$RES" ]; then
            cp "$RES" "$dst"
            if grep -q '^ok,1' "$dst" 2>/dev/null; then
                :
            else
                echo "  !! day=$day scan=$pr 结果非 ok" >> "$OUT/_warn.txt"
                bad=$((bad+1))
            fi
        else
            echo "  !! day=$day scan=$pr 无结果" >> "$OUT/_warn.txt"
            bad=$((bad+1))
        fi
        n=$((n+1))
        if [ $((n % 24)) -eq 0 ]; then echo "[$n] day=$day scan=$pr done (skip=$skip)"; fi
    done
done < "$LIST"

printf 'use\n' > "C:/ProgramData/opencpn/ncdf_mode.txt"
rm -f "$FLAGS" "C:/ProgramData/opencpn/ncdf_seamt_auto.txt" "C:/ProgramData/opencpn/ncdf_seamt_auto.txt.done"
echo "=== 完成：新跑 $n 轮，跳过已有 $skip 轮，异常 $bad 轮 -> $OUT ==="
