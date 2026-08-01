[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "关闭 OpenCPN..." -ForegroundColor Yellow
& taskkill /f /im opencpn.exe 2>$null
Start-Sleep -Seconds 1

$src = Join-Path (Split-Path $PSScriptRoot -Parent) "build\Release\ncdf_pi.dll"
$dstDir = Join-Path $env:LOCALAPPDATA "opencpn\plugins"
$dst = Join-Path $dstDir "ncdf_pi.dll"

if (!(Test-Path $src)) {
    Write-Host "错误：找不到 $src" -ForegroundColor Red
    exit 1
}

if (!(Test-Path $dstDir)) {
    New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
}

Copy-Item -LiteralPath $src -Destination $dst -Force
Write-Host "部署成功！" -ForegroundColor Green

$exe = "C:\Program Files (x86)\OpenCPN\opencpn.exe"
if (Test-Path $exe) {
    Start-Process -FilePath $exe
    Write-Host "OpenCPN 已启动" -ForegroundColor Cyan
} else {
    Write-Host "警告：找不到 OpenCPN: $exe" -ForegroundColor Yellow
}
