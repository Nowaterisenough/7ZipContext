# The desktop application does not require sparse-package registration.
# MSI installations also provide a classic context menu without developer mode.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'register.ps1') -InstallDir $PSScriptRoot
