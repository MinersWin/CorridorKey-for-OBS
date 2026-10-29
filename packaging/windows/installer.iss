; SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PluginVersion
  #error PluginVersion must be supplied by the release workflow
#endif
#ifndef StageDir
  #define StageDir "..\..\stage\corridorkey"
#endif
#ifndef OutputPath
  #define OutputPath "..\..\release"
#endif

[Setup]
#ifdef PackagingSmoke
AppId=CorridorKeyW19PackagingSmoke
PrivilegesRequired=lowest
#else
AppId={{62E527EB-051B-4671-8FE4-F20973544792}
PrivilegesRequired=admin
#endif
AppName=CorridorKey for OBS
AppVersion={#PluginVersion}
AppPublisher=Moritz Mantel
AppPublisherURL=https://github.com/MinersWin/CorridorKey-for-OBS
DefaultDirName={commonappdata}\obs-studio\plugins\corridorkey
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputPath}
OutputBaseFilename=corridorkey-{#PluginVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
Uninstallable=yes
UninstallDisplayName=CorridorKey for OBS
LicenseFile=..\..\LICENSE
CloseApplications=no
RestartApplications=no
SetupMutex=CorridorKeyInstaller
WizardStyle=modern
UsePreviousAppDir=no
AppComments=Models and optional NVIDIA runtimes download separately.

[Files]
Source: "{#StageDir}\corridorkey.dll"; DestDir: "{app}"; Check: UseModernLayout; Flags: ignoreversion
Source: "{#StageDir}\corridorkey.dll"; DestDir: "{app}\bin\64bit"; Check: UseLegacyLayout; Flags: ignoreversion
Source: "{#StageDir}\corridorkey.pdb"; DestDir: "{app}"; Check: UseModernLayout; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#StageDir}\corridorkey.pdb"; DestDir: "{app}\bin\64bit"; Check: UseLegacyLayout; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#StageDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\corridorkey.dll"
Type: files; Name: "{app}\corridorkey.pdb"
Type: files; Name: "{app}\bin\64bit\corridorkey.dll"
Type: files; Name: "{app}\bin\64bit\corridorkey.pdb"
; Do not remove unrelated manual/portable installations outside this target.

[Code]
var
  HostPage: TInputOptionWizardPage;
  OBSPage: TInputDirWizardPage;
  Portable: Boolean;
  CachePath: String;

function DetectOBS(): String;
begin
  Result := ExpandConstant('{autopf}\obs-studio');
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    Result := ExpandConstant('{autopf}\obs-studio');
  Result := RemoveBackslash(ExpandConstant('{param:OBSDIR|' + Result + '}'));
end;

function UseModernLayout(): Boolean;
begin
  Result := HostPage.SelectedValueIndex = 0;
end;

function UseLegacyLayout(): Boolean;
begin
  Result := not UseModernLayout();
end;

procedure InitializeWizard();
var
  Root, Version: String;
  Major: Integer;
begin
  Portable := ExpandConstant('{param:PORTABLE|0}') = '1';
  OBSPage := CreateInputDirPage(wpLicense, 'OBS Studio installation',
    'Select the folder containing bin\64bit\obs64.exe.',
    'Close OBS before continuing. For portable OBS, use the manual zip instructions in the install guide.', False, '');
  OBSPage.Add('OBS Studio folder:');
  OBSPage.Values[0] := DetectOBS();
  HostPage := CreateInputOptionPage(OBSPage.ID, 'OBS Studio version',
    'Choose the OBS installation that will use CorridorKey.',
    'The default OBS installation is detected when available. For a custom installation, select its version range. Portable installations use the zip package.',
    True, False);
  HostPage.Add('OBS Studio 33 or newer');
  HostPage.Add('OBS Studio 30.2 to 32');
  HostPage.SelectedValueIndex := 0;
  Root := OBSPage.Values[0];
  if GetVersionNumbersString(Root + '\bin\64bit\obs64.exe', Version) then begin
    Major := StrToIntDef(Copy(Version, 1, Pos('.', Version) - 1), 33);
    if Major < 33 then HostPage.SelectedValueIndex := 1;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Version, Root: String;
  Major: Integer;
begin
  Result := True;
  if CurPageID = OBSPage.ID then begin
    Root := RemoveBackslash(OBSPage.Values[0]);
    if not GetVersionNumbersString(Root + '\bin\64bit\obs64.exe', Version) then begin
      MsgBox('The selected folder does not contain OBS Studio x64.', mbError, MB_OK);
      Result := False;
      Exit;
    end;
    Major := StrToIntDef(Copy(Version, 1, Pos('.', Version) - 1), 33);
    if Major < 33 then HostPage.SelectedValueIndex := 1
    else HostPage.SelectedValueIndex := 0;
    if FileExists(Root + '\portable_mode.txt') or DirExists(Root + '\config\obs-studio') then
      Portable := True;
    if Portable then begin
      if Major < 33 then begin
        MsgBox('Portable OBS before 33 uses the legacy zip. Follow the manual install guide.', mbError, MB_OK);
        Result := False;
        Exit;
      end;
      WizardForm.DirEdit.Text := Root + '\plugins\corridorkey';
    end else
      WizardForm.DirEdit.Text := ExpandConstant('{commonappdata}\obs-studio\plugins\corridorkey');
  end;
end;

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
var
  Root: String;
begin
  Result := '';
  if OBSIsRunning() then
    Result := 'Close OBS Studio before installing CorridorKey.';
  Root := RemoveBackslash(OBSPage.Values[0]);
  if not FileExists(Root + '\bin\64bit\obs64.exe') then
    Result := 'Select a valid OBS Studio x64 installation.';
  if FileExists(Root + '\obs-plugins\64bit\corridorkey.dll') then
    Result := 'A manual CorridorKey copy exists in OBS obs-plugins\64bit. Back it up and remove it before installing, to prevent duplicate modules.';
  if Portable then begin
    if UseLegacyLayout() then
      Result := 'Portable OBS before 33 requires the legacy zip.';
    WizardForm.DirEdit.Text := Root + '\plugins\corridorkey';
    CachePath := Root + '\config\obs-studio\plugin_config\corridorkey\models';
  end else
    CachePath := ExpandConstant('{userappdata}\obs-studio\plugin_config\corridorkey\models');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    SetIniString('CorridorKey', 'CachePath', CachePath, ExpandConstant('{app}\install-state.ini'));
end;

function InitializeUninstall(): Boolean;
begin
  Result := not OBSIsRunning();
  if not Result then
    MsgBox('Close OBS Studio before uninstalling CorridorKey.', mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path, Expected, Root: String;
begin
  if CurUninstallStep = usUninstall then begin
    if OBSIsRunning() then
      RaiseException('Close OBS Studio before uninstalling CorridorKey. No cache removal has started.');
    Path := GetIniString('CorridorKey', 'CachePath', '', ExpandConstant('{app}\install-state.ini'));
    Expected := ExpandConstant('{userappdata}\obs-studio\plugin_config\corridorkey\models');
    Root := ExtractFileDir(ExtractFileDir(ExpandConstant('{app}')));
    if DirExists(Root + '\config\obs-studio') then
      Expected := Root + '\config\obs-studio\plugin_config\corridorkey\models';
    { Silent uninstall always preserves downloads; only an interactive Yes removes them. }
    if (not UninstallSilent) and (CompareText(Path, Expected) = 0) and DirExists(Path) then
      if MsgBox('Remove downloaded CorridorKey models and runtime cache at:' + #13#10 + Path + #13#10 +
        'Choose No to keep them for a later install. OBS scenes and settings are kept.',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then begin
        if OBSIsRunning() then
          RaiseException('OBS Studio started while the uninstaller was open. Close OBS and run the uninstaller again.');
        DelTree(Path, True, True, True);
      end;
    if OBSIsRunning() then
      RaiseException('OBS Studio started while the uninstaller was open. Close OBS and run the uninstaller again.');
    DeleteFile(ExpandConstant('{app}\install-state.ini'));
  end;
end;
