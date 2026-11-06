#!/usr/bin/env bash
# ============================================================================
# kuro_sweep_batch.sh —— 一次 OpenCPN 启动连跑多轮（批量会话模式）
#
#   bash tools/kuro_sweep_batch.sh <输出目录> <天列表> [参数列表(空格分隔)]
#
# 与 kuro_sweep.sh 的区别：那个每轮都重启 OpenCPN（约 6 秒启动换 1–3 秒提取），
# 这个只启动一次，之后靠「写请求 -> 等 .done -> 换下一条」推进，把启动开销摊掉。
# 实测 3 轮 65 秒 -> 21 秒（含 4 秒启动），轮数越多越划算。
#
# 依赖插件侧的 ncdfAutoSessionActive()：会话标记文件存在即进入轮询。
# 参数名规则：off=论文原评分；wNN=权重 NN*0.01（窗口固定 250）；其它数字=扫描窗。
# 结果文件名 <输出目录>/s<参数>_d<day>.txt；已有 ok,1 的直接跳过（可续跑）。
# 每条结果都用 `file,` 反查输入文件名，防止把上一轮残留当本轮（张冠李戴）。
# ============================================================================
set -u
OUT="${1:?用法: kuro_sweep_batch.sh <输出目录> <天列表> [参数列表]}"
LIST="${2:?缺天列表}"
PARAMS="${3:-w01 w02 w03}"

ROOT="D:/opencpn-plugin-template-main"
REQ="C:/ProgramData/opencpn/ncdf_seamt_auto.txt"
RES="C:/ProgramData/opencpn/ncdf_seamt_auto_result.txt"
MODEFILE="C:/ProgramData/opencpn/ncdf_mode.txt"
SESSION="C:/ProgramData/opencpn/ncdf_seamt_auto_session.txt"
FLAGS="C:/ProgramData/opencpn/ncdf_flags.txt"
DATA="E:\glorys_dl\wang"
OCPN="C:/Program Files (x86)/OpenCPN/opencpn.exe"
S='\'
mkdir -p "$OUT"

cleanup() {
    rm -f "$SESSION" "$REQ" "${REQ}.done" "$REQ.tmp"
    printf 'use\n' > "$MODEFILE"
    taskkill //f //im opencpn.exe >/dev/null 2>&1
    rm -f "C:/ProgramData/opencpn/startcheck.dat" "C:/ProgramData/opencpn/load_stamps/ncdf_pi"
}
trap cleanup EXIT

echo "== [1/4] 部署 DLL =="
taskkill //f //im opencpn.exe >/dev/null 2>&1; sleep 1
cp "$ROOT/build/Release/ncdf_pi.dll" "C:/Users/季曹阳/AppData/Local/opencpn/plugins/ncdf_pi.dll" || exit 1
echo "   $(ls -la --time-style=full-iso "C:/Users/季曹阳/AppData/Local/opencpn/plugins/ncdf_pi.dll" | awk '{print $6, $7}')"

echo "== [2/4] 清标记 + 开会话 =="
rm -f "C:/ProgramData/opencpn/startcheck.dat" "C:/ProgramData/opencpn/load_stamps/ncdf_pi"
rm -f "$REQ" "${REQ}.done" "$REQ.tmp"
printf 'test\n' > "$MODEFILE"
printf 'batch\n' > "$SESSION"

echo "== [3/4] 启动 OpenCPN（只此一次）=="
: > "C:/ProgramData/opencpn/ncdf_debug.log"
start "" "$OCPN"
for i in $(seq 1 40); do
    sleep 1
    if grep -aq "batch session start" "C:/ProgramData/opencpn/ncdf_debug.log" 2>/dev/null; then
        echo "   插件已进入批量会话（约 ${i} 秒）"; break
    fi
    if [ "$i" -eq 40 ]; then echo "   !! 未见 batch session start"; fi
done

echo "== [4/4] 逐条下发 =="
n=0; skip=0; bad=0
for pr in $PARAMS; do
    case "$pr" in
        off)  printf 'kuroRidgeOff=1\n' > "$FLAGS" ;;
        w*)   w="${pr#w}"; w=$((10#$w))
              printf 'kuroRidgeW1000=%d\nkuroRidgeScanKm=250\n' "$((w*100))" > "$FLAGS" ;;
        n*)   nn="${pr#n}"; nn=$((10#$nn))
              printf 'kuroTransectN=%d\nkuroRidgeOff=1\n' "$nn" > "$FLAGS" ;;
        b*)   bb="${pr#b}"; bb=$((10#$bb))
              printf 'kuroBoxPad1000=%d\nkuroRidgeOff=1\n' "$bb" > "$FLAGS" ;;
        *)    printf 'kuroRidgeScanKm=%s\n' "$pr" > "$FLAGS" ;;
    esac
    while read -r day fn; do
        [ -z "${day:-}" ] && continue
        # 天列表可能是 CRLF（Python 文本模式写的）；read 不剥 CR，
        # 留在变量里会让 grep / 路径拼接全部对不上
        day="${day%$'\r'}"; fn="${fn%$'\r'}"
        dst="$OUT/s${pr}_d${day}.txt"
        if [ -s "$dst" ] && grep -q '^ok,1' "$dst" 2>/dev/null; then
            skip=$((skip+1)); continue
        fi
        rm -f "$REQ" "${REQ}.done" "$RES" "$REQ.tmp"
        # 原子替换：先写临时文件再 mv，避免插件读到写了一半的请求
        cat > "$REQ.tmp" <<REQ_EOF
preset=japan
file=${DATA}${S}${fn}
time=0
mode=kuro
REQ_EOF
        mv -f "$REQ.tmp" "$REQ"
        ok=0
        for i in $(seq 1 300); do
            if [ -f "${REQ}.done" ]; then ok=1; break; fi
            sleep 0.2
        done
        if [ "$ok" = 1 ] && [ -f "$RES" ]; then
            cp "$RES" "$dst"
            # 反查 file, 必须等于本次输入 —— 否则是把上一轮残留当本轮（张冠李戴）
            if ! grep -q "file,.*${fn}" "$dst" 2>/dev/null; then
                bad=$((bad+1))
                echo "  !! $pr day=$day file 不匹配 期望=${fn} 实际=$(grep -m1 '^file,' "$dst" 2>/dev/null)" >> "$OUT/_warn.txt"
                rm -f "$dst"
            elif ! grep -q '^ok,1' "$dst" 2>/dev/null; then
                bad=$((bad+1)); echo "  !! $pr day=$day 非 ok" >> "$OUT/_warn.txt"
            fi
        else
            bad=$((bad+1)); echo "  !! $pr day=$day 超时/无结果" >> "$OUT/_warn.txt"
        fi
        n=$((n+1))
        if [ $((n % 12)) -eq 0 ]; then echo "   [$n] $pr day=$day (skip=$skip bad=$bad)"; fi
    done < "$LIST"
done

echo "=== 完成：新跑 $n 轮，跳过 $skip，异常 $bad -> $OUT ==="
