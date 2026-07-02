# Build pcg-core (DLL + LIB) and optionally copy artifacts into Unity Plugins.
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$CopyToUnity,
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$CoreDir = Join-Path $Root "pcg-core"
$BuildDir = Join-Path $CoreDir "build"
$UnityPlugins = Join-Path $Root "Unity\Assets\PcgPlugin\Plugins\x86_64"

Write-Host "==> Configuring pcg-core ($Configuration)"
cmake -S $CoreDir -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> Building pcg-core"
cmake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($RunTests) {
    Write-Host "==> Running ctest"
    ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$OutDir = Join-Path $BuildDir $Configuration
$Dll = Join-Path $OutDir "PcgCore.dll"
$Lib = Join-Path $OutDir "PcgCore.lib"

foreach ($artifact in @($Dll, $Lib)) {
    if (-not (Test-Path $artifact)) {
        Write-Error "Missing build artifact: $artifact"
    }
}

Write-Host "==> Artifacts:"
Get-Item $Dll, $Lib | Format-Table Name, Length, LastWriteTime

if ($CopyToUnity) {
    if (-not (Test-Path $UnityPlugins)) {
        New-Item -ItemType Directory -Path $UnityPlugins -Force | Out-Null
    }

    foreach ($src in @($Dll, $Lib)) {
        $dest = Join-Path $UnityPlugins (Split-Path $src -Leaf)
        try {
            Copy-Item -Path $src -Destination $dest -Force
            Write-Host "Copied -> $dest"
        }
        catch {
            $pending = "$dest.new"
            Copy-Item -Path $src -Destination $pending -Force
            Write-Warning "Unity may have locked $($dest.Name); wrote $pending instead."
        }
    }
}

Write-Host "Done."
