param(
    [Parameter(Mandatory=$true)][string]$Repository,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$Downloads = 'downloaded-windows-base',
    [string]$SourceRoot = (Get-Location).Path
)
$ErrorActionPreference = 'Stop'
$helper = Join-Path (Resolve-Path -LiteralPath $SourceRoot).Path 'tools/windows-base.py'
if (!(Test-Path -LiteralPath $helper -PathType Leaf)) { throw "Windows base recipe helper was not found: $helper" }
$toolchain = & (Join-Path $PSScriptRoot 'select-windows-toolchain.ps1') -GitHubEnvironment
$fetched = & python $helper fetch --repo $Repository --directory $Downloads
if ($LASTEXITCODE -ne 0) { throw 'Windows base download or verification failed' }
$info = $fetched | ConvertFrom-Json
if (!$info.found) { throw 'Run Maintain base SDK with platform=windows for this recipe first; ordinary builds never rebuild dependencies implicitly.' }
$installed = & python $helper install --directory $Downloads --destination $Destination --linker-version $toolchain.LinkerVersion
if ($LASTEXITCODE -ne 0) { throw 'Windows base installation or compiler compatibility check failed' }
$info = $installed | ConvertFrom-Json
"DATAPUMP_WINDOWS_TOOLCHAIN=$($info.toolchain_file)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
Write-Host "Reused Windows dependencies $($info.recipe_id); toolchain $($info.toolchain_file)"
