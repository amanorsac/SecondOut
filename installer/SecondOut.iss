; Inno Setup script for SecondOut
; Build with: ISCC.exe SecondOut.iss

#define AppName "SecondOut"
#define AppVersion "1.3.2"
#define AppPublisher "Amanorsac Studio"
#define BuildDir "..\build\SecondOut_artefacts\Release"

[Setup]
AppId={{7E2A9B41-5C63-4F0D-9A1B-2E8D40C1A7F2}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=.\Output
OutputBaseFilename=SecondOut-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupIconFile=..\Resources\icon.ico
UninstallDisplayName={#AppName} {#AppVersion}
LicenseFile=

[Files]
; VST3 bundle -> C:\Program Files\Common Files\VST3\SecondOut.vst3
Source: "{#BuildDir}\VST3\SecondOut.vst3\*"; DestDir: "{commoncf64}\VST3\SecondOut.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone app (optional component)
Source: "{#BuildDir}\Standalone\SecondOut.exe"; DestDir: "{commonpf64}\{#AppPublisher}\{#AppName}"; \
    Components: standalone; Flags: ignoreversion

[Components]
Name: "vst3"; Description: "SecondOut VST3 plugin (required)"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone app (test outside a DAW)"; Types: full

[Icons]
Name: "{commonprograms}\{#AppName}"; Filename: "{commonpf64}\{#AppPublisher}\{#AppName}\SecondOut.exe"; \
    Components: standalone

[Run]
Filename: "{commonpf64}\{#AppPublisher}\{#AppName}\SecondOut.exe"; \
    Description: "Launch SecondOut standalone"; Components: standalone; \
    Flags: nowait postinstall skipifsilent unchecked
