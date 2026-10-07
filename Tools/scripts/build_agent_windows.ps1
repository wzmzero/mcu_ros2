param([ValidateRange(1, 32)][int]$Jobs = 4)

$ErrorActionPreference = 'Stop'
function Invoke-BuildCommand {
    param([string]$Program, [string[]]$CommandArgs)
    & $Program @CommandArgs
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Write-Utf8 {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

Get-Command cmake, git -ErrorAction Stop | Out-Null
$cache = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/windows_agent'))
$sourceDir = Join-Path $cache 'src/Micro-XRCE-DDS-Agent'
$buildDir = Join-Path $cache 'build'
$installDir = Join-Path $cache 'install'
New-Item -ItemType Directory -Path (Join-Path $cache 'src') -Force | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $sourceDir '.git'))) {
    Invoke-BuildCommand -Program git -CommandArgs @('clone', '--depth', '1', '--branch', 'v2.4.3',
        'https://github.com/eProsima/Micro-XRCE-DDS-Agent.git', $sourceDir)
}

# Download the pinned memory dependency before its configure-time probes run.
$superBuild = Join-Path $sourceDir 'cmake/SuperBuild.cmake'
$text = [IO.File]::ReadAllText($superBuild)
if (-not $text.Contains('STEP_TARGETS download')) {
    $text = $text.Replace('ExternalProject_Add(foonathan_memory',
        "ExternalProject_Add(foonathan_memory`n            STEP_TARGETS download")
    Write-Utf8 -Path $superBuild -Text $text
}
Invoke-BuildCommand -Program cmake -CommandArgs @('-S', $sourceDir, '-B', $buildDir,
    '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_INSTALL_PREFIX=$installDir",
    '-DUAGENT_SUPERBUILD=ON', '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_BUILD_TYPE=Release',
    '-DUAGENT_P2P_PROFILE=OFF', '-DUAGENT_DISCOVERY_PROFILE=ON',
    '-DUAGENT_SOCKETCAN_PROFILE=OFF', '-DUAGENT_CED_PROFILE=OFF')
Invoke-BuildCommand -Program cmake -CommandArgs @('--build', $buildDir, '--config', 'Release',
    '--target', 'foonathan_memory-download', '--parallel', "$Jobs")

# MSVC 19.44 omits template values in static_assert diagnostics. Incomplete
# types expose those same measured values; tolerate localized diagnostic text.
$probes = Join-Path $buildDir 'foonathan_memory/src/foonathan_memory/cmake'
$file = Join-Path $probes 'get_align_of.cpp'
$text = [IO.File]::ReadAllText($file)
$text = $text -replace '(?s)struct align_of\s*\{.*?\};', 'struct align_of;'
if (-not $text.Contains('struct align_of;')) { throw 'Unexpected alignment probe source.' }
Write-Utf8 -Path $file -Text $text
$file = Join-Path $probes 'get_node_size.cpp'
$text = [IO.File]::ReadAllText($file)
if (-not $text.Contains('struct node_size_of<type_align, node_size, true>;')) {
    $text = $text.Replace('struct empty_state {};',
        "template<size_t type_align, size_t node_size>`nstruct node_size_of<type_align, node_size, true>;`n`nstruct empty_state {};")
}
Write-Utf8 -Path $file -Text $text
$file = Join-Path $probes 'get_container_node_sizes.cmake'
$text = [IO.File]::ReadAllText($file)
$text = $text.Replace('"align_of<.*,[ ]*([0-9]+)[ul ]*>"',
    '"[a-z_]*lign_of<.*,[ ]*([0-9]+)[ul ]*>"')
$text = $text.Replace('"node_size_of<[ ]*([0-9]+)[ul ]*,[ ]*([0-9]+)[ul ]*,[ ]*true[ ]*>"',
    '"[a-z_]*ode_size_of<[ ]*([0-9]+)[ul ]*,[ ]*([0-9]+)[ul ]*,[ ]*true[ ]*>"')
Write-Utf8 -Path $file -Text $text

Invoke-BuildCommand -Program cmake -CommandArgs @('--build', $buildDir, '--config', 'Release',
    '--parallel', "$Jobs")
Invoke-BuildCommand -Program cmake -CommandArgs @('--install', $buildDir, '--config', 'Release')
$binDir = Join-Path $installDir 'bin'
Get-ChildItem -LiteralPath (Join-Path $buildDir 'temp_install') -Recurse -File -Filter '*.dll' |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $binDir -Force }
Write-Host "Windows Agent ready: $(Join-Path $binDir 'MicroXRCEAgent.exe')"
Write-Host 'Run from the workspace: .\Tools\scripts\agent.ps1 udp 8888'
