param(
    [ValidateSet('udp')][string]$Transport = 'udp',
    [ValidateRange(1, 65535)][int]$Port = 8888
)

$ErrorActionPreference = 'Stop'
$agentPath = Join-Path $PSScriptRoot '../build/windows_agent/install/bin/MicroXRCEAgent.exe'
if (-not (Test-Path -LiteralPath $agentPath)) {
    throw 'Windows Agent is missing. Run Tools/scripts/build_agent_windows.ps1 first.'
}

& $agentPath udp4 --port $Port -v 6
exit $LASTEXITCODE
