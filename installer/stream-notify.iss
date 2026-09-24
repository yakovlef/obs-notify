#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point to the staged stream-notify directory
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define MyAppName "Stream Notify"
#define MyAppPublisher "Vladimir Yakovlev"
#define MyAppURL "https://github.com/yakovlef/obs-stream-notify"

[Setup]
AppId={{527CC307-80B9-4CD9-9353-750CA021BDAC}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={code:GetDirName}
AppendDefaultDirName=no
DisableDirPage=no
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=stream-notify-{#MyAppVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes
CloseApplicationsFilter=obs64.exe
RestartApplications=no
UninstallDisplayName={#MyAppName}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} installer
VersionInfoCopyright=Copyright (C) 2026 Stream Notify contributors

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "{#SourceDir}\bin\64bit\stream-notify.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data\obs-plugins\stream-notify"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\obs-telegram-notify.dll"
Type: filesandordirs; Name: "{app}\data\obs-plugins\obs-telegram-notify"
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\obs-telegram-notify"

[UninstallDelete]
Type: dirifempty; Name: "{app}\data\obs-plugins\stream-notify\tls"
Type: dirifempty; Name: "{app}\data\obs-plugins\stream-notify\locale"
Type: dirifempty; Name: "{app}\data\obs-plugins\stream-notify"

[CustomMessages]
english.OBSNotFound=OBS Studio was not found in the selected directory. Select the folder that contains bin\64bit\obs64.exe.
russian.OBSNotFound=OBS Studio не найдена в выбранной папке. Выберите папку, содержащую bin\64bit\obs64.exe.
english.OBSVersionReadFailed=The OBS Studio version could not be read from bin\64bit\obs64.exe.
russian.OBSVersionReadFailed=Не удалось определить версию OBS Studio из bin\64bit\obs64.exe.
english.OBSVersionTooOld=OBS Studio 31 or newer is required.
russian.OBSVersionTooOld=Требуется OBS Studio версии 31 или новее.

[Code]
function GetDirName(Value: String): String;
var
  InstallPath: String;
begin
  Result := ExpandConstant('{autopf}\obs-studio');
  if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', InstallPath) then
    Result := InstallPath;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', InstallPath) then
    Result := InstallPath;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  OBSFileName: String;
  OBSVersionMS, OBSVersionLS: Cardinal;
  OBSMajorVersion: Cardinal;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    OBSFileName := ExpandConstant('{app}\bin\64bit\obs64.exe');
    if not FileExists(OBSFileName) then
    begin
      MsgBox(CustomMessage('OBSNotFound'), mbError, MB_OK);
      Result := False;
      Exit;
    end;
    if not GetVersionNumbers(OBSFileName, OBSVersionMS, OBSVersionLS) then
    begin
      MsgBox(CustomMessage('OBSVersionReadFailed'), mbError, MB_OK);
      Result := False;
      Exit;
    end;
    OBSMajorVersion := OBSVersionMS shr 16;
    if OBSMajorVersion < 31 then
    begin
      MsgBox(CustomMessage('OBSVersionTooOld'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;
