; SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PluginVersion
  #error PluginVersion must be supplied by the release workflow
#endif
#ifndef StageDir
  #define StageDir "..\..\stage\corridorkey"
#endif

[Setup]
AppId={{62E527EB-051B-4671-8FE4-F20973544792}
AppName=CorridorKey for OBS
AppVersion={#PluginVersion}
AppPublisher=Moritz Mantel
AppPublisherURL=https://github.com/MinersWin/CorridorKey-for-OBS
DefaultDirName={commonappdata}\obs-studio\plugins\corridorkey
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\release
OutputBaseFilename=corridorkey-{#PluginVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
Uninstallable=yes
UninstallDisplayName=CorridorKey for OBS
LicenseFile=..\..\LICENSE
CloseApplications=no
RestartApplications=no
SetupMutex=CorridorKeyInstaller

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function OBSIsRunning(): Boolean;
var
  Locator, Services, Processes: Variant;
begin
  { Fail closed if process enumeration is unavailable. Never terminate OBS. }
  Result := True;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Processes.Count > 0;
  except
    MsgBox('Unable to check whether OBS Studio is running. Close OBS and try again.', mbError, MB_OK);
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := not OBSIsRunning();
  if not Result then
    MsgBox('Close OBS Studio before installing CorridorKey.', mbError, MB_OK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if OBSIsRunning() then
    Result := 'Close OBS Studio before installing CorridorKey.';
end;

function InitializeUninstall(): Boolean;
begin
  Result := not OBSIsRunning();
  if not Result then
    MsgBox('Close OBS Studio before uninstalling CorridorKey.', mbError, MB_OK);
end;
