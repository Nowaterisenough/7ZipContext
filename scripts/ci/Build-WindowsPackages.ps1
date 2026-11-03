param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][ValidateSet('msvc','ucrt')][string]$Toolchain,
    [string]$InstallDir = 'build/ci-install',
    [string]$OutputDir = 'build/release-packages',
    [string]$DotnetExecutable = 'dotnet',
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Expected a three-part numeric version.' }
$numericVersion = [version]$Version
if ($numericVersion.Major -gt 255 -or $numericVersion.Minor -gt 255 -or $numericVersion.Build -gt 65535) { throw 'Version exceeds MSI limits.' }
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$buildInfo = Get-Content -LiteralPath (Join-Path $BuildDirectory 'toolchain-info.json') -Raw | ConvertFrom-Json
if ($buildInfo.toolchain -ne $Toolchain -or -not $buildInfo.compilerVersion -or $buildInfo.qtVersion -notmatch '^6\.12\.') {
    throw 'Build metadata does not match the requested toolchain or Qt 6.12.'
}
$install = (Resolve-Path -LiteralPath $InstallDir).Path
$output = [IO.Path]::GetFullPath($OutputDir)
if ($output -eq $install -or $output.StartsWith($install.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package output must be outside the installed runtime directory.' }
if ((Get-Item -LiteralPath $install).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installed runtime directory must not be a link.' }
if ((Test-Path -LiteralPath $output) -and @(Get-ChildItem -LiteralPath $output -Force).Count) { throw 'Package output directory must be empty.' }
$work = Join-Path (Split-Path -Parent $output) ('packaging-' + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $work 'stage'
New-Item -ItemType Directory -Path $output,$stage -Force | Out-Null

foreach ($item in Get-ChildItem -LiteralPath $install -Recurse -Force) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installed runtime must not contain links.' }
    if ($item.PSIsContainer -or $item.Extension -in '.pdb','.pfx','.log' -or $item.Name -like '*.previous-*.exe' -or $item.Name -like '*.full-test-*.exe') { continue }
    $relative = [IO.Path]::GetRelativePath($install, $item.FullName)
    if ($relative -match '(^|[\\/])\.7zipcontext-' -or $relative -eq 'update-manifest.json') { continue }
    $destination = Join-Path $stage $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath $item.FullName -Destination $destination
}
foreach ($script in 'register.ps1','unregister.ps1') {
    Copy-Item -LiteralPath (Join-Path $repository "installer/$script") -Destination (Join-Path $stage $script) -Force
}
if ($Toolchain -eq 'msvc') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $visualStudio) { throw 'Cannot locate the MSVC redistributable runtime.' }
    $redistRoot = Join-Path $visualStudio 'VC/Redist/MSVC'
    $redists = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+(\.\d+)?$' } | Sort-Object { [version]$_.Name } -Descending)
    $crt = $null
    foreach ($redist in $redists) {
        $crt = Get-ChildItem -LiteralPath (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($crt) { break }
    }
    if (-not $crt) { throw 'No x64 MSVC CRT directory was found.' }
    Get-ChildItem -LiteralPath $crt.FullName -File -Filter '*.dll' | Copy-Item -Destination $stage -Force
    foreach ($name in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $name))) { throw "Missing MSVC runtime: $name" }
    }
}
$required = @('7ZipContextApp.exe','7ZipContext.dll','Apply-Update.ps1','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll','Qt6Svg.dll','plugins/platforms/qwindows.dll','AppxManifest.xml','Assets/icon.png','Assets/icon.ico','Assets/StoreLogo.png','Assets/Square44x44Logo.png','Assets/Square150x150Logo.png')
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $relative) -PathType Leaf)) { throw "Missing release runtime file: $relative" }
}
if ([Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $stage '7ZipContextApp.exe')).ProductVersion -ne $Version) { throw 'Executable version differs from the package version. Configure CMake with -D7ZIPCONTEXT_VERSION first.' }
$manifestPath = Join-Path $stage 'AppxManifest.xml'
[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
$manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Identity']").SetAttribute('Version', "$Version.0")
$manifest.Save($manifestPath)
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination (Join-Path $licenses '7ZipContext-MIT.txt')
foreach ($name in 'License.txt','copying.txt','unRarLicense.txt') {
    Copy-Item -LiteralPath (Join-Path $repository "3rd/7zip/DOC/$name") -Destination (Join-Path $licenses "7zip-$name")
}
$updateFiles = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/'); size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
})
$updateManifest = [ordered]@{ format = 1; version = $Version; architecture = 'x64'; toolchain = $Toolchain; files = $updateFiles }
[IO.File]::WriteAllText((Join-Path $stage 'update-manifest.json'),($updateManifest | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))

