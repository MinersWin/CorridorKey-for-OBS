# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OBSRoot,
    [string]$StageDir = '',
    [string]$PreviousStageDir = '',
    [string]$InnoCompiler = '',
    [string]$Version = '',
    [string]$PreviousVersion = '0.0.9',
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script:CurrentStep = 'preflight'
$smokeRegistry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CorridorKeyW19PackagingSmoke_is1'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-SmokeStep([string]$Name, [scriptblock]$Action) {
    $script:CurrentStep = $Name
    & $Action
    Write-Host "[PASS] $Name"
}

function Assert-OBSClosed {
    $running = @(Get-Process -Name obs64 -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        throw 'An obs64 process is active. Close OBS before running this test. No OBS process will be stopped.'
    }
}

function Resolve-Directory([string]$Path) {
    $item = Get-Item -LiteralPath $Path
    Assert-True $item.PSIsContainer "Not a directory: $Path"
    $cursor = $item
    while ($null -ne $cursor) {
        Assert-True (($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Reparse-point directory is not allowed: $($cursor.FullName)"
        $cursor = $cursor.Parent
    }
    return $item.FullName.TrimEnd('\')
}

function Assert-Inside([string]$Path, [string]$Root) {
    Assert-True ($Path.StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase)) "Path escapes test target: $Path"
}

function Assert-Stage([string]$Root) {
    $inventoryPath = Join-Path $Root 'data\BUNDLE_CONTENTS.json'
    Assert-True (Test-Path -LiteralPath $inventoryPath -PathType Leaf) 'Use a stage prepared by packaging/package.py, including BUNDLE_CONTENTS.json.'
    $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    Assert-True ($inventory.platform -eq 'windows-x64') 'The stage must be Windows x64.'
    $declared = @{}
    foreach ($entry in $inventory.files) {
        Assert-True ($entry.path.StartsWith('corridorkey/', [StringComparison]::Ordinal)) 'Unexpected inventory root.'
        $relative = $entry.path.Substring('corridorkey/'.Length).Replace('/', '\')
        $path = [IO.Path]::GetFullPath((Join-Path $Root $relative))
        Assert-Inside $path $Root
        $file = Get-Item -LiteralPath $path
        Assert-True (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) "Staged reparse point: $path"
        Assert-True ($file.Length -eq [long]$entry.bytes) "Staged size differs: $relative"
        Assert-True ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $entry.sha256) "Staged hash differs: $relative"
        $declared[$relative] = $true
    }
    $declared['data\BUNDLE_CONTENTS.json'] = $true
    $declared['data\THIRD_PARTY_NOTICES.md'] = $true
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File) {
        $relative = $file.FullName.Substring($Root.Length + 1)
        Assert-True ($declared.ContainsKey($relative)) "Unlisted staged file: $relative. Restage and rerun packaging/package.py."
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $Root 'corridorkey.dll') -PathType Leaf) 'Use the modern, flat installer stage.'
}

function Quote-NativeArgument([string]$Value) {
    Assert-True (-not $Value.Contains('"')) 'A native argument contains an unsupported quote.'
    return '"' + $Value + '"'
}

function Invoke-Installer([string]$Executable, [string]$LogName) {
    Assert-OBSClosed
    $log = Join-Path $evidenceRoot ($LogName + '.log')
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/PORTABLE=1',
        (Quote-NativeArgument ('/OBSDIR=' + $obsRootResolved)), (Quote-NativeArgument ('/LOG=' + $log)))
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Assert-True ($process.ExitCode -eq 0) "Installer exit $($process.ExitCode). See $log"
}

function Invoke-Uninstaller([string]$LogName) {
    Assert-OBSClosed
    $executable = Join-Path $pluginRoot 'unins000.exe'
    Assert-Inside ([IO.Path]::GetFullPath($executable)) $obsRootResolved
    Assert-True (Test-Path -LiteralPath $executable -PathType Leaf) 'The test uninstaller is missing.'
    $log = Join-Path $evidenceRoot ($LogName + '.log')
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', (Quote-NativeArgument ('/LOG=' + $log)))
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    Assert-True ($process.ExitCode -eq 0) "Uninstaller exit $($process.ExitCode). See $log"
}

function Assert-Preserved {
    Assert-True ((Get-FileHash -LiteralPath $cacheMarker -Algorithm SHA256).Hash -eq $cacheHash) 'The model/runtime cache marker changed.'
    Assert-True ((Get-FileHash -LiteralPath $settingsMarker -Algorithm SHA256).Hash -eq $settingsHash) 'The config marker changed.'
}

