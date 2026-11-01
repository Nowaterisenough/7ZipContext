# 7-Zip Context  - Install Script
# Run as Administrator: powershell -ExecutionPolicy Bypass -File install.ps1

$ErrorActionPreference = "Stop"

# Check admin rights
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[ERROR] Please run this script as Administrator." -ForegroundColor Red
    exit 1
}

# Use script directory as install location
$InstallDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DllFile = Join-Path $InstallDir "7ZipContext.dll"
$ManifestFile = Join-Path $InstallDir "AppxManifest.xml"

Write-Host "========================================" -ForegroundColor Green
Write-Host "7-Zip Context - Install" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

# Check required files
if (-not (Test-Path $DllFile)) {
    Write-Host "[ERROR] 7ZipContext.dll not found at: $InstallDir" -ForegroundColor Red
    Write-Host "Please build and install the project first:" -ForegroundColor Yellow
    Write-Host "  cmake --build build --config Release --target install" -ForegroundColor Cyan
    exit 1
}

if (-not (Test-Path $ManifestFile)) {
    Write-Host "[ERROR] AppxManifest.xml not found at: $InstallDir" -ForegroundColor Red
    exit 1
}

$CertName = "7ZipContext"
$CertFile = Join-Path $InstallDir "$CertName.pfx"
$CertPassword = "7zip"

Write-Host "[1/5] Checking desktop application..." -ForegroundColor Cyan
$desktopExe = Join-Path $InstallDir "7ZipContextApp.exe"
if (-not (Test-Path -LiteralPath $desktopExe)) {
    throw "7ZipContextApp.exe not found. Build and install both targets first."
}
Write-Host "  Done."

Write-Host "[2/5] Setting up code signing certificate..." -ForegroundColor Cyan
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$CertName" } | Select-Object -First 1

if (-not $cert) {
    Write-Host "  Creating new self-signed certificate..."
    $cert = New-SelfSignedCertificate `
        -Type Custom `
        -Subject "CN=$CertName" `
        -KeyUsage DigitalSignature `
        -FriendlyName $CertName `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

    $pwd = ConvertTo-SecureString -String $CertPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $CertFile -Password $pwd | Out-Null

    Write-Host "  Installing certificate to Trusted Root..."
    Import-PfxCertificate -FilePath $CertFile -CertStoreLocation Cert:\LocalMachine\Root -Password $pwd | Out-Null
} else {
    Write-Host "  Certificate already exists."
}

Write-Host "[3/5] Checking Assets..." -ForegroundColor Cyan
$iconDir = Join-Path $InstallDir "Assets"
$iconFile = Join-Path $iconDir "icon.png"
if (-not (Test-Path $iconDir)) {
    New-Item -ItemType Directory -Path $iconDir -Force | Out-Null
}
if (-not (Test-Path $iconFile)) {
    # 1x1 transparent PNG
    $pngBytes = [Convert]::FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==")
    [System.IO.File]::WriteAllBytes($iconFile, $pngBytes)
}
Write-Host "  Done."

Write-Host "[4/5] Removing existing package (if any)..." -ForegroundColor Cyan
$existingPkg = Get-AppxPackage -Name "7ZipContext" 2>$null
if ($existingPkg) {
    Remove-AppxPackage -Package $existingPkg.PackageFullName
    Write-Host "  Existing package removed."
} else {
    Write-Host "  No existing package found."
}

Write-Host "[5/5] Registering sparse package..." -ForegroundColor Cyan
try {
    Add-AppxPackage -Register $ManifestFile -ExternalLocation $InstallDir
    Write-Host "  Package registered successfully." -ForegroundColor Green
} catch {
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Common issues:" -ForegroundColor Yellow
    Write-Host "    - Make sure Developer Mode is enabled in Windows Settings"
    Write-Host "    - Settings > Privacy & security > For developers > Developer Mode"
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Installation completed successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Please restart Windows Explorer:" -ForegroundColor Yellow
Write-Host "  Stop-Process -Name explorer -Force; Start-Process explorer" -ForegroundColor Cyan
Write-Host ""
