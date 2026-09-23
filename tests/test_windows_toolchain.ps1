$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/select-windows-toolchain.ps1')

function Assert-Equal($Actual, $Expected, [string]$Context) {
    if ($Actual -cne $Expected) { throw "${Context}: expected '$Expected', got '$Actual'" }
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        return
    }
    throw "Expected error containing '$Message'"
}

# Test selection against real directory inventories, substituting only PE file
# metadata so these small fixtures do not need another compiler installation.
function Get-DataPumpToolFileVersion([string]$Path) {
    return (Get-Content -LiteralPath $Path -Raw).Trim()
}
function New-Instance([string]$Name, [string]$Version, [string[]]$Toolsets) {
    $root = Join-Path $scratch $Name
    foreach ($toolsetVersion in $Toolsets) {
        $bin = Join-Path $root "VC/Tools/MSVC/$toolsetVersion/bin/Hostx64/x64"
        New-Item -ItemType Directory -Path $bin -Force | Out-Null
        '19.44.35224.0' | Set-Content -LiteralPath (Join-Path $bin 'cl.exe')
        '14.44.35224.0' | Set-Content -LiteralPath (Join-Path $bin 'link.exe')
    }
    return [pscustomobject]@{installationPath = $root; installationVersion = $Version; isComplete = $true}
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) "datapump-toolchain-$([guid]::NewGuid())"
$environmentNames = @('CMAKE_GENERATOR', 'CMAKE_GENERATOR_INSTANCE', 'CMAKE_GENERATOR_TOOLSET',
    'DATAPUMP_VS_ROOT', 'DATAPUMP_MSVC_TOOLSET_VERSION', 'DATAPUMP_MSVC_COMPILER_VERSION', 'DATAPUMP_MSVC_LINKER_VERSION')
$previousEnvironment = @{}
foreach ($name in $environmentNames) { $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $generators = @('Visual Studio 17 2022', 'Visual Studio 18 2026')
    $vs2022 = New-Instance 'VS 2022' '17.14.36015.10' @('14.43.34808', '14.44.35207')
    $vs2026 = New-Instance 'VS 2026' '18.9.12120.119' @('14.44.35207', '14.51.36247')
    $selected = Find-DataPumpWindowsToolchain @($vs2026, $vs2022) $generators
    Assert-Equal $selected.Instance $vs2022.installationPath 'Prefer VS2022 when both IDEs are installed'
    Assert-Equal $selected.Toolset 'v143,version=14.44.35207,host=x64' 'Pin the newest installed v143 version'
    Assert-Equal $selected.LinkerVersion '14.44.35224.0' 'Read actual linker patch version rather than directory name'

    $selected = Find-DataPumpWindowsToolchain @($vs2026) $generators
    Assert-Equal $selected.Generator 'Visual Studio 18 2026' 'Use the installed VS2026 generator'
    Assert-Equal $selected.ToolsetVersion '14.44.35207' 'Reject VS2026 default v145 compiler'
    Assert-Equal $selected.Compiler (Join-Path $vs2026.installationPath 'VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe') 'Use the pinned v143 compiler path'
    Assert-Throws { Find-DataPumpWindowsToolchain @($vs2026) @('Visual Studio 17 2022') } 'CMake 4.2 or newer'

    $noV143 = New-Instance 'VS 2026 without v143' '18.9.12120.119' @('14.51.36247')
    Assert-Throws { Find-DataPumpWindowsToolchain @($noV143) $generators } 'MSVC v143'
    $vs2022.isComplete = $false
    Assert-Equal (Find-DataPumpWindowsToolchain @($vs2022, $vs2026) $generators).Instance $vs2026.installationPath 'Skip an incomplete installation'
    Remove-Item -LiteralPath (Join-Path $vs2026.installationPath 'VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/link.exe')
    Assert-Throws { Find-DataPumpWindowsToolchain @($vs2026) $generators } 'MSVC v143'

    $environmentFile = Join-Path $scratch 'github environment.txt'
    Set-DataPumpWindowsToolchainEnvironment $selected $environmentFile
    Assert-Equal $env:CMAKE_GENERATOR $selected.Generator 'Configure the current process'
    Assert-Equal $env:CMAKE_GENERATOR_TOOLSET $selected.Toolset 'Keep CMake on v143'
    Assert-Equal $env:DATAPUMP_MSVC_LINKER_VERSION $selected.LinkerVersion 'Expose the compatibility version'
    $saved = Get-Content -LiteralPath $environmentFile
    Assert-Equal $saved.Count 7 'Persist all compiler metadata for subsequent job steps'
    Assert-Equal $saved[1] "CMAKE_GENERATOR_INSTANCE=$($selected.Instance)" 'Preserve installation paths with spaces'
    $selected.Instance = "unexpected`nvalue"
    Assert-Throws { Set-DataPumpWindowsToolchainEnvironment $selected $environmentFile } 'Unexpected newline'
    Write-Host 'Windows toolchain fixture checks passed'
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
    if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
