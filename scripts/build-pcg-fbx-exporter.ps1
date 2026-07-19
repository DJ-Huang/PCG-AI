param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$CopyToUnity,
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$SourceDir = Join-Path $RepoRoot "pcg-fbx-exporter"
$BuildDir = Join-Path $SourceDir "build-windows"
$UnityPlugins = Join-Path $RepoRoot "Unity/Assets/PcgPlugin/Plugins/Editor/x86_64"

cmake -S $SourceDir -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($RunTests) {
    ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$Library = Join-Path $BuildDir "$Configuration/PcgFbxExporter.dll"
if (-not (Test-Path $Library)) {
    throw "PcgFbxExporter.dll not found: $Library"
}

if ($CopyToUnity) {
    New-Item -ItemType Directory -Force -Path $UnityPlugins | Out-Null
    Copy-Item -Force $Library (Join-Path $UnityPlugins "PcgFbxExporter.dll")
}

Write-Host "Built $Library"
