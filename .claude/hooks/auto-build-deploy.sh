#!/bin/bash
# auto-build-deploy.sh
# 自动编译并部署 ncdf_pi.dll 到 OpenCPN 插件目录

BUILD_DIR="D:/opencpn-plugin-template-main/build"
DLL_SRC="$BUILD_DIR/Release/ncdf_pi.dll"
DLL_DST="C:/Users/季曹阳/AppData/Local/opencpn/plugins/ncdf_pi.dll"
OPENCPN_LNK='C:\Users\Public\Desktop\OpenCPN 5.14.0-0+91f3b67.lnk'

# 1. 编译（使用 Win32 平台）
cd "$BUILD_DIR"
cmake --build . --config Release --target ncdf_pi 2>&1 | tail -5

# 2. 检查编译是否成功
if [ ! -f "$DLL_SRC" ]; then
    echo "编译失败，尝试重新配置CMake..."
    rm -rf CMakeCache.txt CMakeFiles
    WXWIN="D:/opencpn-plugin-template-main/cache/wxWidgets" cmake .. -A Win32 2>&1 | tail -3
    cmake --build . --config Release --target ncdf_pi 2>&1 | tail -3
fi

if [ ! -f "$DLL_SRC" ]; then
    echo "编译失败"
    exit 1
fi

# 3. 部署DLL
cp -f "$DLL_SRC" "$DLL_DST"

# 4. 打开 OpenCPN
"C:/Program Files (x86)/OpenCPN/opencpn.exe" &
