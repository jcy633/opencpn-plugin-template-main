@echo off
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netCDF-32bit" -DBUILD_TYPE=tarball . > cmake_output.txt 2>&1
