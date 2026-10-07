param(
    [ValidateRange(0, 232)][int]$Domain = 0,
    [ValidateRange(1, 65535)][int]$Port = 8888
)

$ErrorActionPreference = 'Stop'
$toolsRoot = Split-Path -Parent $PSScriptRoot
$cache = [IO.Path]::GetFullPath((Join-Path $toolsRoot 'build/windows_agent'))
$binDir = Join-Path $cache 'install/bin'
$agentPath = Join-Path $binDir 'MicroXRCEAgent.exe'
if (-not (Test-Path -LiteralPath $agentPath)) { throw 'Build the Windows Agent first.' }
$buildDir = Join-Path $cache 'data_test'
$sourceDir = Join-Path $toolsRoot 'tests/windows_dds'
$prefix = @('fastrtps-2.14', 'fastcdr-2.2.0', 'foonathan_memory') |
    ForEach-Object { Join-Path $cache "build/temp_install/$_" }
& cmake -S $sourceDir -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$($prefix -join ';')"
if ($LASTEXITCODE -ne 0) { throw 'DDS test configure failed.' }
& cmake --build $buildDir --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'DDS test build failed.' }

$ownedAgent = $null
$previousPath = $env:PATH
try {
    $bound = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
    if ($bound.Count -gt 0) {
        foreach ($endpoint in $bound) {
            $owner = Get-CimInstance Win32_Process -Filter "ProcessId=$($endpoint.OwningProcess)"
            if (-not $owner.ExecutablePath -or $owner.ExecutablePath -ine $agentPath) {
                throw "UDP $Port is occupied by another program (PID $($endpoint.OwningProcess))."
            }
        }
        Write-Host 'Using the existing Windows Agent; it will remain running.'
    } else {
        $ownedAgent = Start-Process -FilePath $agentPath -ArgumentList @('udp4', '--port', "$Port", '-v', '6') `
            -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $cache 'data_test_agent_stdout.log') `
            -RedirectStandardError (Join-Path $cache 'data_test_agent_stderr.log')
        Start-Sleep -Milliseconds 700
        if ($ownedAgent.HasExited) { throw 'Test Agent failed to start; inspect its logs.' }
    }
    $env:PATH = "$binDir;$previousPath"
    Write-Host "Testing domain ${Domain}: heartbeat and command/echo (command value 57007)."
    & (Join-Path $buildDir 'Release/MicroRosDataTest.exe') $Domain
    $testResult = $LASTEXITCODE
} finally {
    $env:PATH = $previousPath
    if ($ownedAgent -and -not $ownedAgent.HasExited) {
        $ownedAgent | Stop-Process
        Write-Host 'Stopped our test Agent and released its port.'
    }
}
exit $testResult
