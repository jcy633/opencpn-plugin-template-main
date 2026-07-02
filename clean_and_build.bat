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

REM Run CMake with full configuration
cd build
"C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/netCDF-32bit" -DBUILD_TYPE=tarball ..

echo.
echo Configuration done.
