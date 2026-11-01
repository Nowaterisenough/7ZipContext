# Optional Windows 11 sparse-package integration. The MSI owns the desktop app
# and classic context-menu entries, so AppX policy cannot roll back the install.
param([string]$InstallDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
$InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$dataDirectory = Join-Path $env:LOCALAPPDATA '7ZipContext'
[IO.Directory]::CreateDirectory($dataDirectory) | Out-Null
$logFile = Join-Path $dataDirectory 'registration.log'
$stateFile = Join-Path $dataDirectory 'shell-registration.json'
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-RegistrationLog([string]$Message) {
    $line = '[' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '] ' + $Message
    try { [IO.File]::AppendAllText($logFile,$line+[Environment]::NewLine,$utf8) } catch { }
    Write-Host $line
}
Write-RegistrationLog "Optional sparse registration: $InstallDir"
foreach ($relative in '7ZipContextApp.exe','7ZipContext.dll','AppxManifest.xml') {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $relative) -PathType Leaf)) { throw "Required application file is missing: $relative" }
}
$policy = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Appx' -ErrorAction SilentlyContinue
$settings = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
$developerMode = $settings.AllowDevelopmentWithoutDevLicense -eq 1
if ($null -ne $policy.AllowDevelopmentWithoutDevLicense) { $developerMode = $policy.AllowDevelopmentWithoutDevLicense -eq 1 }
if (-not $developerMode) {
    Write-RegistrationLog 'Developer mode is disabled. Desktop application and classic context menu remain available; optional unsigned sparse registration was skipped.'
    return
}
if (@(Get-Command Add-AppxPackage,Get-AppxPackage -ErrorAction SilentlyContinue).Count -ne 2) {
    Write-RegistrationLog 'AppX deployment is unavailable. Optional sparse registration was skipped.'
    return
}
try {
    Add-AppxPackage -Register (Join-Path $InstallDir 'AppxManifest.xml') -ExternalLocation $InstallDir -ErrorAction Stop
    $package = Get-AppxPackage -Name '7ZipContext' -ErrorAction Stop
    if (-not $package) { throw 'Sparse package was not found after registration.' }
    $state = [ordered]@{ installDirectory = $InstallDir; packageFullName = $package.PackageFullName }
    [IO.File]::WriteAllText($stateFile,($state | ConvertTo-Json),$utf8)
    Write-RegistrationLog 'Optional Windows 11 context menu registered successfully.'
} catch {
    Write-RegistrationLog ('Optional sparse registration was not completed: ' + $_.Exception.Message)
    Write-RegistrationLog 'Desktop application and classic context menu remain available.'
}
