@echo off
"D:\VS\MSBuild\Current\Bin\MSBuild.exe" "d:\opencpn-plugin-template-main\ncdf_pi.slnx" /p:Configuration=Release /p:Platform=Win32 /t:Build