function Assert-Installed([string]$ExpectedStage, [string]$ExpectedVersion) {
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $pluginRoot 'corridorkey.dll') -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath (Join-Path $ExpectedStage 'corridorkey.dll') -Algorithm SHA256).Hash) 'Installed module differs from the selected payload.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'bin\64bit\corridorkey.dll'))) 'The alternate legacy DLL still exists.'
    $registered = Get-ItemProperty -LiteralPath $smokeRegistry
    Assert-True ($registered.DisplayVersion -eq $ExpectedVersion) 'The isolated test registration has the wrong version.'
    Assert-True ($registered.InstallLocation.TrimEnd('\') -eq $pluginRoot) 'The isolated test registration points elsewhere.'
    Assert-Preserved
}

function Assert-Removed {
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'corridorkey.dll'))) 'The modern module was not removed.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'bin\64bit\corridorkey.dll'))) 'The legacy module was not removed.'
    Assert-True (-not (Test-Path -LiteralPath $smokeRegistry)) 'The isolated uninstall registration remains.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'data\tools\corridorkey_backend_probe.exe'))) 'The backend helper remains installed.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $pluginRoot 'data\tools\corridorkey-runtime.exe'))) 'The runtime installer remains installed.'
    $libraries = @(Get-ChildItem -LiteralPath $pluginRoot -Filter '*.dll' -Recurse -File -ErrorAction SilentlyContinue)
    Assert-True ($libraries.Count -eq 0) 'A private runtime DLL remains in the plugin target.'
    Assert-Preserved
}

