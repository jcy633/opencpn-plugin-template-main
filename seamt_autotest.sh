#!/usr/bin/env bash
# ============================================================================
# seamt_autotest.sh —— SEAMT 一条命令回归
#
# 用法：
#   ./seamt_autotest.sh <nc文件路径> [时间步下标] [lonin] [lonout]
#   MODE=kuro ./seamt_autotest.sh <nc文件路径>        # 跑 Wang2026 而非 SEAMT
#   NPTS=561  ./seamt_autotest.sh <nc文件路径>        # 覆盖集合规模
# 例：
#   ./seamt_autotest.sh 'E:\nc1\海流1.nc' 0 131.0 142.0
#   MODE=kuro ./seamt_autotest.sh 'E:\nc1\海流1.nc'
#
# 做四件事：部署 DLL -> 写请求文件 -> 启动 OpenCPN（插件会自动跑 SEAMT + 全套自检）
#       -> 读结果文件。全程无需手动点界面。
#
# 依赖：插件里 MainDialog::maybeRunSeamtAutoTest 会检测请求文件并自动执行。
# 见 docs/SEAMT急流核心提取-方法论与实现方案.md
# ============================================================================
set -u

NCFILE="${1:-}"
TIMEIDX="${2:-0}"
LONIN="${3:-131.0}"
LONOUT="${4:-142.0}"

if [ -z "$NCFILE" ]; then
    echo "用法: $0 <nc文件路径> [时间步] [lonin] [lonout]"
    exit 1
fi

ROOT="D:/opencpn-plugin-template-main"
DLL_SRC="$ROOT/build/Release/ncdf_pi.dll"
DLL_DST="C:/Users/季曹阳/AppData/Local/opencpn/plugins/ncdf_pi.dll"
REQ="C:/ProgramData/opencpn/ncdf_seamt_auto.txt"
RES="C:/ProgramData/opencpn/ncdf_seamt_auto_result.txt"
MODEFILE="C:/ProgramData/opencpn/ncdf_mode.txt"
LOG="C:/ProgramData/opencpn/ncdf_debug.log"
OCPN="C:/Program Files (x86)/OpenCPN/opencpn.exe"

echo "== [1/4] 部署 DLL =="
if [ ! -f "$DLL_SRC" ]; then
    echo "   找不到 $DLL_SRC —— 先编译"
    exit 1
fi
taskkill //f //im opencpn.exe >/dev/null 2>&1
sleep 1
cp "$DLL_SRC" "$DLL_DST" || exit 1
echo "   $(ls -la --time-style=full-iso "$DLL_DST" | awk '{print $6, $7}')"

echo "== [2/4] 写请求文件 =="
# 测试型/使用型开关：自动化测试入口只在 ncdf_mode.txt 含 test 时生效。
# 这里临时切到 test，脚本退出时恢复原状（默认 use），
# 免得跑完测试后插件一直处于测试型、影响日常使用。
OLD_MODE="$(cat "$MODEFILE" 2>/dev/null || echo use)"
trap 'printf "%s\n" "$OLD_MODE" > "$MODEFILE"' EXIT
printf "test\n" > "$MODEFILE"
echo "   ncdf_mode.txt -> test （脚本结束时恢复为 $OLD_MODE）"

rm -f "$RES"
cat > "$REQ" <<REQ_EOF
file=$NCFILE
time=$TIMEIDX
mode=${MODE:-seamt}
lonin=$LONIN
lonout=$LONOUT
npts=${NPTS:-281}
halfwidth=0.70
REQ_EOF
cat "$REQ" | sed 's/^/   /'

echo "== [3/4] 启动 OpenCPN 并等待自动测试 =="
: > "$LOG"          # 清空日志，方便只看本次
start "" "$OCPN"

for i in $(seq 1 ${WAIT:-60}); do
    sleep 2
    if [ -f "$RES" ]; then
        echo "   完成（约 $((i*2)) 秒）"
        break
    fi
done

echo "== [4/4] 结果 =="
if [ -f "$RES" ]; then
    cat "$RES"
    echo
    echo "-- 诊断日志 --"
    grep -a "\[seamtauto\]\|\[seamt\]" "$LOG" | tail -25
else
    echo "   !! 超时：结果文件未生成。看日志："
    grep -a "\[seamtauto\]\|\[seamt\]" "$LOG" | tail -20
    echo "   （若日志为空，说明面板没打开 —— 检查插件是否加载）"
fi

# 收尾：必须关掉 OpenCPN，否则脚本看起来像卡住了
echo
echo "== 收尾：关闭 OpenCPN =="
if taskkill //f //im opencpn.exe >/dev/null 2>&1; then
    echo "   已关闭"
else
    echo "   （opencpn.exe 本来就没在运行，或已退出）"
fi

[ -f "$RES" ] || exit 2
