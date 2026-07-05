# Verify an IL2CPP Player build folder contains no pcg-core source or Editor DLL.
param(
    [Parameter(Mandatory = $true)]
    [string]$PlayerBuildPath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $PlayerBuildPath)) {
    Write-Error "Player build path not found: $PlayerBuildPath"
}

$violations = @()

$cppFiles = Get-ChildItem -Path $PlayerBuildPath -Recurse -Include *.cpp,*.hpp,*.h -File -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch '\\il2cpp\\' -and
        $_.FullName -notmatch '\\il2cppOutput\\' -and
        $_.FullName -notmatch '\\BackUpThisFolder_ButDontShipItWithYourGame\\' -and
        $_.Name -ne 'baselib.h'
    }

if ($cppFiles) {
    $violations += "Found C/C++ headers/sources outside il2cpp toolchain: $($cppFiles.Count) file(s)"
}

$pcgDll = Get-ChildItem -Path $PlayerBuildPath -Recurse -Filter "PcgCore.dll" -File -ErrorAction SilentlyContinue
if ($pcgDll) {
    $violations += "PcgCore.dll should not ship in Player build (use static link): $($pcgDll.FullName)"
}

$gameAssembly = Get-ChildItem -Path $PlayerBuildPath -Recurse -Filter "GameAssembly.dll" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $gameAssembly) {
    Write-Warning "GameAssembly.dll not found - is this an IL2CPP Windows build?"
}
else {
    Write-Host "Found GameAssembly: $($gameAssembly.FullName)"
}

$dataDir = Join-Path $PlayerBuildPath "*_Data"
if (Test-Path $dataDir) {
    $streamingGraph = Get-ChildItem -Path $dataDir -Recurse -Filter "demo.pcg" -File -ErrorAction SilentlyContinue
    if ($streamingGraph) {
        Write-Host "Found demo graph in StreamingAssets: $($streamingGraph.FullName)"
    }
    else {
        Write-Warning "demo.pcg not found under StreamingAssets (optional for M3 demo)"
    }
}

if ($violations.Count -gt 0) {
    Write-Host ""
    Write-Host "FAILED - release package violations:" -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host ""
Write-Host "PASSED - no PcgCore.dll or stray pcg-core sources in Player package." -ForegroundColor Green
exit 0
