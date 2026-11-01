param(
    [string]$RepositoryPath = (Join-Path $PSScriptRoot '../..'),
    [string]$Ref = $env:GITHUB_REF,
    [string]$GitHubOutput = $env:GITHUB_OUTPUT
)
$ErrorActionPreference = 'Stop'

function Invoke-GitQuery([string[]]$Arguments) {
    $result = & git -C $RepositoryPath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git version query failed: $($Arguments[0])" }
    return $result
}

$commit = Invoke-GitQuery -Arguments @('rev-parse', 'HEAD')
if ($Ref -like 'refs/tags/*') {
    $tag = $Ref.Substring('refs/tags/'.Length)
    if ($tag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Release tags must use vMAJOR.MINOR.PATCH, for example v1.2.3.'
    }
    $version = [version]$tag.Substring(1)
} else {
    $tags = @(Invoke-GitQuery -Arguments @('tag', '--merged', 'HEAD') | Where-Object { $_ -match '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' } |
        ForEach-Object { [pscustomobject]@{ Tag = $_; Version = [version]$_.Substring(1) } } |
        Sort-Object Version -Descending)
    if ($tags.Count) {
        $base = $tags[0]
        $distance = [int](Invoke-GitQuery -Arguments @('rev-list', '--count', "$($base.Tag)..HEAD"))
        $version = [version]::new($base.Version.Major, $base.Version.Minor, $base.Version.Build + $distance)
    } else {
        $version = [version]::new(1, 0, [int](Invoke-GitQuery -Arguments @('rev-list', '--count', 'HEAD')))
    }
    $tag = 'v' + $version.ToString(3)
}
if ($version.Major -gt 255 -or $version.Minor -gt 255 -or $version.Build -gt 65535) {
    throw 'MSI versions require MAJOR/MINOR <= 255 and PATCH <= 65535. Create a compatible version tag.'
}
if ($Ref -like 'refs/tags/*' -and (Invoke-GitQuery -Arguments @('rev-parse', "$tag^{commit}")) -ne $commit) {
    throw 'The requested release tag does not point to the checked-out commit.'
}
$result = [ordered]@{ version = $version.ToString(3); tag = $tag; commit = $commit }
if ($GitHubOutput) {
    foreach ($entry in $result.GetEnumerator()) {
        Add-Content -LiteralPath $GitHubOutput -Value "$($entry.Key)=$($entry.Value)" -Encoding utf8
    }
}
[pscustomobject]$result
