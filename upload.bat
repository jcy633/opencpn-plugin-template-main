rem
rem  Upload the .tar.gz and .xml artifacts to cloudsmith
rem

echo OFF
if "%CLOUDSMITH_API_KEY%" == "" (
    echo Warning: CLOUDSMITH_API_KEY is not available, giving up.
    echo Metadata: [ncdf]-0.5-flatpak-x86-22.08.xml
    echo Tarball: [ncdf]-0.5.0.0+2607031213.28caee7_flatpak-x86-22.08-x86.tar.gz
    echo Version: 0.5.0.0+2607031213.28caee7
    exit /b 0
)
echo Using CLOUDSMITH_API_KEY: %CLOUDSMITH_API_KEY:~,4%%...
echo ON

cloudsmith push raw --no-wait-for-sync ^
    --name [ncdf]-0.5-flatpak-x86-22.08-metadata ^
    --version 0.5.0.0+2607031213.28caee7 ^
    --summary "Plugin metadata for automatic installation" ^
    --republish ^
    opencpn/ncdf-alpha [ncdf]-0.5-flatpak-x86-22.08.xml

cloudsmith push raw --no-wait-for-sync ^
    --name [ncdf]-0.5-flatpak-x86-22.08-tarball ^
    --version 0.5.0.0+2607031213.28caee7 ^
    --summary "Plugin tarball for automatic installation" ^
    --republish ^
    opencpn/ncdf-alpha [ncdf]-0.5.0.0+2607031213.28caee7_flatpak-x86-22.08-x86.tar.gz
