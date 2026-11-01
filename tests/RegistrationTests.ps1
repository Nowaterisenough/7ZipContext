$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixture = Join-Path $repository ('build/registration-tests-' + [guid]::NewGuid().ToString('N'))
$application = Join-Path $fixture 'application'
New-Item -ItemType Directory -Path $application | Out-Null
foreach ($name in '7ZipContextApp.exe','7ZipContext.dll','AppxManifest.xml') { [IO.File]::WriteAllText((Join-Path $application $name),'fixture') }
$savedData = $env:LOCALAPPDATA; $savedModules = $env:PSModulePath
$env:LOCALAPPDATA = Join-Path $fixture 'user-data'
$global:SevenZipRegistrationFixture = @{}
$global:SevenZipRegistrationFixture.development = 0; $global:SevenZipRegistrationFixture.policy = $null; $global:SevenZipRegistrationFixture.failRegistration = $false
$global:SevenZipRegistrationFixture.registrations = 0; $global:SevenZipRegistrationFixture.removals = 0; $global:SevenZipRegistrationFixture.packageName = '7ZipContext_test_package'
function Get-ItemProperty {
    [CmdletBinding()]param([string]$LiteralPath)
    if ($LiteralPath -like '*Policies*') { return @{ AllowDevelopmentWithoutDevLicense = $global:SevenZipRegistrationFixture.policy } }
    return @{ AllowDevelopmentWithoutDevLicense = $global:SevenZipRegistrationFixture.development }
}
function Add-AppxPackage {
    [CmdletBinding()]param([string]$Register,[string]$ExternalLocation)
    $global:SevenZipRegistrationFixture.registrations++
    if ($global:SevenZipRegistrationFixture.failRegistration) { throw 'Simulated AppX policy rejection: 0x80073CFF' }
}
function Get-AppxPackage { [CmdletBinding()]param([string]$Name); return @{ PackageFullName = $global:SevenZipRegistrationFixture.packageName } }
function Remove-AppxPackage { [CmdletBinding()]param([string]$Package); $global:SevenZipRegistrationFixture.removals++ }
function New-SelfSignedCertificate { throw 'Registration must not create certificates.' }
function Export-PfxCertificate { throw 'Registration must not export private keys.' }
function Import-PfxCertificate { throw 'Registration must not change certificate trust.' }
function Stop-Process { throw 'Registration must not stop user processes.' }
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
$register = Join-Path $repository 'installer/register.ps1'
$unregister = Join-Path $repository 'installer/unregister.ps1'
$stateFile = Join-Path $env:LOCALAPPDATA '7ZipContext/shell-registration.json'
try {
    & $register -InstallDir $application
    Assert ($global:SevenZipRegistrationFixture.registrations -eq 0) 'Disabled developer mode attempted unsigned deployment.'
    $global:SevenZipRegistrationFixture.development = 1; $global:SevenZipRegistrationFixture.policy = 0
    & $register -InstallDir $application
    Assert ($global:SevenZipRegistrationFixture.registrations -eq 0) 'Machine policy did not override developer mode.'
    $global:SevenZipRegistrationFixture.policy = $null; $global:SevenZipRegistrationFixture.failRegistration = $true
    & $register -InstallDir $application
    Assert ($global:SevenZipRegistrationFixture.registrations -eq 1 -and $global:SevenZipRegistrationFixture.removals -eq 0) 'AppX rejection removed an existing package.'
    Assert (-not (Test-Path -LiteralPath $stateFile)) 'Rejected registration was recorded as successful.'
    $global:SevenZipRegistrationFixture.failRegistration = $false
    & $register -InstallDir ($application+'\.')
    Assert (Test-Path -LiteralPath $stateFile) 'Successful registration was not recorded.'
    $state = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert ($state.installDirectory -eq $application) 'Registered path was not normalized.'
    & $unregister -InstallDir (Join-Path $fixture 'other-application')
    Assert ($global:SevenZipRegistrationFixture.removals -eq 0) 'Uninstall removed another installation registration.'
    & $unregister -InstallDir $application
    Assert ($global:SevenZipRegistrationFixture.removals -eq 1 -and -not (Test-Path -LiteralPath $stateFile)) 'Owned registration was not removed.'
    & $unregister -InstallDir $application
    Assert ($global:SevenZipRegistrationFixture.removals -eq 1) 'Unregistration was not idempotent.'
    Assert (@(Get-ChildItem -LiteralPath $application -File).Count -eq 3) 'Registration wrote files into the installed application.'
    Write-Output 'Registration tests passed: disabled policy, policy override, deployment failure, success, ownership and idempotence.'
} finally { $env:LOCALAPPDATA = $savedData; $env:PSModulePath = $savedModules; Remove-Variable SevenZipRegistrationFixture -Scope Global }
