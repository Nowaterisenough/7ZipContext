; 7-Zip Context Menu - Inno Setup Script
; Requires Inno Setup 6.0+ (https://jrsoftware.org/isinfo.php)

#define MyAppName "7-Zip Context Menu"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "7ZipContext"
#define MyAppURL "https://github.com/user/7ZipContext"

[Setup]
AppId={{B8A0B7C1-7C5D-4B3A-9E1F-2A3B4C5D6E7F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\7ZipContext
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir=..\build\installer
OutputBaseFilename=7ZipContext-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\7ZipContextApp.exe
SetupIconFile=..\Package\Assets\icon.ico

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
chinesesimplified.BeveledLabel=7-Zip 右键菜单扩展
english.BeveledLabel=7-Zip Context Menu Extension

[Files]
Source: "..\build\install\*"; DestDir: "{app}"; Excludes: "*.pdb,*.pfx,*.log,*.previous-*.exe"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\7-Zip Context"; Filename: "{app}\7ZipContextApp.exe"; IconFilename: "{app}\Assets\icon.ico"; IconIndex: 0

[Registry]
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext"; ValueType: string; ValueName: "MUIVerb"; ValueData: "7-Zip Context"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext"; ValueType: string; ValueName: "SubCommands"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext\shell\extract"; ValueType: string; ValueData: "Extract and organize..."
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext\shell\extract\command"; ValueType: string; ValueData: """{app}\7ZipContextApp.exe"" --extract ""%1"""
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext\shell\compress"; ValueType: string; ValueData: "Add to archive..."
Root: HKLM; Subkey: "Software\Classes\*\shell\7ZipContext\shell\compress\command"; ValueType: string; ValueData: """{app}\7ZipContextApp.exe"" --compress ""%1"""
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext"; ValueType: string; ValueName: "MUIVerb"; ValueData: "7-Zip Context"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext"; ValueType: string; ValueName: "SubCommands"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext\shell\extract"; ValueType: string; ValueData: "Extract and organize..."
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext\shell\extract\command"; ValueType: string; ValueData: """{app}\7ZipContextApp.exe"" --extract ""%1"""
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext\shell\compress"; ValueType: string; ValueData: "Add to archive..."
Root: HKLM; Subkey: "Software\Classes\Directory\shell\7ZipContext\shell\compress\command"; ValueType: string; ValueData: """{app}\7ZipContextApp.exe"" --compress ""%1"""

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\register.ps1"""; Flags: runhidden waituntilterminated; StatusMsg: "正在注册右键菜单扩展..."
Filename: "{app}\7ZipContextApp.exe"; Parameters: "--tray"; Flags: nowait postinstall skipifsilent; Description: "启动 7-Zip Context 托盘"

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\unregister.ps1"""; Flags: runhidden waituntilterminated
