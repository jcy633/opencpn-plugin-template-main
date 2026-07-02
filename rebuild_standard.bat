@echo off
echo ================================================
echo           ncdf_pi 插件一键编译脚本
echo ================================================
echo.

echo [1/3] 删除旧的CMake缓存...
del CMakeCache.txt 2>nul
rmdir /s /q CMakeFiles 2>nul
echo 完成。

echo.
echo [2/3] 运行CMake配置...
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netcdf/netCDF-32bit" -DBUILD_TYPE=tarball .
if %errorlevel% neq 0 (
    echo CMake配置失败！
    pause
    exit /b 1
)
echo 完成。

echo.
echo [3/3] 运行MSBuild编译...
"D:\VS\MSBuild\Current\Bin\MSBuild.exe" "ncdf_pi.slnx" /p:Configuration=Release /p:Platform=Win32 /t:Build /v:minimal
if %errorlevel% neq 0 (
    echo 编译完成（可能有非关键错误，如flatpak-conf）
) else (
    echo 编译成功！
)

echo.
echo ================================================
echo 检查输出文件...
if exist "Release\ncdf_pi.dll" (
    echo DLL文件已生成: Release\ncdf_pi.dll
    for %%f in ("Release\ncdf_pi.dll") do echo 文件大小: %%~zf 字节
) else (
    echo ERROR: DLL文件未生成！
)
echo ================================================

pause