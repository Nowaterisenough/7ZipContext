# Remove only the optional sparse registration owned by this application path.
# Use Windows Installed Apps to uninstall an MSI installation.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'unregister.ps1') -InstallDir $PSScriptRoot
