; ============================================================================
; ModernUI Demos — combined installer for the example programs
; ----------------------------------------------------------------------------
; Contents:
;   modernui_blend2d_demo.exe   Canvas / layered-window / Blend2D validation
;   modernui_ui_demo.exe        Widget composition and Flexbox layout
;   modernui_fluent_demo.exe    Fluent Design control gallery
;   modernui_style_demo.exe     CSS / .ui styling demo
;   assets\                     demo.css and demo.ui used by style_demo
;
; Build first (MSYS2 UCRT64):
;   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
;   cmake --build build -j
;
; Then compile this script with Inno Setup:
;   ISCC.exe installer\DemosSuite.iss
;
; Output: dist\ModernUI_Demos_Setup_0.1.0.exe
; ============================================================================

#define AppName "ModernUI Demos"
#define AppVersion "0.1.0"
#define AppPublisher "ModernUI"
#define AppIdGuid "{{D4E5F6A7-B8C9-4DAE-9F01-23456789ABCD}"

[Setup]
AppId={#AppIdGuid}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoVersion=0.1.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup

DefaultDirName={autopf}\ModernUI Demos
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes

; 64-bit x86-64 binaries; Windows 10 or later
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=ModernUI_Demos_Setup_{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\modernui_fluent_demo.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\examples\blend2d_demo\modernui_blend2d_demo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\examples\ui_demo\modernui_ui_demo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\examples\fluent_demo\modernui_fluent_demo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\examples\style_demo\modernui_style_demo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\examples\style_demo\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Blend2D Canvas Demo"; Filename: "{app}\modernui_blend2d_demo.exe"; WorkingDir: "{app}"
Name: "{group}\UI Demo"; Filename: "{app}\modernui_ui_demo.exe"; WorkingDir: "{app}"
Name: "{group}\Fluent Design Demo"; Filename: "{app}\modernui_fluent_demo.exe"; WorkingDir: "{app}"
Name: "{group}\Style Demo"; Filename: "{app}\modernui_style_demo.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\modernui_fluent_demo.exe"; Description: "{cm:LaunchProgram,ModernUI Fluent Demo}"; Flags: nowait postinstall skipifsilent
