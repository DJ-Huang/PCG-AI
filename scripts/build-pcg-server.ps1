# Build and optionally run pcg-server (Windows).
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$Run,
    [int]$Port = 17890
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ServerDir = Join-Path $Root "pcg-server"
$BuildDir = Join-Path $ServerDir "build"

Write-Host "==> Configuring pcg-server ($Config)"
cmake -S $ServerDir -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
Write-Host "==> Building pcg-server"
cmake --build $BuildDir --config $Config -j

$Bin = @(
    (Join-Path $BuildDir "pcg-server.exe"),
    (Join-Path $BuildDir "$Config\pcg-server.exe"),
    (Join-Path $BuildDir "Release\pcg-server.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $Bin) {
    throw "pcg-server.exe not found under $BuildDir"
}

Write-Host "==> Built: $Bin"
if ($Run) {
    Write-Host "==> Starting pcg-server on port $Port"
    & $Bin --port $Port
}
