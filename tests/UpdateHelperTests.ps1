param(
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$FailureExecutable
)
$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$failureFixture = (Resolve-Path -LiteralPath $FailureExecutable).Path
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$root = Join-Path $repository ('build/update-helper-tests-' + [guid]::NewGuid().ToString('N'))
$target = Join-Path $root 'application'
$stage = Join-Path $root 'release'
New-Item -ItemType Directory -Path $root,$target,$stage | Out-Null
$powershell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$helper = Join-Path $repository 'scripts/Apply-Update.ps1'
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $runtime '7ZipContextApp.exe')).ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Runtime must have a release version resource.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
Copy-Item -Path (Join-Path $runtime '*') -Destination $stage -Recurse
Copy-Item -LiteralPath $helper -Destination (Join-Path $stage 'Apply-Update.ps1') -Force
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Text([string]$Path,[string]$Text) { [IO.File]::WriteAllText($Path,$Text,$utf8) }
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Write-Manifest([string]$Directory,[string]$ReleaseVersion) {
    $files = @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Where-Object Name -NE 'update-manifest.json' | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($Directory.Length+1).Replace('\','/'); size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
    Write-Text (Join-Path $Directory 'update-manifest.json') ([ordered]@{ format = 1; version = $ReleaseVersion; architecture = 'x64'; files = $files } | ConvertTo-Json -Depth 5)
}
function Zip-Stage([string]$Name) {
    $path = Join-Path $root ($Name+'.zip')
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$path,[IO.Compression.CompressionLevel]::Fastest,$false)
    return $path
}
function Run-Update([string]$Name,[string]$Archive,[string]$Checksum,[bool]$Success) {
    $arguments = @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$helper+'"'),'-Package',('"'+$Archive+'"'),'-Sha256',$Checksum,'-Version',$version,'-TargetDirectory',('"'+$target+'"'),'-NoRestart')
    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $process = Start-Process -FilePath $powershell -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root "$Name.stdout.log") -RedirectStandardError (Join-Path $root "$Name.stderr.log")
        if (-not $process.WaitForExit(60000)) { $process.Kill(); $process.WaitForExit(); throw "Update helper timed out: $Name" }
        $result = Get-Content -LiteralPath (Join-Path $root 'update-result.json') -Raw | ConvertFrom-Json
        Assert ($result.success -eq $Success) "Unexpected helper result ($Name): $($result.error)"
        Assert (($process.ExitCode -eq 0) -eq $Success) "Unexpected helper exit code: $Name"
        $errors = @{ 'checksum-rejection' = 'checksum mismatch'; 'traversal-rejection' = 'Unsafe update file path'; 'version-rejection' = 'manifest does not match'; 'content-hash-rejection' = 'checksum mismatch'; 'old-control-path-rejection' = 'Invalid manifest file path'; 'startup-failure-rolls-back' = 'startup failed' }
        if ($errors.ContainsKey($Name)) { Assert ($result.error -match $errors[$Name]) "Unexpected rejection reason ($Name): $($result.error)" }
        Write-Host "PASS $Name"
        return $result
    } finally { $env:PATH = $savedPath }
}
Write-Text (Join-Path $target '7ZipContextApp.exe') 'original application'
Write-Text (Join-Path $target 'unrelated.txt') 'preserve user file'
Write-Text (Join-Path $target 'retired.txt') 'old owned file'
Write-Text (Join-Path $target 'modified.txt') 'old data'
$oldFiles = @('retired.txt','modified.txt') | ForEach-Object { @{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $target $_)).Hash; size = (Get-Item -LiteralPath (Join-Path $target $_)).Length } }
Write-Text (Join-Path $target 'update-manifest.json') (@{ format = 1; version = '0.0.1'; architecture = 'x64'; files = @($oldFiles) } | ConvertTo-Json -Depth 5)
Write-Text (Join-Path $target 'modified.txt') 'user modified data'
Write-Manifest $stage $version
$validManifest = Get-Content -LiteralPath (Join-Path $stage 'update-manifest.json') -Raw
$valid = Zip-Stage 'valid'
$validHash = (Get-FileHash -LiteralPath $valid).Hash
$null = Run-Update 'checksum-rejection' $valid ('0'*64) $false
Assert ((Get-Content -LiteralPath (Join-Path $target '7ZipContextApp.exe') -Raw) -eq 'original application') 'Checksum failure changed original files.'

