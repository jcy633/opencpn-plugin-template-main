#!/usr/bin/env bash
# ============================================================================
# sweep_gin.sh —— 对同一份数据扫一批 gamma_in，快速定位"哪条经线起才追得上急流"
#
# 用法：
#   ./sweep_gin.sh <nc文件路径> <lonin> [lonin ...]
#   LONPAD=0 ./sweep_gin.sh 'E:\nc1\全3.nc' 130 131 132   # 传给 seamt_autotest.sh
# 例：
#   ./sweep_gin.sh 'E:\nc1\全3.nc' 130 131 132 133 134
#
# 每个 gamma_in 跑一遍 seamt_autotest.sh（Γ_out 固定 142），只打印关键指标。
# 逐次运行的完整报告会被保存成 gin_<lon>.txt，方便事后比对核心线。
# ============================================================================
set -u

NC="${1:-}"
if [ -z "$NC" ]; then
    echo "用法: $0 <nc文件路径> <lonin> [lonin ...]"
    exit 1
fi
shift

OUT="${OUT:-$(cd "$(dirname "$0")" && pwd)/sweep_out}"
mkdir -p "$OUT"
RES="C:/ProgramData/opencpn/ncdf_seamt_auto_result.txt"
LOG="C:/ProgramData/opencpn/ncdf_debug.log"

for gin in "$@"; do
    WAIT=${WAIT:-250} ./seamt_autotest.sh "$NC" 0 "$gin" 142.0 > "$OUT/gin_$gin.log" 2>&1
    cp "$RES" "$OUT/gin_$gin.txt" 2>/dev/null
    echo "===== gamma_in = $gin ====="
    if [ -f "$RES" ]; then
        grep -aE "^(T_days|length_km|mean_speed_ms|lat_in|lat_out|G,|reasons|bridged)," "$RES" | sed 's/^/  /'
        grep -a "P0 candidate\|core:\|axis fit\|traces=" "$LOG" | tail -8 | sed 's/^/  /'
    else
        echo "  (no result file -- see $OUT/gin_$gin.log)"
    fi
    # 清掉残留请求文件：它一旦留下，下次启动 OpenCPN 就会重复触发自动测试
    rm -f "C:/ProgramData/opencpn/ncdf_seamt_auto.txt"
done
