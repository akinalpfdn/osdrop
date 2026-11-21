; Osdrop installer for Windows (DEVPLAN Phase 19). scripts/windows/Build-Installer.ps1 stages the
; files and compiles this script; see docs/BUILDING.md. It installs the app, the OsdropService
; service (which keeps OsdropInput running in the signed-in user's session) and a firewall rule;
; the uninstaller removes all of them and puts the system cursors back.

#ifndef AppVersion
  #error Define AppVersion, for example /DAppVersion=0.1.0
#endif
#ifndef StageDir
  #error Define StageDir: the folder Build-Installer.ps1 stages the files in
#endif
#ifndef OutputDir
  #define OutputDir AddBackslash(SourcePath) + "..\build\installer"
#endif
#define RepoDir AddBackslash(SourcePath) + "..\.."

[Setup]
; Never change AppId: upgrades and the uninstaller find an installed Osdrop by it.
AppId={{12382C6D-FD30-4A37-B7A1-D9A6379E2A9C}
AppName=Osdrop
AppVersion={#AppVersion}
AppVerName=Osdrop {#AppVersion}
AppPublisher=Akinalp Fidan
AppPublisherURL=https://github.com/akinalpfdn/osdrop
AppSupportURL=https://github.com/akinalpfdn/osdrop/issues
AppUpdatesURL=https://github.com/akinalpfdn/osdrop/releases
AppCopyright=Copyright (c) 2026 Akinalp Fidan
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Osdrop
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
; Windows 10 22H2 or later.
MinVersion=10.0.19045
LicenseFile={#StageDir}\licenses\LICENSE.txt
SetupIconFile={#RepoDir}\windows\app\Osdrop.App\Assets\Osdrop.ico
UninstallDisplayIcon={app}\Osdrop.exe
UninstallDisplayName=Osdrop
OutputDir={#OutputDir}
; No version in the name, so releases/latest/download/Osdrop-Setup.exe always works.
OutputBaseFilename=Osdrop-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A running Osdrop is closed when its files are replaced; the service is stopped before.
CloseApplications=force
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl,Osdrop.en.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl,Osdrop.tr.isl"

[Files]
Source: "{#StageDir}\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Osdrop"; Filename: "{app}\Osdrop.exe"

[Run]
Filename: "{app}\Osdrop.exe"; Description: "{cm:LaunchProgram,Osdrop}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
; The service's and the input process's logs.
Type: filesandordirs; Name: "{commonappdata}\Osdrop"

[Code]
const
  FirewallRuleName = 'Osdrop';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  SPI_SETCURSORS = $0057;

function SystemParametersInfo(Action, Param: Cardinal; Value: Cardinal; WinIni: Cardinal): Boolean;
  external 'SystemParametersInfoW@user32.dll stdcall';

{ Runs a program without a window and waits for it. Returns its exit code, or -1 when it could not
  be started. Both are written to the setup log. }
function RunHidden(const FileName, Params: String): Integer;
var
  ResultCode: Integer;
begin
  Log('Running: ' + FileName + ' ' + Params);
  if Exec(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := ResultCode;
    Log('Exit code: ' + IntToStr(Result));
  end
  else
  begin
    Result := -1;
    Log('Could not start: ' + SysErrorMessage(ResultCode));
  end;
end;

function ServicePath: String;
begin
  Result := ExpandConstant('{app}\OsdropService.exe');
end;

function NetshPath: String;
begin
  Result := ExpandConstant('{sysnative}\netsh.exe');
end;

procedure ReportFailure(const MessageName: String; Code: Integer);
begin
  SuppressibleMsgBox(FmtMessage(CustomMessage(MessageName), [IntToStr(Code)]), mbError, MB_OK, IDOK);
end;

{ The service starts OsdropInput in the signed-in user's session and restarts it if it ends. }
procedure InstallService;
var
  Code: Integer;
begin
  Code := RunHidden(ServicePath, '--install');
  if Code <> 0 then
    ReportFailure('ServiceSetupFailed', Code);
end;

{ Every profile: Windows marks new networks Public, and pairing, not the network, decides who may
  connect. The listener lives in OsdropEngine.dll, inside Osdrop.exe. }
procedure AddFirewallRule;
var
  Code: Integer;
begin
  { Deleting first keeps a single rule across upgrades; it fails harmlessly when there is none. }
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  Code := RunHidden(NetshPath, 'advfirewall firewall add rule name="' + FirewallRuleName +
    '" dir=in action=allow enable=yes profile=any program="' +
    ExpandConstant('{app}\Osdrop.exe') + '"');
  if Code <> 0 then
    ReportFailure('FirewallSetupFailed', Code);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  { An upgrade: stop the old service, which ends OsdropInput, so their files can be replaced. }
  if FileExists(ServicePath) then
    RunHidden(ServicePath, '--stop');
end;

{ "Start with Windows" names the Osdrop.exe that turned it on. Pointing an existing entry at this
  install keeps the user's choice and stops a copy from another folder (a development build, a
  moved install) starting after every sign-in instead of this one. }
procedure RepointStartup;
var
  Current, Wanted: String;
begin
  Wanted := '"' + ExpandConstant('{app}\Osdrop.exe') + '"';
  if RegQueryStringValue(HKEY_CURRENT_USER, RunKey, 'Osdrop', Current) and
     (CompareText(Current, Wanted) <> 0) then
  begin
    Log('Start with Windows pointed at ' + Current);
    RegWriteStringValue(HKEY_CURRENT_USER, RunKey, 'Osdrop', Wanted);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    RepointStartup;
    InstallService;
    AddFirewallRule;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  { A running Osdrop holds its files and the input pipe. }
  RunHidden(ExpandConstant('{sysnative}\taskkill.exe'), '/F /IM Osdrop.exe');
  { Stops OsdropInput, which shows the cursors again, and removes the service. }
  RunHidden(ServicePath, '--uninstall');
  RunHidden(ExpandConstant('{sysnative}\taskkill.exe'), '/F /IM OsdropInput.exe');
  { An OsdropInput ended with the cursors hidden would leave them so; this loads them again. }
  if not SystemParametersInfo(SPI_SETCURSORS, 0, 0, 0) then
    Log('Loading the system cursors again failed');
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  { "Start with Windows" in Osdrop's settings. }
  RegDeleteValue(HKEY_CURRENT_USER, RunKey, 'Osdrop');
end;
