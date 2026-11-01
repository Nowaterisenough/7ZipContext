$ErrorActionPreference = 'Stop'
$script = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../scripts/ci/Get-ReleaseVersion.ps1')).Path
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixture = Join-Path $temporaryRoot ('7ZipContext-release-version-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$checks = 0
function GitFixture([string[]]$Arguments) {
    & git -C $fixture @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Git fixture command failed.' }
}
function CheckVersion([string]$Expected, [string]$Ref = 'refs/heads/main') {
    $actual = & $script -RepositoryPath $fixture -Ref $Ref -GitHubOutput ''
    if ($actual.version -ne $Expected -or $actual.tag -ne "v$Expected" -or $actual.commit -notmatch '^[0-9a-f]{40}$') { throw "Expected $Expected, got $($actual.version)." }
    $script:checks++
}
function RejectVersion([string]$Ref) {
    $rejected = $false
    try { & $script -RepositoryPath $fixture -Ref $Ref -GitHubOutput '' | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid version was accepted: $Ref" }
    $script:checks++
}
try {
    GitFixture -Arguments @('init', '-q', '-b', 'main')
    GitFixture -Arguments @('config', 'user.name', 'Release Version Test')
    GitFixture -Arguments @('config', 'user.email', 'release-version@example.invalid')
    GitFixture -Arguments @('config', 'commit.gpgsign', 'false')
    GitFixture -Arguments @('config', 'tag.gpgsign', 'false')
    GitFixture -Arguments @('config', 'core.hooksPath', (Join-Path $fixture 'no-hooks'))
    GitFixture -Arguments @('commit', '--allow-empty', '-q', '-m', 'initial')
    CheckVersion '1.0.1'
    GitFixture -Arguments @('tag', 'v1.4.7')
    GitFixture -Arguments @('commit', '--allow-empty', '-q', '-m', 'next')
    CheckVersion '1.4.8'
    GitFixture -Arguments @('tag', 'v1.4.8')
    CheckVersion '1.4.8'
    RejectVersion 'refs/tags/v1.4.7'
    GitFixture -Arguments @('checkout', '-q', '-b', 'other')
    GitFixture -Arguments @('commit', '--allow-empty', '-q', '-m', 'other branch')
    GitFixture -Arguments @('tag', 'v9.0.0')
    GitFixture -Arguments @('checkout', '-q', 'main')
    CheckVersion '1.4.8'
    GitFixture -Arguments @('tag', 'v1.9.99')
    GitFixture -Arguments @('tag', 'v1.10.0')
    CheckVersion '1.10.0'
    GitFixture -Arguments @('tag', 'v2.3.4')
    CheckVersion '2.3.4' 'refs/tags/v2.3.4'
    foreach ($invalid in 'refs/tags/v1.2.3-rc.1','refs/tags/v01.2.3','refs/tags/v256.1.1','refs/tags/v1.0.65536') { RejectVersion $invalid }
    $output = Join-Path $fixture 'outputs.txt'
    & $script -RepositoryPath $fixture -Ref 'refs/tags/v2.3.4' -GitHubOutput $output | Out-Null
    $lines = @(Get-Content -LiteralPath $output)
    if ($lines.Count -ne 3 -or 'version=2.3.4' -notin $lines -or 'tag=v2.3.4' -notin $lines -or @($lines | Where-Object { $_ -match '^commit=[0-9a-f]{40}$' }).Count -ne 1) { throw 'Invalid GitHub Actions outputs.' }
    $checks++
    GitFixture -Arguments @('tag', 'v2.3.65535')
    GitFixture -Arguments @('commit', '--allow-empty', '-q', '-m', 'version overflow')
    RejectVersion 'refs/heads/main'
    Write-Output "Release version tests passed: $checks"
} finally {
    $resolved = (Resolve-Path -LiteralPath $fixture).Path
    if ($resolved -ne $fixture -or -not $resolved.StartsWith($temporaryRoot + '\7ZipContext-release-version-', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected fixture cleanup path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
