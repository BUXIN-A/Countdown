; =============================================================================
; Countdown.iss - Windows 安装程序（Inno Setup 7）
;
; 编译： "<Inno Setup 7>\ISCC.exe" installer\Countdown.iss
; 版本号自动读取 build-release\Countdown.exe 的文件版本，无需在此重复维护。
; 注意：本文件含中文，必须保存为 UTF-8 with BOM。
; =============================================================================

#define MyAppExeName "Countdown.exe"
#define BuildDir "..\build-release"
; 读取 exe 的文件版本（CMake 固定三段，这里去掉末尾的 .0 以便与发布文件名一致）
#define VerFull GetVersionNumbersString(AddBackslash(BuildDir) + MyAppExeName)
#define MyAppVersion Copy(VerFull, 1, Len(VerFull) - 2)

[Setup]
; 稳定的 AppId：升级安装会沿用同一卸载记录
AppId={{B7E4C1A9-3D62-4F58-8C1B-2A9D6E0F7C34}
AppName=倒计时
AppVerName=倒计时 {#MyAppVersion}
AppVersion={#MyAppVersion}
AppPublisher=Bu Xin
AppPublisherURL=https://github.com/BUXIN-A/Countdown
AppSupportURL=https://github.com/BUXIN-A/Countdown
AppUpdatesURL=https://github.com/BUXIN-A/Countdown/releases
VersionInfoVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\Countdown
DefaultGroupName=倒计时
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=Countdown-v{#MyAppVersion}-win64-Setup
SetupIconFile=..\countdown.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 检测正在运行的实例（与程序内互斥体同名），提示用户关闭后再继续
AppMutex=Local\Countdown_SingleInstance_Mutex
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\lib\article\normal.dll"; DestDir: "{app}\lib\article"; Flags: ignoreversion
Source: "{#BuildDir}\lib\article\week.dll"; DestDir: "{app}\lib\article"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\倒计时"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,倒计时}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\倒计时"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,倒计时}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时一并清理运行时生成的数据（配置、文章、缓存、历史、字体与模块目录）
Type: files; Name: "{app}\config.json"
Type: filesandordirs; Name: "{app}\article"
Type: filesandordirs; Name: "{app}\hitokoto"
Type: filesandordirs; Name: "{app}\temp"
Type: filesandordirs; Name: "{app}\fonts"
Type: filesandordirs; Name: "{app}\lib"
