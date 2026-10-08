[CmdletBinding()]
param(
    [string]$Distro = 'jazzy',
    [ValidateRange(0, 232)][int]$Domain = 0,
    [ValidateRange(1, 65535)][int]$Port = 8765,
    [string]$WslDistribution = '',
    [switch]$NetworkDds,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$scriptPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'web.sh')).Replace('\', '/')
if ($scriptPath -notmatch '^([A-Za-z]):/(.*)$') {
    throw 'Web 后端脚本必须位于 WSL /mnt/<盘符>/ 可访问的 Windows 本地磁盘。'
}
$linuxPath = '/mnt/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
$wslArgs = @()
if ($WslDistribution) { $wslArgs += @('--distribution', $WslDistribution) }
$wslArgs += @('--exec', 'bash', $linuxPath, '--distro', $Distro, '--domain', "$Domain", '--port', "$Port")
if ($NetworkDds) { $wslArgs += '--network-dds' }
Write-Host ("启动脚本：{0}" -f $PSCommandPath)
Write-Host ("ROS 2 {0}，Domain {1}，HTTP 端口 {2}" -f $Distro, $Domain, $Port)
Write-Host ("浏览器打开 http://localhost:{0}；按 Ctrl+C 停止服务。" -f $Port)
if ($Check) {
    Write-Host ('WSL 参数：' + ($wslArgs -join ' | '))
    return
}
& wsl.exe @wslArgs
exit $LASTEXITCODE
