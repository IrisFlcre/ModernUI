; ============================================================================
; CameraPro + Annotation 统一安装包
; ----------------------------------------------------------------------------
; 包含内容：
;   CameraPro.exe              教学摄像头展台
;   Annotation.exe             全屏批注（由 modernui_annotation.exe 重命名）
;   icons\*.svg                两套程序共用的全部图标资源
;   models\*.onnx / *.txt      PP-OCRv6 检测 / 识别模型与字典
;   运行库 DLL（若构建目录中存在，自动一并收集）
;
; 编译（Windows PowerShell）：
;   & "C:\Program Files\Inno Setup 7\ISCC.exe" "D:\ModernUI\installer\CameraProSuite.iss"
;
; 输出：D:\ModernUI\dist\CameraPro_Annotation_Setup_0.1.0.exe
; ============================================================================

#define AppName "CameraPro 教学展台与批注套件"
#define AppVersion "0.1.0"
#define AppPublisher "ModernUI"
#define AppIdGuid "{{C7A3E1F4-6B2D-4E58-9A10-5D8F3B7C2A61}"

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
VersionInfoDescription={#AppName} 安装程序

; 两个程序安装到同一目录，共享 icons / models
DefaultDirName={autopf}\CameraPro Suite
DefaultGroupName=CameraPro 套件
DisableProgramGroupPage=yes

; 64 位程序（MinGW x86_64）
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 程序依赖 UCRT 与 Media Foundation（Windows 10 及以上内置）
MinVersion=10.0

PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=CameraPro_Annotation_Setup_{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\CameraPro.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.DesktopCameraPro=创建 CameraPro 桌面快捷方式
chinesesimplified.DesktopAnnotation=创建 Annotation 桌面快捷方式
english.DesktopCameraPro=Create a &desktop shortcut for CameraPro
english.DesktopAnnotation=Create a &desktop shortcut for Annotation

[Tasks]
Name: "desktopcamera"; Description: "{cm:DesktopCameraPro}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktopannotation"; Description: "{cm:DesktopAnnotation}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; --- 主程序 ---
Source: "..\CameraPro\build\CameraPro.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Annotation\build\modernui_annotation.exe"; DestDir: "{app}"; DestName: "Annotation.exe"; Flags: ignoreversion

; --- 图标（CameraPro 图标集覆盖两程序公共图标）---
Source: "..\CameraPro\icons\*"; DestDir: "{app}\icons"; Flags: ignoreversion recursesubdirs createallsubdirs
; Annotation 的 AI 教学卡片语义图标（CameraPro 图标集中没有，从共享图标目录补齐）
Source: "..\icons\book.svg"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "..\icons\explain.svg"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "..\icons\inspiration.svg"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "..\icons\question.svg"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "..\icons\tips.svg"; DestDir: "{app}\icons"; Flags: ignoreversion
Source: "..\icons\arrow-roadmap.svg"; DestDir: "{app}\icons"; Flags: ignoreversion

; --- OCR 模型与字典（两程序共用；模型、字典均在目录内）---
Source: "..\CameraPro\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs

; --- 第三方运行时 DLL（当前为全静态链接，无外部 DLL；
;     若以后构建目录出现 DLL，会自动打包）---
Source: "..\CameraPro\build\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\Annotation\build\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\CameraPro 教学展台"; Filename: "{app}\CameraPro.exe"; WorkingDir: "{app}"
Name: "{group}\Annotation 全屏批注"; Filename: "{app}\Annotation.exe"; WorkingDir: "{app}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\CameraPro 教学展台"; Filename: "{app}\CameraPro.exe"; WorkingDir: "{app}"; Tasks: desktopcamera
Name: "{autodesktop}\Annotation 全屏批注"; Filename: "{app}\Annotation.exe"; WorkingDir: "{app}"; Tasks: desktopannotation

[Run]
Filename: "{app}\CameraPro.exe"; Description: "{cm:LaunchProgram,CameraPro 教学展台}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Annotation.exe"; Description: "{cm:LaunchProgram,Annotation 全屏批注}"; Flags: nowait postinstall skipifsilent
