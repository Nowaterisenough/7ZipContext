param(
    [Parameter(Mandatory = $true)][string]$StagingDir,
    [Parameter(Mandatory = $true)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
$staging = (Resolve-Path -LiteralPath $StagingDir).Path
$namespace = 'http://wixtoolset.org/schemas/v4/wxs'
$document = [System.Xml.XmlDocument]::new()
function Element([string]$name, [hashtable]$attributes = @{}) {
    $node = $document.CreateElement($name, $namespace)
    foreach ($key in $attributes.Keys) { $node.SetAttribute($key, [string]$attributes[$key]) }
    return ,$node
}
function StableId([string]$prefix, [string]$path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $hash = $algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($path.ToLowerInvariant())) }
    finally { $algorithm.Dispose() }
    return $prefix + ([BitConverter]::ToString($hash).Replace('-', '').Substring(0, 24))
}
$wix = Element 'Wix'
[void]$document.AppendChild($wix)
$fragment = Element 'Fragment'
[void]$wix.AppendChild($fragment)
$directory = Element 'DirectoryRef' @{ Id = 'INSTALLFOLDER' }
[void]$fragment.AppendChild($directory)
$group = Element 'ComponentGroup' @{ Id = 'DesktopRuntime' }
[void]$fragment.AppendChild($group)
$directories = @{ '' = $directory }
function EnsureDirectory([string]$relative) {
    if ($directories.ContainsKey($relative)) { return ,$directories[$relative] }
    $parent = Split-Path -Parent $relative
    $node = Element 'Directory' @{ Id = (StableId 'RuntimeDir' $relative); Name = (Split-Path -Leaf $relative) }
    $parentNode = EnsureDirectory $parent
    [void]$parentNode.AppendChild($node)
    $directories[$relative] = $node
    return ,$node
}
$explicitFiles = @('7ZipContext.dll', '7ZipContextApp.exe', 'AppxManifest.xml', 'register.ps1', 'unregister.ps1', 'Assets\icon.png')
foreach ($file in Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName) {
    $relative = $file.FullName.Substring($staging.Length + 1)
    if ($relative -in $explicitFiles -or $file.Extension -in '.pdb', '.pfx', '.log' -or $file.Name -like '*.previous-*.exe') { continue }
    $id = StableId 'Runtime' $relative
    $component = Element 'Component' @{ Id = $id; Guid = '*'; Bitness = 'always64' }
    [void]$component.AppendChild((Element 'File' @{ Id = ($id + 'File'); Source = $file.FullName; KeyPath = 'yes' }))
    $parent = EnsureDirectory (Split-Path -Parent $relative)
    [void]$parent.AppendChild($component)
    [void]$group.AppendChild((Element 'ComponentRef' @{ Id = $id }))
}
$document.Save([IO.Path]::GetFullPath($OutputFile))
Write-Output "Runtime files collected: $($group.ChildNodes.Count)"
