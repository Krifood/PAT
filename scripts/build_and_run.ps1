[CmdletBinding()]
param(
  [ValidateSet("Build", "Test", "Run", "Package")]
  [string]$Action = "Package",
  [string]$BuildDir = "build/windows",
  [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Config = "Release",
  [string]$QtRoot,
  [int]$Jobs = [Environment]::ProcessorCount,
  [switch]$DisableSerial,
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Test-QtRoot([string]$Path) {
  if (-not $Path) { return $false }
  return Test-Path (Join-Path $Path "lib/cmake/Qt6/Qt6Config.cmake")
}

function Find-QtRoot {
  $candidates = [System.Collections.Generic.List[string]]::new()
  if ($QtRoot) { $candidates.Add($QtRoot) }
  if ($env:QT_ROOT) { $candidates.Add($env:QT_ROOT) }
  if ($env:CMAKE_PREFIX_PATH) {
    foreach ($entry in ($env:CMAKE_PREFIX_PATH -split ';')) { $candidates.Add($entry) }
  }
  if ($env:Qt6_DIR) { $candidates.Add((Join-Path $env:Qt6_DIR "../../..")) }

  foreach ($drive in @("C:", "D:")) {
    $candidates.Add((Join-Path $drive "msys64/ucrt64"))
    $candidates.Add((Join-Path $drive "msys64/mingw64"))
  }

  foreach ($qtBase in @("C:\Qt", "D:\Qt")) {
    if (Test-Path $qtBase) {
      Get-ChildItem $qtBase -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object {
          Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue |
            Where-Object Name -Match '^(msvc|mingw)' |
            ForEach-Object { $candidates.Add($_.FullName) }
        }
    }
  }

  foreach ($candidate in $candidates) {
    try { $resolved = (Resolve-Path $candidate -ErrorAction Stop).Path } catch { continue }
    if (Test-QtRoot $resolved) { return $resolved }
  }
  throw "Qt 6 was not found. Pass -QtRoot <path>, for example C:\Qt\6.8.3\mingw_64 or D:\msys64\ucrt64."
}

function Find-CommandPath([string[]]$Names, [string[]]$SearchDirs) {
  foreach ($dir in $SearchDirs) {
    if (-not $dir) { continue }
    foreach ($name in $Names) {
      $candidate = Join-Path $dir $name
      if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
  }
  foreach ($name in $Names) {
    $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
  }
  return $null
}

function Copy-MingwRuntimeDependencies([string]$StageDir, [string]$SourceBin, [string]$Objdump) {
  # MSYS2's windeployqt packages Qt plugins but intentionally leaves distro
  # runtime libraries in /ucrt64/bin. Recursively copy only DLLs imported by
  # the deployed executable, Qt libraries and plugins.
  $destinationBin = Join-Path $StageDir "bin"
  $knownNames = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
  $queue = [System.Collections.Generic.Queue[string]]::new()
  Get-ChildItem $StageDir -Recurse -File |
    Where-Object Extension -In @(".exe", ".dll") |
    ForEach-Object {
      [void]$knownNames.Add($_.Name)
      $queue.Enqueue($_.FullName)
    }
  $scanned = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)

  while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (-not $scanned.Add($binary)) { continue }
    & $Objdump -p $binary 2>$null | ForEach-Object {
      if ($_ -notmatch 'DLL Name:\s*(.+?)\s*$') { return }
      $name = $Matches[1]
      if ($knownNames.Contains($name)) { return }
      $source = Join-Path $SourceBin $name
      if (-not (Test-Path $source)) { return }
      $destination = Join-Path $destinationBin $name
      Copy-Item -LiteralPath $source -Destination $destination
      [void]$knownNames.Add($name)
      $queue.Enqueue($destination)
      Write-Host "Deploying MinGW runtime dependency: $name"
    }
  }
}

$QtRoot = Find-QtRoot
$qtBin = Join-Path $QtRoot "bin"
$env:PATH = "$qtBin;$env:PATH"
$qtInstallBase = Split-Path (Split-Path $QtRoot -Parent) -Parent
$qtToolsDir = Join-Path $qtInstallBase "Tools"
$cmakeSearchDirs = [System.Collections.Generic.List[string]]::new()
$cmakeSearchDirs.Add($qtBin)
if (Test-Path $qtToolsDir) {
  Get-ChildItem $qtToolsDir -Directory -Filter "CMake*" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { $cmakeSearchDirs.Add((Join-Path $_.FullName "bin")) }
}

$cmake = Find-CommandPath @("cmake.exe", "cmake") $cmakeSearchDirs
if (-not $cmake) { throw "CMake 3.20 or newer is required and was not found." }

$buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) {
  [IO.Path]::GetFullPath($BuildDir)
} else {
  [IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
}

if ($Clean -and (Test-Path $buildPath)) {
  $repoPrefix = $RepoRoot.TrimEnd('\') + '\'
  if (-not $buildPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a directory outside the repository: $buildPath"
  }
  Remove-Item -LiteralPath $buildPath -Recurse -Force
}

$qtLooksLikeMingw = (Test-Path (Join-Path $QtRoot "bin/g++.exe")) -or
  ((Split-Path $QtRoot -Leaf) -match '^mingw')
$generator = $null
$configureArgs = @(
  "-S", $RepoRoot,
  "-B", $buildPath,
  "-DCMAKE_PREFIX_PATH=$QtRoot",
  "-DPAT_BUILD_GUI=ON",
  "-DPAT_ENABLE_SERIAL=$(if ($DisableSerial) { 'OFF' } else { 'ON' })",
  "-DPAT_BUILD_TESTS=ON",
  "-DPAT_STRICT_WARNINGS=ON"
)

if ($qtLooksLikeMingw) {
  $compilerDirs = [System.Collections.Generic.List[string]]::new()
  $compilerDirs.Add($qtBin)
  $ninjaDirs = [System.Collections.Generic.List[string]]::new()
  $ninjaDirs.Add($qtBin)
  if (Test-Path $qtToolsDir) {
    Get-ChildItem $qtToolsDir -Directory -Filter "mingw*" -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending |
      ForEach-Object { $compilerDirs.Add((Join-Path $_.FullName "bin")) }
    Get-ChildItem $qtToolsDir -Directory -Filter "Ninja*" -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending |
      ForEach-Object { $ninjaDirs.Add($_.FullName) }
  }
  $ninja = Find-CommandPath @("ninja.exe", "ninja") $ninjaDirs
  if (-not $ninja) { throw "Ninja is required for the detected MinGW Qt installation." }
  $cxx = Find-CommandPath @("g++.exe", "c++.exe") $compilerDirs
  if (-not $cxx) { throw "A MinGW C++ compiler matching this Qt installation was not found." }

  $generator = "Ninja"
  $configureArgs += @(
    "-G", $generator,
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_CXX_COMPILER=$cxx",
    "-DCMAKE_BUILD_TYPE=$Config"
  )
}

Write-Host "Qt:      $QtRoot"
Write-Host "Build:   $buildPath"
Write-Host "Config:  $Config"
Write-Host "Serial:  $(if ($DisableSerial) { 'disabled' } else { 'enabled' })"

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

& $cmake --build $buildPath --config $Config --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

$exe = @(
  (Join-Path $buildPath "pat_app.exe"),
  (Join-Path $buildPath "$Config/pat_app.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe -or -not (Test-Path $exe)) {
  throw "Build succeeded but pat_app.exe was not found under: $buildPath"
}

if ($Action -in @("Test", "Run", "Package")) {
  & $cmake --build $buildPath --config $Config --target pat_core_tests --parallel $Jobs
  if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE." }
  & $cmake -E env "PATH=$qtBin;$env:PATH" ctest --test-dir $buildPath -C $Config --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }
}

if ($Action -eq "Run") {
  Write-Host "Launching $exe"
  & $exe
  exit $LASTEXITCODE
}

if ($Action -eq "Package") {
  $packageRoot = Join-Path $buildPath "package"
  $stageDir = Join-Path $packageRoot "PAT-$Config"
  if (Test-Path $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
  New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

  & $cmake --install $buildPath --config $Config --prefix $stageDir
  if ($LASTEXITCODE -ne 0) { throw "Install/deployment failed with exit code $LASTEXITCODE." }

  if ($qtLooksLikeMingw) {
    $objdump = Find-CommandPath @("objdump.exe", "objdump") @((Split-Path $cxx -Parent), $qtBin)
    if (-not $objdump) { throw "objdump is required to collect MinGW runtime dependencies." }
    Copy-MingwRuntimeDependencies $stageDir $qtBin $objdump
  }

  Copy-Item -LiteralPath (Join-Path $RepoRoot "README.md") -Destination $stageDir
  Copy-Item -LiteralPath (Join-Path $RepoRoot "CHANGELOG.md") -Destination $stageDir
  Copy-Item -LiteralPath (Join-Path $RepoRoot "samples") -Destination $stageDir -Recurse

  $zipPath = Join-Path $packageRoot "PAT-$Config-win64.zip"
  if (Test-Path $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
  Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
  $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
  Set-Content -LiteralPath "$zipPath.sha256" -Value "$hash  $(Split-Path $zipPath -Leaf)" -Encoding ascii

  Write-Host "Portable directory: $stageDir"
  Write-Host "Release archive:    $zipPath"
  Write-Host "SHA-256:            $hash"
} else {
  Write-Host "Executable: $exe"
}
