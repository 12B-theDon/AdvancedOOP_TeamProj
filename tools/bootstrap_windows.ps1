#Requires -Version 5.1
[CmdletBinding()]
param([switch]$FunctionsOnly)

function Refresh-BootstrapPath {
    $env:Path = @($env:Path, [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User'),
        (Join-Path $env:ProgramFiles 'Git\cmd'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Git\cmd'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps')) -join ';'
}
function Ensure-WinGet {
    Refresh-BootstrapPath
    $command = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    Write-Host 'Preparing Windows Package Manager (current user)...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    Install-PackageProvider -Name NuGet -Scope CurrentUser -Force -MinimumVersion 2.8.5.201 | Out-Null
    Install-Module -Name Microsoft.WinGet.Client -Repository PSGallery -Scope CurrentUser -Force -AllowClobber
    Import-Module Microsoft.WinGet.Client -Force
    $repair = Get-Command Repair-WinGetPackageManager -ErrorAction Stop
    $arguments = @{}
    if ($repair.Parameters.ContainsKey('Force')) { $arguments.Force = $true }
    & $repair @arguments | Out-Host
    Refresh-BootstrapPath
    $command = Get-Command winget.exe -ErrorAction Stop
    return $command.Source
}

if (-not $FunctionsOnly) { Ensure-WinGet | Out-Host }
