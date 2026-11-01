# 7-Zip Context Menu - Register Script for MSI
# Called by Windows Installer during installation

param(
    [string]$InstallDir = $PSScriptRoot
)

$ErrorActionPreference = "Stop"

# Setup logging
$LogFile = Join-Path $env:TEMP "7ZipContext_register.log"
function Write-Log {
    param([string]$Message)
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Add-Content -Path $LogFile -Value $logMessage -ErrorAction SilentlyContinue
    Write-Host $logMessage
}

Write-Log "=== 7ZipContext Registration Started ==="
Write-Log "Input InstallDir: $InstallDir"

# Normalize path
$InstallDir = $InstallDir.TrimEnd('\', '.')
if (-not $InstallDir) {
    $InstallDir = $PSScriptRoot
}

Write-Log "Normalized InstallDir: $InstallDir"

$CertName = "7ZipContext"
$CertFile = Join-Path $InstallDir "$CertName.pfx"
$CertPassword = "7zip"
$ManifestFile = Join-Path $InstallDir "AppxManifest.xml"

Write-Log "CertFile: $CertFile"
Write-Log "ManifestFile: $ManifestFile"

# Verify required files exist
$DllFile = Join-Path $InstallDir "7ZipContext.dll"
if (-not (Test-Path $DllFile)) {
    Write-Log "ERROR: DLL not found at $DllFile"
    throw "7ZipContext.dll not found"
}
Write-Log "DLL verified: $DllFile"
$DesktopFile = Join-Path $InstallDir "7ZipContextApp.exe"
if (-not (Test-Path -LiteralPath $DesktopFile)) {
    throw "7ZipContextApp.exe not found"
}

if (-not (Test-Path $ManifestFile)) {
    Write-Log "ERROR: Manifest not found at $ManifestFile"
    throw "AppxManifest.xml not found"
}
Write-Log "Manifest verified: $ManifestFile"

# Create certificate if not exists
Write-Log "Checking for existing certificate..."
$cert = Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue | Where-Object { $_.Subject -eq "CN=$CertName" } | Select-Object -First 1

if (-not $cert) {
    Write-Log "Creating new self-signed certificate..."
    $cert = New-SelfSignedCertificate `
        -Type Custom `
        -Subject "CN=$CertName" `
        -KeyUsage DigitalSignature `
        -FriendlyName $CertName `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

    Write-Log "Certificate created: $($cert.Thumbprint)"

    $pwd = ConvertTo-SecureString -String $CertPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $CertFile -Password $pwd | Out-Null
    Write-Log "Certificate exported to: $CertFile"

    # Import to CurrentUser\Root (works without admin in impersonated context)
    Import-PfxCertificate -FilePath $CertFile -CertStoreLocation Cert:\CurrentUser\Root -Password $pwd | Out-Null
    Write-Log "Certificate imported to CurrentUser\\Root"
} else {
    Write-Log "Existing certificate found: $($cert.Thumbprint)"
}

# Remove existing package
Write-Log "Checking for existing package..."
$pkg = Get-AppxPackage -Name "7ZipContext" -ErrorAction SilentlyContinue
if ($pkg) {
    Write-Log "Removing existing package: $($pkg.PackageFullName)"
    Remove-AppxPackage -Package $pkg.PackageFullName -ErrorAction SilentlyContinue
    Write-Log "Existing package removed"
} else {
    Write-Log "No existing package found"
}

# Register sparse package
Write-Log "Registering sparse package..."
Write-Log "  Manifest: $ManifestFile"
Write-Log "  ExternalLocation: $InstallDir"

try {
    Add-AppxPackage -Register $ManifestFile -ExternalLocation $InstallDir
    Write-Log "Sparse package registered successfully"
} catch {
    Write-Log "ERROR: Failed to register sparse package: $_"
    throw
}

# Verify registration
$pkg = Get-AppxPackage -Name "7ZipContext" -ErrorAction SilentlyContinue
if ($pkg) {
    Write-Log "Verification: Package registered successfully"
    Write-Log "  PackageFullName: $($pkg.PackageFullName)"
    Write-Log "  InstallLocation: $($pkg.InstallLocation)"
} else {
    Write-Log "WARNING: Package verification failed - package not found after registration"
}

Write-Log "=== 7ZipContext Registration Completed ==="