$manifest = $validManifest | ConvertFrom-Json
$manifest.files[0].path = '../escaped.txt'
Write-Text (Join-Path $stage 'update-manifest.json') ($manifest | ConvertTo-Json -Depth 5)
$invalid = Zip-Stage 'traversal'
$null = Run-Update 'traversal-rejection' $invalid (Get-FileHash -LiteralPath $invalid).Hash $false
Assert (-not (Test-Path -LiteralPath (Join-Path $root 'escaped.txt'))) 'Archive path escaped target.'
$manifest = $validManifest | ConvertFrom-Json; $manifest.version = '0.0.0'
Write-Text (Join-Path $stage 'update-manifest.json') ($manifest | ConvertTo-Json -Depth 5)
$invalid = Zip-Stage 'wrong-manifest-version'
$null = Run-Update 'version-rejection' $invalid (Get-FileHash -LiteralPath $invalid).Hash $false
$manifest = $validManifest | ConvertFrom-Json; $manifest.files[0].sha256 = '0'*64
Write-Text (Join-Path $stage 'update-manifest.json') ($manifest | ConvertTo-Json -Depth 5)
$invalid = Zip-Stage 'wrong-content-hash'
$null = Run-Update 'content-hash-rejection' $invalid (Get-FileHash -LiteralPath $invalid).Hash $false
Write-Text (Join-Path $stage 'update-manifest.json') $validManifest

$previousManifestPath = Join-Path $target 'update-manifest.json'
$previousManifest = Get-Content -LiteralPath $previousManifestPath -Raw
$invalidPrevious = $previousManifest | ConvertFrom-Json; $invalidPrevious.files[0].path = '.7zipcontext-update.lock'
Write-Text $previousManifestPath ($invalidPrevious | ConvertTo-Json -Depth 5)
$null = Run-Update 'old-control-path-rejection' $valid $validHash $false
Write-Text $previousManifestPath $previousManifest

$success = Run-Update 'complete-runtime-update' $valid $validHash $true
Assert ((Get-FileHash -LiteralPath (Join-Path $target '7ZipContextApp.exe')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $runtime '7ZipContextApp.exe')).Hash) 'New binary differs from release.'
Assert ((Get-Content -LiteralPath (Join-Path $target 'unrelated.txt') -Raw) -eq 'preserve user file') 'Unrelated file changed.'
Assert ((Get-Content -LiteralPath (Join-Path $target 'modified.txt') -Raw) -eq 'user modified data') 'Modified old file changed.'
Assert (-not (Test-Path -LiteralPath (Join-Path $target 'retired.txt'))) 'Unmodified retired file remains.'
Assert (Test-Path -LiteralPath (Join-Path $success.backup 'retired.txt')) 'Retired file has no backup.'

$before = @{}; Get-ChildItem -LiteralPath $target -Recurse -File | Where-Object FullName -NotMatch '[\\/]\.7zipcontext-' | ForEach-Object { $before[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName).Hash }
Copy-Item -LiteralPath $failureFixture -Destination (Join-Path $stage '7ZipContextApp.exe') -Force
Write-Manifest $stage $version
$broken = Zip-Stage 'broken-runtime'
$rollback = Run-Update 'startup-failure-rolls-back' $broken (Get-FileHash -LiteralPath $broken).Hash $false
Assert ($rollback.error -eq 'Updated application startup failed: 23') "Unexpected rollback trigger: $($rollback.error)"
foreach ($path in $before.Keys) { Assert ((Get-FileHash -LiteralPath $path).Hash -eq $before[$path]) "Rollback did not restore $path" }
Assert (@(Get-ChildItem -LiteralPath $target -Directory -Filter '.7zipcontext-update-stage-*').Count -eq 0) 'Update left a staging directory.'
Write-Host "Update helper integration tests passed. Artifacts: $root"
