# SPDX-License-Identifier: GPL-3.0-or-later
param(
  [string]$Build = 'build_x64',
  [string]$PortableRoot = 'S:\Projects\obs-test\obs-33.0.0-w17',
  [string]$Output = 'docs/images/diagnostics'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$installed = 'C:\Program Files\obs-studio'
$PortableRoot = [IO.Path]::GetFullPath($PortableRoot)
if (-not $PortableRoot.StartsWith('S:\Projects\obs-test\obs-', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Use a dedicated portable OBS beneath S:\Projects\obs-test\obs-.'
}
$exe = Join-Path $PortableRoot 'bin/64bit/obs64.exe'
if (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -eq $exe) {
  throw 'The selected portable OBS is already running. Do not interrupt it.'
}
New-Item -ItemType Directory -Force -Path $PortableRoot | Out-Null
$lockPath = Join-Path $PortableRoot 'w17-capture.lock'
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$owned = $null
$pluginMutated = $false
$plugin = Join-Path $PortableRoot 'plugins/corridorkey'
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
try {
  if (Test-Path -LiteralPath $plugin) { throw 'Refusing to replace an existing portable test plugin.' }
  if (-not (Test-Path -LiteralPath $exe)) {
    foreach ($directory in @('bin', 'core', 'data', 'obs-plugins')) {
      $source = Join-Path $installed $directory
      if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $PortableRoot -Recurse }
    }
  }
  $config = Join-Path $PortableRoot 'config/obs-studio'
  New-Item -ItemType Directory -Force -Path $config, (Join-Path $config 'basic/scenes'), $plugin, $outputPath | Out-Null
  $pluginMutated = $true
  # Reuse initialized test preferences only when creating this isolated profile.
  foreach ($file in @('user.ini', 'global.ini')) {
    $destination = Join-Path $config $file
    if (-not (Test-Path -LiteralPath $destination)) {
      Copy-Item -LiteralPath (Join-Path 'S:\Projects\obs-test\obs-33.0.0-w07\config\obs-studio' $file) -Destination $destination
    }
  }
  $scene = Join-Path $config 'basic/scenes/CorridorKey_Test.json'
  if (-not (Test-Path -LiteralPath $scene)) {
    $empty = @{name='CorridorKey Test'; current_scene='CorridorKey Test'; current_program_scene='CorridorKey Test'; scene_order=@(@{name='CorridorKey Test'}); sources=@(@{name='CorridorKey Test'; id='scene'; settings=@{items=@()}})}
    [IO.File]::WriteAllText($scene, ($empty | ConvertTo-Json -Depth 10))
  }
  Copy-Item -LiteralPath (Join-Path $repo "$Build/RelWithDebInfo/corridorkey.dll") -Destination (Join-Path $plugin 'corridorkey.dll')
  Copy-Item -LiteralPath (Join-Path $repo 'data') -Destination $plugin -Recurse
  $env:CK_DIAGNOSTICS_CAPTURE = $outputPath
  $env:QT_SCALE_FACTOR = '0.75'
  $env:CK_BINDER_EXPORTS = Join-Path $repo "$Build/no-local-models"
  $done = Join-Path $outputPath 'complete.txt'
  if (Test-Path -LiteralPath $done) { Remove-Item -LiteralPath $done }
  $owned = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -ArgumentList '--multi --portable --collection "CorridorKey Test" --disable-shutdown-check' -WindowStyle Hidden -PassThru
  $deadline = (Get-Date).AddSeconds(90)
  while (-not $owned.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1; $owned.Refresh() }
  if (-not $owned.HasExited) { throw 'Owned capture host did not finish in 90 seconds.' }
  $log = Get-ChildItem -LiteralPath (Join-Path $config 'logs') -Filter '*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  New-Item -ItemType Directory -Force -Path (Join-Path $repo '.cache') | Out-Null
  Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $repo '.cache/diagnostics-obs33.log') -Force
  Select-String -LiteralPath $log.FullName -Pattern '\[corridorkey|Number of memory leaks' | ForEach-Object Line
  if (-not (Test-Path -LiteralPath $done)) { throw 'Diagnostics capture did not complete; inspect the local OBS log.' }
} finally {
  if ($owned -and -not $owned.HasExited) {
    $owned.CloseMainWindow() | Out-Null
    if (-not $owned.WaitForExit(15000)) { throw 'Owned OBS has not exited; leave its test files in place until it stops.' }
  }
  if (-not (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -eq $exe)) {
    $target = [IO.Path]::GetFullPath($plugin)
    if ($target -ne (Join-Path $PortableRoot 'plugins\corridorkey')) { throw 'Unexpected cleanup target.' }
    if ($pluginMutated -and (Test-Path -LiteralPath $target)) { Remove-Item -LiteralPath $target -Recurse -Force }
  }
  $lock.Dispose()
  Remove-Item -LiteralPath $lockPath
  foreach ($key in @('CK_DIAGNOSTICS_CAPTURE', 'QT_SCALE_FACTOR', 'CK_BINDER_EXPORTS')) {
    [Environment]::SetEnvironmentVariable($key, $null, 'Process')
  }
}
