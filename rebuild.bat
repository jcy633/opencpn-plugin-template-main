@echo off
REM Delete CMake cache
del CMakeCache.txt 2>nul
rmdir /s /q CMakeFiles 2>nul
del cmake_output.txt 2>nul
del msbuild_output.txt 2>nul

REM Run CMake
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netcdf/netCDF-32bit" -DBUILD_TYPE=tarball . > cmake_output.txt 2>&1

echo CMake configuration completed.

REM Run MSBuild
"D:\VS\MSBuild\Current\Bin\MSBuild.exe" "ncdf_pi.slnx" /p:Configuration=Release /p:Platform=Win32 /t:Build /v:minimal > msbuild_output.txt 2>&1

echo Build completed.