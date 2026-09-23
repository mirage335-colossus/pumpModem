param([switch]$GitHubEnvironment)

$ErrorActionPreference = 'Stop'

function Get-DataPumpToolFileVersion([string]$Path) {
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    if ($version.FileMajorPart -eq 0) { throw "Missing compiler version information: $Path" }
    return "$($version.FileMajorPart).$($version.FileMinorPart).$($version.FileBuildPart).$($version.FilePrivatePart)"
}

function Find-DataPumpWindowsToolchain($Instances, [string[]]$Generators) {
    # Prefer the original VS2022 installation, but use its v143 compiler under
    # VS2026 when that is the installed IDE. Never silently choose v145.
    $instancesInOrder = @($Instances | Where-Object {
        $_.isComplete -and ([version]$_.installationVersion).Major -in @(17, 18)
    } | Sort-Object @{Expression = { ([version]$_.installationVersion).Major }},
                    @{Expression = { [version]$_.installationVersion }; Descending = $true})
    foreach ($instance in $instancesInOrder) {
        $major = ([version]$instance.installationVersion).Major
        $generator = if ($major -eq 17) { 'Visual Studio 17 2022' } else { 'Visual Studio 18 2026' }
        $toolsDirectory = Join-Path $instance.installationPath 'VC/Tools/MSVC'
        if (!(Test-Path -LiteralPath $toolsDirectory -PathType Container)) { continue }
        $toolsets = @(Get-ChildItem -LiteralPath $toolsDirectory -Directory | Where-Object {
            $_.Name -match '^14\.(3[0-9]|4[0-9])\.\d+$'
        } | Sort-Object { [version]$_.Name } -Descending)
        foreach ($toolset in $toolsets) {
            $compiler = Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
            $linker = Join-Path $toolset.FullName 'bin/Hostx64/x64/link.exe'
            if (!(Test-Path -LiteralPath $compiler -PathType Leaf) -or
                !(Test-Path -LiteralPath $linker -PathType Leaf)) { continue }
            if ($generator -notin $Generators) {
                throw "CMake does not support $generator. VS2026 requires CMake 4.2 or newer."
            }
            return [pscustomobject]@{
                Generator = $generator
                Instance = $instance.installationPath
                Toolset = "v143,version=$($toolset.Name),host=x64"
                ToolsetVersion = $toolset.Name
                Compiler = $compiler
                Linker = $linker
                CompilerVersion = Get-DataPumpToolFileVersion $compiler
                LinkerVersion = Get-DataPumpToolFileVersion $linker
            }
        }
    }
    throw 'An installed VS2022 or VS2026 with the MSVC v143 x64/x86 build tools is required; the existing Windows base is not rebuilt implicitly.'
}

function Set-DataPumpWindowsToolchainEnvironment($Toolchain, [string]$EnvironmentFile) {
    $values = [ordered]@{
        CMAKE_GENERATOR = $Toolchain.Generator
        CMAKE_GENERATOR_INSTANCE = $Toolchain.Instance
        CMAKE_GENERATOR_TOOLSET = $Toolchain.Toolset
        DATAPUMP_VS_ROOT = $Toolchain.Instance
        DATAPUMP_MSVC_TOOLSET_VERSION = $Toolchain.ToolsetVersion
        DATAPUMP_MSVC_COMPILER_VERSION = $Toolchain.CompilerVersion
        DATAPUMP_MSVC_LINKER_VERSION = $Toolchain.LinkerVersion
    }
    foreach ($entry in $values.GetEnumerator()) {
        if ($entry.Value -match '[\r\n]') { throw 'Unexpected newline in Windows compiler metadata' }
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
        if ($EnvironmentFile) {
            "$($entry.Key)=$($entry.Value)" | Out-File -LiteralPath $EnvironmentFile -Encoding utf8 -Append
        }
    }
}

# Dot-sourcing exposes the selection functions for the filesystem fixture tests.
if ($MyInvocation.InvocationName -eq '.') { return }
if ($GitHubEnvironment -and !$env:GITHUB_ENV) { throw 'GITHUB_ENV is required with -GitHubEnvironment' }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio Installer vswhere.exe was not found' }
$inventory = & $vswhere -all -version '[17.0,19.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed' }
$capabilities = & cmake -E capabilities
if ($LASTEXITCODE -ne 0) { throw 'CMake generator discovery failed' }
$selected = Find-DataPumpWindowsToolchain ($inventory | ConvertFrom-Json) @((($capabilities | ConvertFrom-Json).generators).name)
$environmentFile = if ($GitHubEnvironment) { $env:GITHUB_ENV } else { $null }
Set-DataPumpWindowsToolchainEnvironment $selected $environmentFile
Write-Host "Selected $($selected.Generator), $($selected.Toolset); linker $($selected.LinkerVersion) at $($selected.Instance)"
return $selected
