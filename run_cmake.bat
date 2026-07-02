@echo off
setlocal

set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
set PATH=D:\VS\VC\Tools\MSVC\14.51.36231\bin\Hostx86\x86;C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86;D:\VS\Common7\IDE;D:\VS\MSBuild\Current\Bin;C:\Program Files\CMake\bin;%PATH%
set INCLUDE=D:\VS\VC\Tools\MSVC\14.51.36231\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\ucrt;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\um;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\shared;%INCLUDE%
set LIB=D:\VS\VC\Tools\MSVC\14.51.36231\lib\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\um\x86;%LIB%

if exist "build" rmdir /s /q "build"
mkdir "build"

cd build
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netCDF-32bit" -DBUILD_TYPE=tarball ..

echo.
echo CMake configuration completed.
echo Now building ALL_BUILD target...
"d:\VS\MSBuild\Current\Bin\MSBuild.exe" "ALL_BUILD.vcxproj" /p:Configuration=Release /p:Platform=Win32 /t:Build /verbosity:minimal
