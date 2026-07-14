@echo off
setlocal
set WXWIN=d:\opencpn-plugin-template-main\cache\wxWidgets
set PATH=D:\VS\VC\Tools\MSVC\14.51.36231\bin\Hostx86\x86;C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86;%PATH%
set INCLUDE=D:\VS\VC\Tools\MSVC\14.51.36231\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\ucrt;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\um;C:\Program Files (x86)\Windows Kits\10\include\10.0.28000.0\shared;%WXWIN%\include;%WXWIN%\lib\vc145_dll\mswu;D:\opencpn-plugin-template-main\include;D:\opencpn-plugin-template-main\opencpn-libs\plugin_dc;D:\opencpn-plugin-template-main\opencpn-libs\wxJSON
set LIB=D:\VS\VC\Tools\MSVC\14.51.36231\lib\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.28000.0\um\x86;%WXWIN%\lib\vc145_dll

echo Compiling...
cl.exe /c /nologo /W3 /O2 /D "WIN32" /D "_WINDOWS" /D "NDEBUG" /D "_USRDLL" /D "ocpnUSE_GL" /D "UNICODE" /D "_UNICODE" /D "wxUSE_GUI=1" /I"%WXWIN%\include" /I"%WXWIN%\lib\vc145_dll\mswu" /I"D:\opencpn-plugin-template-main\include" /I"D:\opencpn-plugin-template-main\opencpn-libs\plugin_dc" /I"D:\opencpn-plugin-template-main\opencpn-libs\wxJSON" /EHsc /MD /Fo"D:\opencpn-plugin-template-main\Release\ncdfoverlayfactory.obj" "D:\opencpn-plugin-template-main\src\ncdfoverlayfactory.cpp"
echo Exit: %ERRORLEVEL%
