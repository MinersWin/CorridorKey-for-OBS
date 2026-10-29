# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$Build = 'build_x64')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$hostRoot = 'S:\Projects\obs-test\obs-30.2.0'
$dll = Join-Path $hostRoot 'obs-plugins/64bit/corridorkey.dll'
$data = Join-Path $hostRoot 'data/obs-plugins/corridorkey'
$config = Join-Path $hostRoot 'config/obs-studio'
$backup = Join-Path $repo ('.cache/obs30-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$output = Join-Path $repo '.cache/obs30-captures'
$portableExe = Join-Path $hostRoot 'bin/64bit/obs64.exe'
if (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -eq $portableExe) { throw 'The portable OBS test host is already running.' }
New-Item -ItemType Directory -Path $backup, $output -Force | Out-Null
$hadDll = Test-Path -LiteralPath $dll
$hadData = Test-Path -LiteralPath $data
$pluginMutated = $false
$owned = $null
try {
  if ($hadDll) { Copy-Item -LiteralPath $dll -Destination (Join-Path $backup 'corridorkey.dll') }
  if ($hadData) { Copy-Item -LiteralPath $data -Destination (Join-Path $backup 'data') -Recurse }
  Copy-Item -LiteralPath (Join-Path $config 'global.ini') -Destination (Join-Path $backup 'global.ini')
  $pluginMutated = $true
  Copy-Item -LiteralPath (Join-Path $repo "$Build/RelWithDebInfo/corridorkey.dll") -Destination $dll -Force
  New-Item -ItemType Directory -Path $data -Force | Out-Null
  Copy-Item -Path (Join-Path $repo 'data/*') -Destination $data -Recurse -Force
  $ini = [System.IO.File]::ReadAllText((Join-Path $config 'global.ini')).Replace('FirstRun=true', 'FirstRun=false')
  [System.IO.File]::WriteAllText((Join-Path $config 'global.ini'), $ini)
  $env:CK_MANAGER_FIXTURE = Join-Path $repo "$Build/manager-fixture"
  $env:CK_MANAGER_CAPTURE = $output
  $env:CK_MANAGER_THEME = 'obs30'
  $owned = Start-Process -FilePath $portableExe -WorkingDirectory (Join-Path $hostRoot 'bin/64bit') -ArgumentList '--multi --portable --collection "CorridorKey Test" --profile "CorridorKey Test" --disable-shutdown-check' -WindowStyle Hidden -PassThru
  $deadline = (Get-Date).AddSeconds(90)
  while (-not $owned.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1; $owned.Refresh() }
  if (-not $owned.HasExited -or -not (Test-Path -LiteralPath (Join-Path $output 'obs30-complete.txt'))) { throw 'Portable OBS capture did not complete.' }
  $log = Get-ChildItem -LiteralPath (Join-Path $config 'logs') -Filter '*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $repo '.cache/obs30-manager.log')
  Select-String -LiteralPath $log.FullName -Pattern '\[corridorkey|Number of memory leaks' | ForEach-Object Line
} finally {
  if ($owned -and -not $owned.HasExited) { $owned.CloseMainWindow() | Out-Null; if (-not $owned.WaitForExit(15000)) { throw 'Owned portable OBS is still running.' } }
  if (-not (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -eq $portableExe)) {
    if (Test-Path -LiteralPath (Join-Path $backup 'global.ini')) { Copy-Item -LiteralPath (Join-Path $backup 'global.ini') -Destination (Join-Path $config 'global.ini') -Force }
    # Verify the complete absolute directory before removing this test plugin's data.
    if ([System.IO.Path]::GetFullPath($data) -ne 'S:\Projects\obs-test\obs-30.2.0\data\obs-plugins\corridorkey') { throw 'Unexpected portable restore target.' }
    if ($pluginMutated) {
      if (Test-Path -LiteralPath $data) { Remove-Item -LiteralPath $data -Recurse -Force }
      if ($hadData) { Copy-Item -LiteralPath (Join-Path $backup 'data') -Destination $data -Recurse }
      if ($hadDll) { Copy-Item -LiteralPath (Join-Path $backup 'corridorkey.dll') -Destination $dll -Force } elseif (Test-Path -LiteralPath $dll) { Remove-Item -LiteralPath $dll }
    }
  }
  foreach ($name in @('CK_MANAGER_FIXTURE', 'CK_MANAGER_CAPTURE', 'CK_MANAGER_THEME')) { [System.Environment]::SetEnvironmentVariable($name, $null, 'Process') }
}
