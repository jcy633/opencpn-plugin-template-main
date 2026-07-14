@echo off
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets

cd /d D:\opencpn-plugin-template-main\build
"D:\VS\MSBuild\Current\Bin\MSBuild.exe" "ncdf_pi.vcxproj" -p:Configuration=Release -p:Platform=Win32 -t:Build -v:minimal > ..\msbuild_output.txt 2>&1

echo Build completed.
cd /d D:\opencpn-plugin-template-main

REM Kill OpenCPN if running
taskkill /f /im opencpn.exe >nul 2>&1
timeout /t 1 /nobreak >nul

REM Deploy
copy /Y "build\Release\ncdf_pi.dll" "C:\Users\季曹阳\AppData\Local\opencpn\plugins\" > nul
echo Deployed.

REM Launch OpenCPN
start "" "C:\Program Files (x86)\OpenCPN\opencpn.exe"
echo OpenCPN launched.