Push-Location $repository
try {
    $commit = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
    $dirty = -not [string]::IsNullOrWhiteSpace((& git status --porcelain | Out-String))
    $toolVersion = (Get-Content -LiteralPath '.config/dotnet-tools.json' -Raw | ConvertFrom-Json).tools.wix.version
    & $DotnetExecutable tool restore
    if ($LASTEXITCODE -ne 0) { throw 'WiX tool restore failed.' }
    foreach ($extension in 'WixToolset.UI.wixext','WixToolset.Util.wixext') {
        & $DotnetExecutable tool run wix -- extension add "$extension/$toolVersion"
        if ($LASTEXITCODE -ne 0) { throw "WiX extension restore failed: $extension" }
    }
    $runtime = Join-Path $work 'runtime-files.wxs'
    & (Join-Path $repository 'installer/collect-runtime.ps1') -StagingDir $stage -OutputFile $runtime
    $msi = Join-Path $output "7ZipContext-$Version-windows-x64-$Toolchain.msi"
    & $DotnetExecutable tool run wix -- build (Join-Path $repository 'installer/7ZipContext.wxs') $runtime `
        -d "StagingDir=$stage" -d "InstallerDir=$(Join-Path $repository 'installer')" -d "ProductVersion=$Version" `
        -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -arch x64 `
        -intermediateFolder (Join-Path $work 'wixobj') -pdbtype none -o $msi
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $msi)) { throw 'MSI packaging failed.' }
} finally { Pop-Location }

# Compare every staged component against the MSI file table, including Qt plugins and runtime DLLs.
$expected = @{
    SevenZipContextDLL = '7ZipContext.dll'; DesktopEXE = '7ZipContextApp.exe'; AppxManifestXML = 'AppxManifest.xml'
    RegisterPS1 = 'register.ps1'; UnregisterPS1 = 'unregister.ps1'; IconFile = 'Assets/icon.png'
}
$sizes = @{}
foreach ($id in $expected.Keys) { $sizes[$id] = (Get-Item -LiteralPath (Join-Path $stage $expected[$id])).Length }
[xml]$runtimeXml = Get-Content -LiteralPath $runtime -Raw
foreach ($file in $runtimeXml.SelectNodes("//*[local-name()='File']")) { $sizes[$file.GetAttribute('Id')] = (Get-Item -LiteralPath $file.GetAttribute('Source')).Length }
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $null; $view = $null
try {
    $database = $installer.OpenDatabase($msi, 0)
    $view = $database.OpenView('SELECT File, FileSize FROM File'); $view.Execute()
    $actual = @{}
    while ($record = $view.Fetch()) { $actual[$record.StringData(1)] = [long]$record.IntegerData(2) }
    $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view); $view = $null
    if ($actual.Count -ne $sizes.Count) { throw 'MSI runtime file count differs from staging.' }
    foreach ($id in $sizes.Keys) { if (-not $actual.ContainsKey($id) -or $actual[$id] -ne $sizes[$id]) { throw "MSI component missing or truncated: $id" } }
    $view = $database.OpenView("SELECT Value FROM Property WHERE Property = 'ProductVersion'"); $view.Execute()
    if ($view.Fetch().StringData(1) -ne $Version) { throw 'MSI version does not match the release version.' }
} finally {
    if ($view) { $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
    if ($database) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}

$zip = Join-Path $output "7ZipContext-$Version-windows-x64-$Toolchain-portable.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
$smokeDirectory = Join-Path $work 'portable-check'
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $smokeDirectory)
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = Start-Process -FilePath (Join-Path $smokeDirectory '7ZipContextApp.exe') -ArgumentList '--smoke-test' -WorkingDirectory $smokeDirectory -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(20000)) { $process.Kill(); throw 'Portable app smoke test timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable app smoke test failed: $($process.ExitCode)" }
} finally { $env:PATH = $savedPath }

[ordered]@{
    version = $Version; commit = $commit; dirtyWorkingTree = $dirty; architecture = 'x64'
    toolchain = $Toolchain; qtVersion = $buildInfo.qtVersion
    compilerId = $buildInfo.compilerId; compilerVersion = $buildInfo.compilerVersion
    wixVersion = $toolVersion; runtimeFiles = $sizes.Count; portableSmokeTest = 'passed'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output "build-info-$Toolchain.json") -Encoding utf8
$checksums = @(Get-ChildItem -LiteralPath $output -File | Sort-Object Name | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name })
[IO.File]::WriteAllText((Join-Path $output "SHA256SUMS-$Toolchain.txt"), ($checksums -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length
