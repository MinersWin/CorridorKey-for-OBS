# SPDX-License-Identifier: GPL-3.0-or-later
param(
  [string]$Build = 'build_x64',
  [string]$Output = 'docs/images/model-manager',
  [string[]]$Themes = @('yami', 'yami-light'),
  [string]$Scale = '1',
  [string]$PortableRoot = '',
  [string]$ProbeTools = '',
  [double]$HostScale = 1,
  [switch]$Diagnostics
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$obsExe = 'C:\Program Files\obs-studio\bin\64bit\obs64.exe'
$plugin = 'C:\ProgramData\obs-studio\plugins\corridorkey'
$config = Join-Path $env:APPDATA 'obs-studio'
$lockPath = 'S:\Projects\obs-test\installed-obs.lock'
$lock = $null
if ($PortableRoot) {
  $PortableRoot = (Resolve-Path -LiteralPath $PortableRoot).Path
  if (-not $PortableRoot.StartsWith('S:\Projects\obs-test\obs-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use an isolated OBS test copy beneath S:\Projects\obs-test\obs-.' }
  $obsExe = Join-Path $PortableRoot 'bin/64bit/obs64.exe'
  $plugin = Join-Path $PortableRoot 'plugins/corridorkey'
  $config = Join-Path $PortableRoot 'config/obs-studio'
  if (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object Path -eq $obsExe) { throw 'The selected portable OBS is already running.' }
} else {
  if (Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'OBS is already running. Do not interrupt it.' }
  $lock = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
  $bytes = [System.Text.Encoding]::UTF8.GetBytes('d1-design')
  $lock.Write($bytes, 0, $bytes.Length)
}
# Refuse a profile that can open listeners or browser networking during capture.
$websocket = Join-Path $config 'plugin_config/obs-websocket/config.json'
if (-not (Test-Path -LiteralPath $websocket) -or
    (Get-Content -Raw -LiteralPath $websocket | ConvertFrom-Json).server_enabled -ne $false) {
  if ($lock) { $lock.Dispose(); Remove-Item -LiteralPath $lockPath }
  throw 'Disable obs-websocket in the dedicated test profile before capturing.'
}
$scene = Join-Path $config 'basic/scenes/CorridorKey_Test.json'
if (-not (Test-Path -LiteralPath $scene) -or (Get-Content -Raw -LiteralPath $scene) -match '"browser_source"') {
  if ($lock) { $lock.Dispose(); Remove-Item -LiteralPath $lockPath }
  throw 'Use the CorridorKey Test collection without browser sources.'
}
$backup = Join-Path $repo ('.cache/obs-capture-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repo $Output))
$pluginExisted = Test-Path -LiteralPath $plugin
$pluginMutated = $false
$owned = $null
try {
  New-Item -ItemType Directory -Force -Path $backup, $outputPath | Out-Null
  foreach ($file in @('user.ini', 'global.ini')) {
    Copy-Item -LiteralPath (Join-Path $config $file) -Destination (Join-Path $backup $file)
  }
  if ($pluginExisted) { Copy-Item -LiteralPath $plugin -Destination (Join-Path $backup 'plugin') -Recurse }
  $pluginMutated = $true
  New-Item -ItemType Directory -Force -Path $plugin | Out-Null
  Copy-Item -LiteralPath (Join-Path $repo "$Build/RelWithDebInfo/corridorkey.dll") -Destination (Join-Path $plugin 'corridorkey.dll')
  Copy-Item -LiteralPath (Join-Path $repo 'data') -Destination $plugin -Recurse -Force
  if ($ProbeTools) {
    Copy-Item -LiteralPath $ProbeTools -Destination (Join-Path $plugin 'data/tools') -Recurse -Force
  }
  $env:QT_SCREEN_SCALE_FACTORS = '1'
  $env:CK_MANAGER_FIXTURE = Join-Path $repo "$Build/manager-fixture"
  $env:CK_MANAGER_CAPTURE = $outputPath
  if ($Diagnostics) { $env:CK_DIAGNOSTICS_CAPTURE = $outputPath }
  $env:QT_SCALE_FACTOR = ([double]$Scale / $HostScale).ToString([Globalization.CultureInfo]::InvariantCulture)
  foreach ($theme in $Themes) {
    if (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { -not $PortableRoot -or $_.Path -eq $obsExe }) { throw 'Another OBS process appeared in the selected host.' }
    $themeId = if ($theme -eq 'yami-light') { 'com.obsproject.Yami.Light' } else { 'com.obsproject.Yami.Original' }
    $ini = [System.IO.File]::ReadAllText((Join-Path $backup 'user.ini'))
    $ini = [regex]::Replace($ini, '(?m)^Theme=.*\r?\n', '')
    $ini = $ini.Replace('[Appearance]', "[Appearance]`r`nTheme=$themeId")
    [System.IO.File]::WriteAllText((Join-Path $config 'user.ini'), $ini)
    $env:CK_MANAGER_THEME = if ($Scale -eq '1') { $theme } else { "$theme-$Scale" }
    $done = Join-Path $outputPath "$env:CK_MANAGER_THEME-complete.txt"
    if (Test-Path -LiteralPath $done) { Remove-Item -LiteralPath $done }
    $arguments = '--collection "CorridorKey Test" --disable-shutdown-check'
    if ($PortableRoot) { $arguments = '--multi --portable --profile "CorridorKey Test" ' + $arguments }
    $owned = Start-Process -FilePath $obsExe -WorkingDirectory (Split-Path $obsExe) -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(90)
    while (-not $owned.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1; $owned.Refresh() }
    if (-not $owned.HasExited) { throw 'The owned OBS capture process did not finish within 90 seconds.' }
    if (-not (Test-Path -LiteralPath $done)) { throw "Capture did not complete for $theme." }
    $log = Get-ChildItem -LiteralPath (Join-Path $config 'logs') -Filter '*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $repo ".cache/$env:CK_MANAGER_THEME-obs.log")
    Select-String -LiteralPath $log.FullName -Pattern '\[corridorkey|Number of memory leaks' | ForEach-Object Line
    $owned = $null
  }
} finally {
  if ($owned -and -not $owned.HasExited) {
    $owned.CloseMainWindow() | Out-Null
    if (-not $owned.WaitForExit(15000)) { throw 'Owned OBS has not exited; restore the test plugin only after it stops.' }
  }
  if (-not (Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { -not $PortableRoot -or $_.Path -eq $obsExe })) {
    foreach ($file in @('user.ini', 'global.ini')) {
      $saved = Join-Path $backup $file
      if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination (Join-Path $config $file) -Force }
    }
    if ($pluginMutated) {
      # Verify the complete absolute test-plugin directory before recursive removal.
      $resolvedPlugin = [System.IO.Path]::GetFullPath($plugin)
      if ($PortableRoot) {
        if ($resolvedPlugin -ne (Join-Path $PortableRoot 'plugins/corridorkey').Replace('/', '\')) { throw 'Unexpected portable plugin restore target.' }
      } elseif ($resolvedPlugin -ne 'C:\ProgramData\obs-studio\plugins\corridorkey') { throw 'Unexpected plugin restore target.' }
      if (Test-Path -LiteralPath $plugin) { Remove-Item -LiteralPath $plugin -Recurse -Force }
      if ($pluginExisted -and (Test-Path -LiteralPath (Join-Path $backup 'plugin'))) {
        Copy-Item -LiteralPath (Join-Path $backup 'plugin') -Destination $plugin -Recurse
      }
    }
  }
  if ($lock) { $lock.Dispose(); Remove-Item -LiteralPath $lockPath }
  foreach ($name in @('CK_MANAGER_FIXTURE', 'CK_MANAGER_CAPTURE', 'CK_MANAGER_THEME', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'CK_DIAGNOSTICS_CAPTURE')) {
    [System.Environment]::SetEnvironmentVariable($name, $null, 'Process')
  }
}
