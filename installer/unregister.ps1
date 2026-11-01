param([string]$InstallDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
$InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$stateFile = Join-Path $env:LOCALAPPDATA '7ZipContext/shell-registration.json'
if (-not (Test-Path -LiteralPath $stateFile -PathType Leaf)) { return }
try {
    $state = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not [string]::Equals($state.installDirectory,$InstallDir,[StringComparison]::OrdinalIgnoreCase)) { return }
    $package = Get-AppxPackage -Name '7ZipContext' -ErrorAction Stop
    if ($package -and $package.PackageFullName -eq $state.packageFullName) {
        Remove-AppxPackage -Package $package.PackageFullName -ErrorAction Stop
    }
    Remove-Item -LiteralPath $stateFile -Force
} catch {
    Write-Warning ('Optional sparse package removal was not completed: ' + $_.Exception.Message)
}
