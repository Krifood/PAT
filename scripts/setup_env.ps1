[CmdletBinding()]
param(
  [string]$QtRoot = "D:\msys64\ucrt64"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $QtRoot)) {
  throw "指定的 Qt 根目录不存在：$QtRoot"
}

# 当前会话生效
$env:QT_ROOT = $QtRoot
$env:CMAKE_PREFIX_PATH = $QtRoot
if (-not ($env:PATH -split ';' | Where-Object { $_ -eq "$QtRoot\bin" })) {
  $env:PATH = "$QtRoot\bin;$env:PATH"
}

# 持久化到用户环境变量
[Environment]::SetEnvironmentVariable("QT_ROOT", $QtRoot, "User")
[Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", $QtRoot, "User")
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
$pathParts = $currentPath -split ';' | Where-Object { $_ -ne "" }
if ($pathParts -notcontains "$QtRoot\bin") {
  $newPath = "$QtRoot\bin;" + $currentPath
  [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
}

Write-Host "已设置并持久化 QT_ROOT/CMAKE_PREFIX_PATH 到 $QtRoot"
Write-Host "并确保 $QtRoot\bin 已加入用户 Path（可能需要重新打开终端后生效）。"
