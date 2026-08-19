; Inno Setup script -- produces a per-user installer with a proper
; "Apps & features" / Control Panel entry (install + uninstall).
; Version is injected by CI:  iscc /DAppVersion=1.2.3 WhatsAppPrivacy.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "WhatsApp Privacy"
#define AppExe  "WhatsAppPrivacy.exe"

[Setup]
; A stable GUID is what lets an upgrade replace the previous install instead of
; stacking up duplicate Control Panel entries.
AppId={{8F3A6C21-4D5E-4B7A-9C10-2E6B5A9D7F42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=sanjeevnode
AppPublisherURL=https://sanjeevnode.in
AppSupportURL=https://github.com/sanjeevnode/win-whatsapp-privacy
AppUpdatesURL=https://github.com/sanjeevnode/win-whatsapp-privacy/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
SetupIconFile=..\assets\icons\app.ico
OutputDir=..\dist
OutputBaseFilename=WhatsAppPrivacy-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Per-user install: no UAC prompt, and it still appears in Control Panel.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "..\build\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
// WebView2 Evergreen ships with Windows 11 and current Windows 10, but not with
// older/LTSC images. Without it the app opens an empty window, so check up front
// and offer the official bootstrapper rather than failing at first launch.
function WebView2Installed: Boolean;
var
  Value: string;
begin
  Result :=
    RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Value) or
    RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Value) or
    RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', Value);
  if Result then
    Result := (Value <> '') and (Value <> '0.0.0.0');
end;

function InitializeSetup: Boolean;
var
  ErrCode: Integer;
begin
  Result := True;
  if not WebView2Installed then begin
    if MsgBox('{#AppName} needs the Microsoft Edge WebView2 Runtime, which is not installed.'
              + #13#10#13#10 'Open the Microsoft download page now?',
              mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://go.microsoft.com/fwlink/p/?LinkId=2124703',
                '', '', SW_SHOW, ewNoWait, ErrCode);
  end;
end;

[UninstallDelete]
; The WebView2 profile holds the logged-in WhatsApp session; remove it on
; uninstall so no chat data is left behind.
Type: filesandordirs; Name: "{localappdata}\WhatsAppPrivacy"
