param(
    [Parameter(Mandatory = $true)][string]$Package,
    [switch]$AllowMachineInstall
)
$ErrorActionPreference = 'Stop'
if (-not $AllowMachineInstall) { throw 'This test performs a real machine installation. Pass -AllowMachineInstall only on an isolated test machine.' }
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'The isolated installer test requires administrator rights.' }
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$target = Join-Path $env:ProgramFiles '7ZipContext'
if (Test-Path -LiteralPath $target) { throw 'An existing installation must not be used for this test.' }
$windowsInstaller = New-Object -ComObject WindowsInstaller.Installer
$database = $windowsInstaller.OpenDatabase($packagePath,0)
$properties = @{}
try {
    foreach ($name in 'ProductCode','ProductVersion','UpgradeCode') {
        $view = $database.OpenView("SELECT Value FROM Property WHERE Property = '$name'"); $view.Execute()
        try { $properties[$name] = $view.Fetch().StringData(1) }
        finally { $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
    }
} finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowsInstaller) }
if ($properties.UpgradeCode -ne '{A1B2C3D4-0000-4B3A-9E1F-000000000001}') { throw 'Unexpected installer identity.' }
$uninstallKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + $properties.ProductCode
if (Test-Path -LiteralPath $uninstallKey) { throw 'This product is already installed.' }
$certificates = @(Get-ChildItem Cert:\CurrentUser\My,Cert:\CurrentUser\Root | Where-Object Subject -EQ 'CN=7ZipContext' | Select-Object -ExpandProperty Thumbprint | Sort-Object)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class InstallerIconProbe {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern uint ExtractIconEx(string file, int index, IntPtr large, IntPtr small, uint count);
}
'@
function Invoke-Installer([string[]]$Arguments,[string]$LogName) {
    $log = Join-Path $repository ('build/'+$LogName)
    $process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/msiexec.exe') -ArgumentList ($Arguments + @('/qn','/norestart','/L*V',('"'+$log+'"'))) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(180000)) { throw "Installer timed out. See $log" }
    if ($process.ExitCode -notin 0,3010) { throw "Installer failed with $($process.ExitCode). See $log" }
}
$installed = $false
try {
    Invoke-Installer -Arguments @('/i',('"'+$packagePath+'"')) -LogName 'msi-install.log'
    $installed = $true
    $executable = Join-Path $target '7ZipContextApp.exe'
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($executable).ProductVersion -ne $properties.ProductVersion) { throw 'Installed application version differs.' }
    $registration = Get-ItemProperty -LiteralPath $uninstallKey
    if ($registration.InstallLocation.TrimEnd('\') -ne $target) { throw 'Installed location is missing from Windows installation records.' }
    foreach ($type in '*','Directory') {
        $key = 'HKLM:\SOFTWARE\Classes\'+$type+'\shell\7ZipContext\shell\extract\command'
        if ((Get-Item -LiteralPath $key).GetValue('') -ne ('"'+$executable+'" --extract "%1"')) { throw 'Classic context-menu command is missing or incorrect.' }
    }
    $shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) '7-Zip Context.lnk'
    if (-not (Test-Path -LiteralPath $shortcut)) { throw 'Start-menu shortcut was not installed.' }
    if ([InstallerIconProbe]::ExtractIconEx($executable,-1,[IntPtr]::Zero,[IntPtr]::Zero,0) -ne 1) { throw 'Installed executable has no application icon.' }
    $shortcutShell = New-Object -ComObject WScript.Shell
    try { $iconLocation = $shortcutShell.CreateShortcut($shortcut).IconLocation }
    finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcutShell) }
    $iconMatch = [regex]::Match($iconLocation,'^(.+),(-?\d+)$')
    if (-not $iconMatch.Success) { throw 'Installed shortcut has no explicit icon.' }
    $iconPath = [Environment]::ExpandEnvironmentVariables($iconMatch.Groups[1].Value.Trim('"'))
    if (-not (Test-Path -LiteralPath $iconPath -PathType Leaf) -or [InstallerIconProbe]::ExtractIconEx($iconPath,-1,[IntPtr]::Zero,[IntPtr]::Zero,0) -ne 1) { throw 'Shortcut icon cannot be read by Windows.' }
    foreach ($logo in @{ 'StoreLogo.png' = 50; 'Square44x44Logo.png' = 44; 'Square150x150Logo.png' = 150 }.GetEnumerator()) {
        $image = [Drawing.Image]::FromFile((Join-Path $target ('Assets/'+$logo.Key)))
        try { if ($image.Width -ne $logo.Value -or $image.Height -ne $logo.Value) { throw "Incorrect package logo dimensions: $($logo.Key)" } }
        finally { $image.Dispose() }
    }
    if (Test-Path -LiteralPath (Join-Path $target '7ZipContext.pfx')) { throw 'Installer exported a private key.' }
    $smokeInfo = New-Object Diagnostics.ProcessStartInfo
    $smokeInfo.FileName = $executable; $smokeInfo.Arguments = '--smoke-test'; $smokeInfo.WorkingDirectory = $target
    $smokeInfo.UseShellExecute = $false; $smokeInfo.CreateNoWindow = $true
    $smoke = [Diagnostics.Process]::Start($smokeInfo)
    if (-not $smoke.WaitForExit(20000)) { $smoke.Kill(); $smoke.WaitForExit(); throw 'Installed application did not exit its startup check.' }
    if ($smoke.ExitCode -ne 0) { throw 'Installed application startup failed.' }
    $afterCertificates = @(Get-ChildItem Cert:\CurrentUser\My,Cert:\CurrentUser\Root | Where-Object Subject -EQ 'CN=7ZipContext' | Select-Object -ExpandProperty Thumbprint | Sort-Object)
    if (($certificates -join ',') -ne ($afterCertificates -join ',')) { throw 'Installer changed the certificate store.' }
} finally {
    if ($installed -or (Test-Path -LiteralPath $uninstallKey)) { Invoke-Installer -Arguments @('/x',$properties.ProductCode) -LogName 'msi-uninstall.log' }
}
if (Test-Path -LiteralPath (Join-Path $target '7ZipContextApp.exe')) { throw 'Uninstall left the application behind.' }
if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstall left its Windows product registration behind.' }
if (Test-Path -LiteralPath 'HKLM:\SOFTWARE\Classes\*\shell\7ZipContext') { throw 'Uninstall left its context menu behind.' }
if (Test-Path -LiteralPath 'HKLM:\SOFTWARE\Classes\Directory\shell\7ZipContext') { throw 'Uninstall left its folder context menu behind.' }
if (Test-Path -LiteralPath $shortcut) { throw 'Uninstall left its Start-menu shortcut behind.' }
Write-Output 'MSI installation, installed startup, application/shortcut icons, context-menu registration and uninstall tests passed.'
