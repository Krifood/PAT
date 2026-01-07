[CmdletBinding()]
param(
  [string]$BuildDir = "build",
  [string]$Config = "Release",
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\").Path.TrimEnd('\\')
$MsysRoot = "D:\\msys64"
$QtRoot = "D:\\msys64\\ucrt64"
$qtBin = Join-Path $QtRoot "bin"
$cmakeExe = Join-Path $qtBin "cmake.exe"
$ninjaExe = Join-Path $qtBin "ninja.exe"
$cxxExe = Join-Path $qtBin "c++.exe"
$ccExe = Join-Path $qtBin "gcc.exe"
if (-not (Test-Path $QtRoot)) { throw "Qt root not found at $QtRoot" }
if (-not (Test-Path $cmakeExe)) { throw "cmake.exe not found at $cmakeExe" }
if (-not (Test-Path $ninjaExe)) { throw "ninja.exe not found at $ninjaExe" }
if (-not (Test-Path $cxxExe)) { throw "c++.exe not found at $cxxExe" }
$env:PATH = "$qtBin;$env:PATH"
$env:QT_ROOT = $QtRoot
$env:CMAKE_PREFIX_PATH = $QtRoot

$buildPath = Join-Path $RepoRoot $BuildDir
if ($Clean -and (Test-Path $buildPath)) { Remove-Item -Recurse -Force $buildPath }

function Get-Generator {
  if ($env:CMAKE_GENERATOR) { return $env:CMAKE_GENERATOR }
  if ($QtRoot -match "msys64" -or $QtRoot -match "mingw") { return "Ninja" }
  return $null
}

function Is-MultiConfig([string]$Generator) {
  if (-not $Generator) { return $true }
  if ($Generator -match "Visual Studio") { return $true }
  if ($Generator -match "Xcode") { return $true }
  if ($Generator -match "Ninja Multi-Config") { return $true }
  return $false
}

$generator = Get-Generator
$cmakeArgs = @(
  "-S", $RepoRoot,
  "-B", $buildPath,
  "-DPAT_ENABLE_QT_CHARTS=ON",
  "-DPAT_STRICT_WARNINGS=ON",
  "-DCMAKE_PREFIX_PATH=$QtRoot",
  "-DCMAKE_MAKE_PROGRAM=$ninjaExe",
  "-DCMAKE_C_COMPILER=$ccExe",
  "-DCMAKE_CXX_COMPILER=$cxxExe"
)
if ($generator) { $cmakeArgs += "-G"; $cmakeArgs += $generator }
if (-not (Is-MultiConfig $generator)) { $cmakeArgs += "-DCMAKE_BUILD_TYPE=$Config" }

& $cmakeExe @cmakeArgs
& $cmakeExe --build $buildPath --config $Config

$exe = Join-Path $buildPath "pat_app.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $buildPath "pat_app" }
if (-not (Test-Path $exe)) { $exe = Join-Path (Join-Path $buildPath $Config) "pat_app.exe" }
if (-not (Test-Path $exe)) { $exe = Join-Path (Join-Path $buildPath $Config) "pat_app" }
if (Test-Path $exe) { Write-Host "Launching $exe"; & $exe } else { Write-Warning "Binary not found at $exe" }