try {
    Invoke-SmokeStep 'preflight: OBS closed, portable host and verified stages' {
        Assert-OBSClosed
        Assert-True ([Environment]::Is64BitProcess) 'Run this script in 64-bit PowerShell.'
        $script:obsRootResolved = Resolve-Directory $OBSRoot
        Assert-True ($obsRootResolved -ine (Join-Path $env:ProgramFiles 'obs-studio')) 'Use a dedicated portable copy, not the installed OBS.'
        Assert-True (Test-Path -LiteralPath (Join-Path $obsRootResolved 'portable_mode.txt') -PathType Leaf) 'The dedicated OBS copy needs portable_mode.txt.'
        $obsExecutable = Join-Path $obsRootResolved 'bin\64bit\obs64.exe'
        Assert-True ((Get-Item -LiteralPath $obsExecutable).VersionInfo.FileMajorPart -ge 33) 'This test targets the OBS 33+ portable layout.'
        $script:pluginRoot = [IO.Path]::GetFullPath((Join-Path $obsRootResolved 'plugins\corridorkey'))
        Assert-Inside $pluginRoot $obsRootResolved
        if (Test-Path -LiteralPath $pluginRoot) { $null = Resolve-Directory $pluginRoot }
        if ([string]::IsNullOrEmpty($StageDir)) { $StageDir = Join-Path $repoRoot 'stage\corridorkey' }
        $script:currentStage = Resolve-Directory $StageDir
        if ([string]::IsNullOrEmpty($PreviousStageDir)) {
            $PreviousStageDir = $currentStage
            Write-Host '[INFO] Prior-version fixture uses the current payload. This tests installer mechanics, not historical config-schema migration.'
        }
        $script:oldStage = Resolve-Directory $PreviousStageDir
        Assert-Stage $currentStage
        Assert-Stage $oldStage
        if ([string]::IsNullOrEmpty($Version)) {
            $script:Version = (Get-Content -LiteralPath (Join-Path $repoRoot 'buildspec.json') -Raw | ConvertFrom-Json).version
        }
        Assert-True ($Version -match '^\d+\.\d+\.\d+$' -and $PreviousVersion -match '^\d+\.\d+\.\d+$') 'Fixture versions must be numeric x.y.z versions.'
        Assert-True ([version]$PreviousVersion -lt [version]$Version) 'PreviousVersion must precede Version.'
        if ([string]::IsNullOrEmpty($InnoCompiler)) { $InnoCompiler = Join-Path $repoRoot '.cache\innosetup\ISCC.exe' }
        $script:compiler = (Get-Item -LiteralPath $InnoCompiler).FullName
        if (Test-Path -LiteralPath $smokeRegistry) {
            $registered = Get-ItemProperty -LiteralPath $smokeRegistry
            Assert-True ($registered.InstallLocation.TrimEnd('\') -eq $pluginRoot) 'Another portable test owns the smoke registration. Finish that test first.'
        } elseif (Test-Path -LiteralPath (Join-Path $pluginRoot 'corridorkey.dll')) {
            throw 'An unrecognised/manual plugin exists in this portable copy. Use a clean copy or finish its install separately.'
        }
    }
    if ($PreflightOnly) {
        Write-Host '[PASS] Preflight only; no installer, cache marker or registry change was started.'
        exit 0
    }
    Invoke-SmokeStep 'compile isolated current and previous installer fixtures' {
        $script:evidenceRoot = Join-Path $repoRoot ('.cache\packaging-smoke\' + [guid]::NewGuid().ToString('N'))
        Assert-Inside ([IO.Path]::GetFullPath($evidenceRoot)) $repoRoot
        $null = New-Item -ItemType Directory -Path $evidenceRoot -Force
        $source = Join-Path $repoRoot 'packaging\windows\installer.iss'
        foreach ($fixture in @(@{ Version = $Version; Stage = $currentStage }, @{ Version = $PreviousVersion; Stage = $oldStage })) {
            $arguments = @('/DPackagingSmoke=1', ('/DPluginVersion=' + $fixture.Version),
                ('/DStageDir=' + $fixture.Stage), ('/DOutputPath=' + $evidenceRoot), $source)
            & $compiler @arguments *> (Join-Path $evidenceRoot ('compile-' + $fixture.Version + '.log'))
            Assert-True ($LASTEXITCODE -eq 0) "Inno compilation failed for $($fixture.Version)."
        }
        $script:currentInstaller = Join-Path $evidenceRoot "corridorkey-$Version-windows-x64-setup.exe"
        $script:oldInstaller = Join-Path $evidenceRoot "corridorkey-$PreviousVersion-windows-x64-setup.exe"
        Assert-OBSClosed
        $config = Join-Path $obsRootResolved 'config\obs-studio\plugin_config\corridorkey'
        $cache = Join-Path $config 'models'
        Assert-Inside ([IO.Path]::GetFullPath($cache)) $obsRootResolved
        if (Test-Path -LiteralPath $config) { $null = Resolve-Directory $config }
        if (Test-Path -LiteralPath $cache) { $null = Resolve-Directory $cache }
        $null = New-Item -ItemType Directory -Path $cache -Force
        $markerId = [guid]::NewGuid().ToString('N')
        $script:cacheMarker = Join-Path $cache "w19-smoke-$markerId.txt"
        $script:settingsMarker = Join-Path $config "w19-smoke-$markerId.json"
        Set-Content -LiteralPath $cacheMarker -Value $markerId -Encoding UTF8
        Set-Content -LiteralPath $settingsMarker -Value ('{"smoke":"' + $markerId + '"}') -Encoding UTF8
        $script:cacheHash = (Get-FileHash -LiteralPath $cacheMarker -Algorithm SHA256).Hash
        $script:settingsHash = (Get-FileHash -LiteralPath $settingsMarker -Algorithm SHA256).Hash
    }
    Invoke-SmokeStep 'remove an existing isolated fixture, preserving cache and config' {
        if (Test-Path -LiteralPath $smokeRegistry) { Invoke-Uninstaller 'remove-existing-fixture' }
        Assert-Removed
    }
    Invoke-SmokeStep 'clean install previous fixture' {
        Invoke-Installer $oldInstaller 'install-previous'
        Assert-Installed $oldStage $PreviousVersion
    }
    Invoke-SmokeStep 'upgrade, remove alternate layout and preserve cache/config' {
        Assert-OBSClosed
        $legacy = Join-Path $pluginRoot 'bin\64bit'
        Assert-Inside ([IO.Path]::GetFullPath($legacy)) $obsRootResolved
        $null = New-Item -ItemType Directory -Path $legacy -Force
        Set-Content -LiteralPath (Join-Path $legacy 'corridorkey.dll') -Value 'alternate-layout test marker' -Encoding ASCII
        Invoke-Installer $currentInstaller 'upgrade-current'
        Assert-Installed $currentStage $Version
    }
    Invoke-SmokeStep 'silent uninstall current fixture, preserving cache/config' {
        Invoke-Uninstaller 'uninstall-current'
        Assert-Removed
    }
    Invoke-SmokeStep 'rollback to previous fixture with cache/config retained' {
        Invoke-Installer $oldInstaller 'rollback-previous'
        Assert-Installed $oldStage $PreviousVersion
    }
    Invoke-SmokeStep 'final uninstall leaves no module or test registration' {
        Invoke-Uninstaller 'uninstall-rollback'
        Assert-Removed
        Remove-Item -LiteralPath $cacheMarker
        Remove-Item -LiteralPath $settingsMarker
    }
    Write-Host "[PASS] All installer checks completed. Evidence: $evidenceRoot"
    Write-Host '[INFO] Interactive cache deletion and OBS module loading are separate checks; this script tests silent cache preservation and installed payloads.'
    exit 0
} catch {
    Write-Host "[FAIL] $script:CurrentStep - $($_.Exception.Message)"
    if (Get-Variable -Name evidenceRoot -Scope Script -ErrorAction SilentlyContinue) { Write-Host "Evidence: $evidenceRoot" }
    Write-Host '[INFO] Test stopped at the first failure. Existing config/cache and evidence are retained for inspection.'
    exit 1
}
