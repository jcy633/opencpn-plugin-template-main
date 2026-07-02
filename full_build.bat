@echo off
REM Clean all CMake generated files
for %%f in (*.vcxproj *.vcxproj.filters *.slnx CMakeCache.txt) do if exist "%%f" del "%%f"
if exist "CMakeFiles" rmdir /s /q "CMakeFiles"
if exist "ncdf_pi.dir" rmdir /s /q "ncdf_pi.dir"
if exist "ZERO_CHECK.dir" rmdir /s /q "ZERO_CHECK.dir"
if exist "ALL_BUILD.dir" rmdir /s /q "ALL_BUILD.dir"
if exist "INSTALL.dir" rmdir /s /q "INSTALL.dir"
if exist "default.dir" rmdir /s /q "default.dir"
if exist "flatpak.dir" rmdir /s /q "flatpak.dir"
if exist "flatpak-conf.dir" rmdir /s /q "flatpak-conf.dir"
if exist "android.dir" rmdir /s /q "android.dir"
if exist "android-build.dir" rmdir /s /q "android-build.dir"
if exist "android-finish.dir" rmdir /s /q "android-finish.dir"
if exist "android-install.dir" rmdir /s /q "android-install.dir"
if exist "tarball.dir" rmdir /s /q "tarball.dir"
if exist "tarball-build.dir" rmdir /s /q "tarball-build.dir"
if exist "tarball-install.dir" rmdir /s /q "tarball-install.dir"
if exist "tarball-finish.dir" rmdir /s /q "tarball-finish.dir"
if exist "ncdf-i18n.dir" rmdir /s /q "ncdf-i18n.dir"
if exist "ncdf-po-update.dir" rmdir /s /q "ncdf-po-update.dir"
if exist "ncdf-pot-update.dir" rmdir /s /q "ncdf-pot-update.dir"
if exist "build" rmdir /s /q "build"

REM Create new build directory
mkdir build

REM Set environment variables
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
set PATH=D:\VS\VC\Tools\MSVC\14.51.36231\bin\Hostx86\x86;C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86;D:\VS\Common7\IDE;D:\VS\MSBuild\Current\Bin;C:\Program Files\CMake\bin;%PATH%
set INCLUDE=D:\VS\VC\Tools\MSVC\14.51.36231\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\ucrt;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\um;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\shared;%INCLUDE%
set LIB=D:\VS\VC\Tools\MSVC\14.51.36231\lib\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\um\x86;%LIB%

REM Run CMake with full configuration
cd build
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netCDF-32bit" -DBUILD_TYPE=tarball ..

echo.
echo CMake configuration completed. Building project...
cd ..

REM Build the project
"d:\VS\MSBuild\Current\Bin\MSBuild.exe" "build\ALL_BUILD.vcxproj" /p:Configuration=Release /p:Platform=Win32 /t:Build /v:minimal

echo.
echo Build completed. Checking for DLL...
dir /s /b build\*ncdf_pi*.dll
