[CmdletBinding()]
param(
  [string]$QtRoot = $env:QT_ROOT,
  [string]$BuildDir = "build",
  [string]$Config = "Release",
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\").Path.TrimEnd('\')
if (-not $QtRoot -and $env:CMAKE_PREFIX_PATH) { $QtRoot = $env:CMAKE_PREFIX_PATH }
if (-not $QtRoot) { throw "Set -QtRoot or QT_ROOT/CMAKE_PREFIX_PATH to your Qt prefix (with lib/cmake/Qt6Core)." }
$qtBin = Join-Path $QtRoot "bin"
if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
$buildPath = Join-Path $RepoRoot $BuildDir
if ($Clean -and (Test-Path $buildPath)) { Remove-Item -Recurse -Force $buildPath }
$cmakeArgs = @("-S", $RepoRoot, "-B", $buildPath, "-DPAT_ENABLE_QT_CHARTS=ON", "-DPAT_STRICT_WARNINGS=ON", "-DCMAKE_PREFIX_PATH=$QtRoot")
if ($env:CMAKE_GENERATOR) { $cmakeArgs += "-G"; $cmakeArgs += $env:CMAKE_GENERATOR }
cmake @cmakeArgs
cmake --build $buildPath --config $Config
$exe = Join-Path $buildPath "pat_app.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $buildPath "pat_app" }
if (Test-Path $exe) { Write-Host "Launching $exe"; & $exe } else { Write-Warning "Binary not found at $exe" }
