; Inno Setup script for the installer build. Driven by tools/package.ps1, which passes:
;   /DAppVersion=<x.y.z>   /DExePath=<built openfences.exe>   /DOutDir=<dist folder>
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef ExePath
  #define ExePath "..\build\Release\openfences.exe"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

[Setup]
AppId={{8F3C2B1E-5A47-4D0B-9C6E-0E7F1A2B3C4D}
AppName=Open Fences
AppVersion={#AppVersion}
AppPublisher=Open Fences contributors
DefaultDirName={autopf}\Open Fences
DefaultGroupName=Open Fences
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=OpenFences-{#AppVersion}-setup
SetupIconFile=..\app\resources\app.ico
UninstallDisplayIcon={app}\openfences.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no admin prompt); the dialog offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; The app holds this mutex while it runs: setup asks to close it before updating.
AppMutex=openFences_SingleInstance
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start Open Fences with Windows"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#ExePath}"; DestDir: "{app}"; DestName: "openfences.exe"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.ru.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.zh-CN.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Open Fences"; Filename: "{app}\openfences.exe"
Name: "{group}\Uninstall Open Fences"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Open Fences"; Filename: "{app}\openfences.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Open Fences"; \
    ValueData: """{app}\openfences.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\openfences.exe"; Description: "{cm:LaunchProgram,Open Fences}"; Flags: nowait postinstall skipifsilent
