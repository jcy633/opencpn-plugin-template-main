@echo off
del CMakeCache.txt 2>nul
rmdir /s /q CMakeFiles 2>nul
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netcdf/netCDF-32bit" -DBUILD_TYPE=tarball .