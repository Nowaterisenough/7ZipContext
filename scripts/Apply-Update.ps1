param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$Sha256,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$TargetDirectory,
    [ValidateSet('portable','msi')][string]$Mode = 'portable',
    [int]$ParentPid = 0,
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'
# PowerShell 7 parents can pass a module path incompatible with Windows PowerShell 5.1.
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
$result = [ordered]@{ success = $false; version = $Version; error = ''; backup = '' }
$target = [IO.Path]::GetFullPath($TargetDirectory).TrimEnd('\')
$packagePath = [IO.Path]::GetFullPath($Package)
$resultFile = Join-Path (Split-Path -Parent $packagePath) 'update-result.json'
$executable = Join-Path $target '7ZipContextApp.exe'
$lock = $null; $stage = ''; $backup = ''; $installed = @{}; $saved = @(); $mayRestart = $false

function Safe-Path([string]$Root, [string]$Relative) {
    $relativePath = $Relative.Replace('\','/')
    if (-not $relativePath -or $relativePath.StartsWith('/') -or $relativePath -match '[\x00-\x1f:*?"<>|]') { throw 'Invalid update file path.' }
    foreach ($part in $relativePath.Split('/')) {
        if (-not $part -or $part -in '.','..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Unsafe update file path.' }
    }
    $full = [IO.Path]::GetFullPath((Join-Path $Root $relativePath))
    if (-not $full.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Update file escaped its directory.' }
    return $full
}
function Check-Links([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Updates cannot follow filesystem links.' }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }; $current = $parent
    }
}
function Move-OwnedFile([string]$From, [string]$To) {
    Check-Links $From; Check-Links $To
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To)) | Out-Null
    Move-Item -LiteralPath $From -Destination $To -ErrorAction Stop
}
function Check-ManifestPath([string]$Relative) {
    [void](Safe-Path $target $Relative)
    if ($Relative.Contains('\') -or $Relative -match '^\.7zipcontext-' -or $Relative -eq 'update-manifest.json') { throw 'Invalid manifest file path.' }
}
function Remove-Stage {
    if (-not $stage -or -not (Test-Path -LiteralPath $stage)) { return }
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ($resolved -ne $stage -or -not $resolved.StartsWith($target+'\.7zipcontext-update-stage-',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected update staging path.' }
    $pending = New-Object 'Collections.Generic.Queue[string]'; $pending.Enqueue($resolved)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Staging directory contains a link.' }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
        }
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
try {
    if ($Sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid update identity.' }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Target is not a 7-Zip Context installation.' }
    Check-Links $target; Check-Links $packagePath
    if ((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash -ne $Sha256) { throw 'Update checksum mismatch.' }
    if ($ParentPid -gt 0) {
        $parent = Get-Process -Id $ParentPid -ErrorAction SilentlyContinue
        if ($parent) {
            if ([IO.Path]::GetFullPath($parent.Path) -ne $executable) { throw 'Update parent does not match the application.' }
            if (-not $parent.WaitForExit(120000)) { throw 'Application is still running; update was not applied.' }
        }
    }
    $mayRestart = $true
    foreach ($running in @(Get-Process 7ZipContextApp -ErrorAction SilentlyContinue)) {
        if ($running.Path -and [IO.Path]::GetFullPath($running.Path) -eq $executable) { $mayRestart = $false; throw 'Another application task is still running.' }
    }
    if ($Mode -eq 'msi') {
        $installer = Join-Path $env:SystemRoot 'System32\msiexec.exe'
        $database = $null; $windowsInstaller = New-Object -ComObject WindowsInstaller.Installer
        try {
            $database = $windowsInstaller.OpenDatabase($packagePath, 0)
            foreach ($property in @{ ProductVersion = $Version; UpgradeCode = '{A1B2C3D4-0000-4B3A-9E1F-000000000001}' }.GetEnumerator()) {
                $view = $database.OpenView("SELECT Value FROM Property WHERE Property = '$($property.Key)'"); $view.Execute()
                try { if ($view.Fetch().StringData(1) -ne $property.Value) { throw 'Installer identity does not match the release.' } }
                finally { $view.Close(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) }
            }
        } finally {
            if ($database) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowsInstaller)
        }
        $process = Start-Process -FilePath $installer -ArgumentList @('/i',('"'+$packagePath+'"'),'/passive','/norestart') -Verb RunAs -WindowStyle Hidden -PassThru
        $process.WaitForExit()
        if ($process.ExitCode -notin 0,3010) { throw "Installer failed: $($process.ExitCode)" }
        if ([Diagnostics.FileVersionInfo]::GetVersionInfo($executable).ProductVersion -ne $Version) { throw 'Installer completed, but the target application has not been updated.' }
        $result.success = $true
    } else {
        $lock = [IO.File]::Open((Join-Path $target '.7zipcontext-update.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
        try {
            $manifestEntry = @($archive.Entries | Where-Object FullName -EQ 'update-manifest.json')
            if ($manifestEntry.Count -ne 1 -or $manifestEntry[0].Length -gt 4194304) { throw 'Update manifest is missing or too large.' }
            $reader = New-Object IO.StreamReader($manifestEntry[0].Open(),[Text.Encoding]::UTF8)
            try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
            if ($manifest.format -ne 1 -or $manifest.version -ne $Version -or $manifest.architecture -ne 'x64' -or @($manifest.files).Count -gt 10000) { throw 'Update manifest does not match the release.' }
            $expected = @{}; $total = [long]0
            foreach ($file in $manifest.files) {
                $relative = [string]$file.path; Check-ManifestPath $relative
                if ($expected.ContainsKey($relative) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $null -eq $file.size -or [long]$file.size -lt 0 -or [long]$file.size -gt 2147483648) { throw 'Invalid update manifest entry.' }
                $expected[$relative] = $file; $total += [long]$file.size
                if ($total -gt 2147483648) { throw 'Update unpacked size exceeds the limit.' }
            }
            foreach ($required in '7ZipContextApp.exe','Apply-Update.ps1','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll','Qt6Svg.dll','plugins/platforms/qwindows.dll') {
                if (-not $expected.ContainsKey($required)) { throw "Incomplete runtime: $required" }
            }
            $found = @{}
            foreach ($entry in $archive.Entries) {
                if ($entry.FullName.EndsWith('/')) { [void](Safe-Path $target $entry.FullName.TrimEnd('/')); continue }
                [void](Safe-Path $target $entry.FullName)
                if ($found.ContainsKey($entry.FullName)) { throw 'Duplicate update archive entry.' }
                $found[$entry.FullName] = $entry
                if ($entry.FullName -ne 'update-manifest.json' -and (-not $expected.ContainsKey($entry.FullName) -or $entry.Length -ne [long]$expected[$entry.FullName].size)) { throw 'Unexpected update archive contents.' }
            }
            if ($found.Count -ne $expected.Count+1) { throw 'Update archive is incomplete.' }
            $operation = [guid]::NewGuid().ToString('N')
            $stage = Safe-Path $target ('.7zipcontext-update-stage-'+$operation)
            $backup = Safe-Path $target ('.7zipcontext-update-backup-'+$operation)
            New-Item -ItemType Directory -Path $stage,$backup | Out-Null
            foreach ($relative in $found.Keys) {
                $destination = Safe-Path $stage $relative
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
                $input = $found[$relative].Open(); $output = [IO.File]::Open($destination,[IO.FileMode]::CreateNew)
                try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
                if ($relative -ne 'update-manifest.json' -and (Get-FileHash -LiteralPath $destination).Hash -ne $expected[$relative].sha256) { throw 'Extracted update file checksum mismatch.' }
                Check-Links (Safe-Path $target $relative)
            }
        } finally { $archive.Dispose() }
        $replace = @($expected.Keys) + 'update-manifest.json'
        $previousManifest = Join-Path $target 'update-manifest.json'
        if (Test-Path -LiteralPath $previousManifest) {
            Check-Links $previousManifest
            if ((Get-Item -LiteralPath $previousManifest).Length -gt 4194304) { throw 'Previous manifest is too large.' }
            $previous = Get-Content -LiteralPath $previousManifest -Raw | ConvertFrom-Json
            if ($previous.format -ne 1 -or $previous.architecture -ne 'x64' -or @($previous.files).Count -gt 10000) { throw 'Invalid previous manifest.' }
            $previousPaths = @{}
            foreach ($file in $previous.files) {
                Check-ManifestPath ([string]$file.path)
                if ($previousPaths.ContainsKey([string]$file.path) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid previous manifest entry.' }
                $previousPaths[[string]$file.path] = $true
                $path = Safe-Path $target ([string]$file.path)
                Check-Links $path
                if (-not $expected.ContainsKey([string]$file.path) -and (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-FileHash -LiteralPath $path).Hash -eq $file.sha256) { $replace += [string]$file.path }
            }
        }
        foreach ($relative in $replace) {
            $path = Safe-Path $target $relative; Check-Links $path
            if (Test-Path -LiteralPath $path -PathType Container) { throw 'An update file conflicts with an existing directory.' }
        }
        foreach ($relative in ($replace | Sort-Object -Unique)) {
            $path = Safe-Path $target $relative
            if (Test-Path -LiteralPath $path) { Move-OwnedFile $path (Safe-Path $backup $relative); $saved += $relative }
            $new = Safe-Path $stage $relative
            if (Test-Path -LiteralPath $new) {
                $newHash = (Get-FileHash -LiteralPath $new).Hash
                Move-OwnedFile $new $path; $installed[$relative] = $newHash
            }
        }
        if ([Diagnostics.FileVersionInfo]::GetVersionInfo($executable).ProductVersion -ne $Version) { throw 'Updated executable version does not match the release.' }
        # A real launch check catches missing runtime DLLs before accepting the update.
        Add-Type 'using System.Runtime.InteropServices; public static class UpdateErrorMode { [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode); }'
        [void][UpdateErrorMode]::SetErrorMode(0x8003)
        # ShellExecute does not reliably inherit the caller's error mode.
        $smokeInfo = New-Object Diagnostics.ProcessStartInfo
        $smokeInfo.FileName = $executable; $smokeInfo.Arguments = '--smoke-test'; $smokeInfo.WorkingDirectory = $target
        $smokeInfo.UseShellExecute = $false; $smokeInfo.CreateNoWindow = $true; $smokeInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $smoke = [Diagnostics.Process]::Start($smokeInfo)
        if (-not $smoke.WaitForExit(20000)) { $smoke.Kill(); $smoke.WaitForExit(); throw 'Updated application did not start.' }
        if ($smoke.ExitCode -ne 0) { throw "Updated application startup failed: $($smoke.ExitCode)" }
        $result.success = $true; $result.backup = $backup
    }
} catch {
    $result.error = $_.Exception.Message
    if ($backup) {
        try {
            foreach ($relative in ($installed.Keys | Sort-Object -Descending)) {
                $path = Safe-Path $target $relative
                Check-Links $path
                if (Test-Path -LiteralPath $path) {
                    if ((Get-FileHash -LiteralPath $path).Hash -ne $installed[$relative]) { throw 'Updated file was modified; preserve it and the original backup.' }
                    Move-OwnedFile $path (Safe-Path $stage $relative)
                }
            }
            foreach ($relative in $saved) { Move-OwnedFile (Safe-Path $backup $relative) (Safe-Path $target $relative) }
        } catch { $result.error += ' Rollback requires attention; original files remain in the backup directory.' }
        $result.backup = $backup
    }
} finally {
    if ($lock) { $lock.Dispose() }
    try { Remove-Stage } catch { }
    [IO.File]::WriteAllText($resultFile,($result | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    if (-not $NoRestart -and $mayRestart -and (Test-Path -LiteralPath $executable)) { Start-Process -FilePath $executable -ArgumentList '--tray' -WorkingDirectory $target -WindowStyle Hidden | Out-Null }
}
if ($result.success) { exit 0 } else { exit 1 }
