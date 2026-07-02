@echo off
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
"D:\VS\MSBuild\Current\Bin\MSBuild.exe" "d:\opencpn-plugin-template-main\ncdf_pi.slnx" /p:Configuration=Release /p:Platform=Win32 /t:Build /v:minimal > msbuild_output.txt 2>&1