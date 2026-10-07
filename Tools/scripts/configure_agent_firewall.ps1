param(
    [System.Net.IPAddress]$McuAddress = '192.168.7.1',
    [ValidateRange(1, 65535)][int]$Port = 8888,
    [ValidateSet('windows', 'wsl')][string]$Target = 'windows'
)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script in an administrator PowerShell window.'
}
if ($McuAddress.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or
    $McuAddress.Equals([Net.IPAddress]::Any) -or $McuAddress.Equals([Net.IPAddress]::Broadcast)) {
    throw 'McuAddress must be an individual IPv4 address.'
}

if ($Target -eq 'wsl') {
    # Mirrored WSL traffic is also filtered by the Hyper-V firewall. Native
    # Windows Agent application rules do not authorize the Linux executable.
    foreach ($command in 'Get-NetFirewallHyperVRule', 'New-NetFirewallHyperVRule', 'Set-NetFirewallHyperVRule') {
        if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
            throw "Missing $command. This option requires WSL mirrored networking and Hyper-V firewall support."
        }
    }
    $ruleName = "MicroRosWslUdp$Port"
    $hostOptions = @{
        Name = $ruleName
        Enabled = 'True'
        Direction = 'Inbound'
        Action = 'Allow'
        Protocol = 'UDP'
        LocalPort = $Port
        RemoteAddress = $McuAddress.ToString()
        Profile = 'Any'
    }
    if (Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue) {
        Set-NetFirewallRule @hostOptions | Out-Null
    } else {
        New-NetFirewallRule @hostOptions -DisplayName "micro-ROS WSL UDP $Port" | Out-Null
    }
    $vmOptions = @{
        Name = $ruleName
        Enabled = 'True'
        Direction = 'Inbound'
        Action = 'Allow'
        VMCreatorId = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'
        Protocol = 'UDP'
        LocalPorts = $Port
        RemoteAddresses = $McuAddress.ToString()
    }
    if (Get-NetFirewallHyperVRule -Name $ruleName -ErrorAction SilentlyContinue) {
        Set-NetFirewallHyperVRule @vmOptions | Out-Null
    } else {
        New-NetFirewallHyperVRule @vmOptions -DisplayName "micro-ROS WSL UDP $Port" | Out-Null
    }
    Write-Host "Allowed MCU $McuAddress -> WSL Agent UDP $Port in Windows and Hyper-V firewalls."
    return
}

$agentPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../build/windows_agent/install/bin/MicroXRCEAgent.exe')).Path

# An explicit application block takes priority over a port allow rule. Only
# disable inbound UDP blocks associated with this exact executable.
$applicationFilters = @(Get-NetFirewallApplicationFilter -Program $agentPath -ErrorAction SilentlyContinue)
foreach ($applicationFilter in $applicationFilters) {
    $appRules = Get-NetFirewallRule -AssociatedNetFirewallApplicationFilter $applicationFilter
    foreach ($rule in $appRules) {
        if ($rule.Direction -eq 'Inbound' -and $rule.Action -eq 'Block' -and $rule.Enabled -eq 'True') {
            $filter = $rule | Get-NetFirewallPortFilter
            if ($filter.Protocol -eq 'UDP' -or $filter.Protocol -eq '17') {
                $rule | Disable-NetFirewallRule | Out-Null
                Write-Host "Disabled the Agent's UDP block: $($rule.Name)"
            }
        }
    }
}

$ruleName = "MicroRosWindowsUdp$Port"
$existing = Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue
$ruleOptions = @{
    Name = $ruleName
    Enabled = 'True'
    Direction = 'Inbound'
    Action = 'Allow'
    Program = $agentPath
    Protocol = 'UDP'
    LocalPort = $Port
    RemoteAddress = $McuAddress.ToString()
    Profile = 'Any'
}
if ($existing) {
    Set-NetFirewallRule @ruleOptions | Out-Null
} else {
    New-NetFirewallRule @ruleOptions -DisplayName "micro-ROS Windows UDP $Port" | Out-Null
}
Write-Host "Allowed MCU $McuAddress -> Windows Agent UDP $Port."
