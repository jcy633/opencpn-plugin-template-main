@echo off
cd "d:\opencpn-plugin-template-main\build"
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netCDF-32bit" -DBUILD_TYPE=tarball ..
echo.
echo Configuration done. Press any key to continue...
pause
