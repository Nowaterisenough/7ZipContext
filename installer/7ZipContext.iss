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

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
chinesesimplified.BeveledLabel=7-Zip 右键菜单扩展
english.BeveledLabel=7-Zip Context Menu Extension

[Files]
Source: "..\build\install\*"; DestDir: "{app}"; Excludes: "*.pdb,*.pfx,*.log,*.previous-*.exe"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\7-Zip Context"; Filename: "{app}\7ZipContextApp.exe"

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\register.ps1"""; Flags: runhidden waituntilterminated; StatusMsg: "正在注册右键菜单扩展..."
Filename: "powershell.exe"; Parameters: "-Command ""Stop-Process -Name explorer -Force; Start-Sleep -Seconds 1; Start-Process explorer"""; Flags: runhidden nowait; StatusMsg: "正在重启资源管理器..."
Filename: "{app}\7ZipContextApp.exe"; Parameters: "--tray"; Flags: nowait postinstall skipifsilent; Description: "启动 7-Zip Context 托盘"

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\unregister.ps1"""; Flags: runhidden waituntilterminated
Filename: "powershell.exe"; Parameters: "-Command ""Stop-Process -Name explorer -Force; Start-Sleep -Seconds 1; Start-Process explorer"""; Flags: runhidden nowait

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  RegisterScript: String;
  UnregisterScript: String;
begin
  if CurStep = ssPostInstall then
  begin
    // Create register.ps1
    RegisterScript :=
      '$ErrorActionPreference = "Stop"' + #13#10 +
      '$InstallDir = "' + ExpandConstant('{app}') + '"' + #13#10 +
      '$CertName = "7ZipContext"' + #13#10 +
      '$CertFile = Join-Path $InstallDir "$CertName.pfx"' + #13#10 +
      '$CertPassword = "7zip"' + #13#10 +
      '' + #13#10 +
      '# Create certificate if not exists' + #13#10 +
      '$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$CertName" } | Select-Object -First 1' + #13#10 +
      'if (-not $cert) {' + #13#10 +
      '    $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=$CertName" -KeyUsage DigitalSignature -FriendlyName $CertName -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")' + #13#10 +
      '    $pwd = ConvertTo-SecureString -String $CertPassword -Force -AsPlainText' + #13#10 +
      '    Export-PfxCertificate -Cert $cert -FilePath $CertFile -Password $pwd | Out-Null' + #13#10 +
      '    Import-PfxCertificate -FilePath $CertFile -CertStoreLocation Cert:\LocalMachine\Root -Password $pwd | Out-Null' + #13#10 +
      '}' + #13#10 +
      '' + #13#10 +
      '# Remove existing package' + #13#10 +
      '$pkg = Get-AppxPackage -Name "7ZipContext" 2>$null' + #13#10 +
      'if ($pkg) { Remove-AppxPackage -Package $pkg.PackageFullName 2>$null }' + #13#10 +
      '' + #13#10 +
      '# Register sparse package' + #13#10 +
      '$ManifestFile = Join-Path $InstallDir "AppxManifest.xml"' + #13#10 +
      'Add-AppxPackage -Register $ManifestFile -ExternalLocation $InstallDir' + #13#10;
    SaveStringToFile(ExpandConstant('{app}\register.ps1'), RegisterScript, False);

    // Create unregister.ps1
    UnregisterScript :=
      '$ErrorActionPreference = "SilentlyContinue"' + #13#10 +
      '' + #13#10 +
      '# Remove package' + #13#10 +
      '$pkg = Get-AppxPackage -Name "7ZipContext"' + #13#10 +
      'if ($pkg) { Remove-AppxPackage -Package $pkg.PackageFullName }' + #13#10 +
      '' + #13#10 +
      '# Remove certificates' + #13#10 +
      '$certUser = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=7ZipContext" }' + #13#10 +
      'if ($certUser) { Remove-Item $certUser.PSPath }' + #13#10 +
      '$certRoot = Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Subject -eq "CN=7ZipContext" }' + #13#10 +
      'if ($certRoot) { Remove-Item $certRoot.PSPath }' + #13#10;
    SaveStringToFile(ExpandConstant('{app}\unregister.ps1'), UnregisterScript, False);
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
end;
