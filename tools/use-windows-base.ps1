param(
    [Parameter(Mandatory=$true)][string]$Repository,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$Downloads = 'downloaded-windows-base'
)
$ErrorActionPreference = 'Stop'
$helper = Join-Path $PSScriptRoot 'windows-base.py'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio 2022 C++ tools are required' }
$toolset = (Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" -Raw).Trim()
$linker = [System.Diagnostics.FileVersionInfo]::GetVersionInfo("$vs/VC/Tools/MSVC/$toolset/bin/Hostx64/x64/link.exe")
$linkerVersion = "$($linker.FileMajorPart).$($linker.FileMinorPart).$($linker.FileBuildPart).$($linker.FilePrivatePart)"
$fetched = & python $helper fetch --repo $Repository --directory $Downloads
if ($LASTEXITCODE -ne 0) { throw 'Windows base download or verification failed' }
$info = $fetched | ConvertFrom-Json
if (!$info.found) { throw 'Run Maintain base SDK with platform=windows for this recipe first; ordinary builds never rebuild dependencies implicitly.' }
$installed = & python $helper install --directory $Downloads --destination $Destination --linker-version $linkerVersion
if ($LASTEXITCODE -ne 0) { throw 'Windows base installation or compiler compatibility check failed' }
$info = $installed | ConvertFrom-Json
"DATAPUMP_WINDOWS_TOOLCHAIN=$($info.toolchain_file)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
Write-Host "Reused Windows dependencies $($info.recipe_id); toolchain $($info.toolchain_file)"
