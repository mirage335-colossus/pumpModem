param(
    [Parameter(Mandatory=$true)][string]$Repository,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$Downloads = 'downloaded-windows-base',
    [string]$SourceRoot = (Get-Location).Path,
    [string]$ReleaseTag = '',
    [string]$InventorySha256 = ''
)
$ErrorActionPreference = 'Stop'
$helper = Join-Path (Resolve-Path -LiteralPath $SourceRoot).Path 'tools/windows-base.py'
if (!(Test-Path -LiteralPath $helper -PathType Leaf)) { throw "Windows base recipe helper was not found: $helper" }
if ($InventorySha256 -and !$ReleaseTag) { throw 'InventorySha256 requires ReleaseTag' }
$toolchain = & (Join-Path $PSScriptRoot 'select-windows-toolchain.ps1') -GitHubEnvironment
$info = $null
if ($ReleaseTag) {
    $dependencyHelper = Join-Path $PSScriptRoot 'release-dependencies.py'
    $arguments = @($dependencyHelper, 'fetch', '--binary-only', '--repo', $Repository, '--tag', $ReleaseTag,
                   '--kind', 'windows-base', '--directory', $Downloads)
    if ($InventorySha256) { $arguments += @('--inventory-sha256', $InventorySha256) }
    $fetched = & python @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Release Windows dependency download or verification failed' }
    $info = $fetched | ConvertFrom-Json
}
if (!$info -or !$info.found) {
    # Legacy releases have no embedded dependency inventory.
    $fetched = & python $helper fetch --repo $Repository --directory $Downloads
    if ($LASTEXITCODE -ne 0) { throw 'Windows base download or verification failed' }
    $info = $fetched | ConvertFrom-Json
}
if (!$info.found) { throw 'Run Maintain base SDK with platform=windows for this recipe first; ordinary builds never rebuild dependencies implicitly.' }
$installed = & python $helper install --directory $Downloads --destination $Destination --linker-version $toolchain.LinkerVersion
if ($LASTEXITCODE -ne 0) { throw 'Windows base installation or compiler compatibility check failed' }
$info = $installed | ConvertFrom-Json
"DATAPUMP_WINDOWS_TOOLCHAIN=$($info.toolchain_file)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
Write-Host "Reused Windows dependencies $($info.recipe_id); toolchain $($info.toolchain_file)"
