#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SourceHint,
    [string]$SourcePath,
    [string]$OriginExecutable,
    [string]$RunnerExecutable,
    [string]$LogPath,
    [switch]$FunctionsOnly
)

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
function Ensure-BootstrapGit {
    Refresh-BootstrapPath
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) {
        $winget = Ensure-WinGet
        & $winget install --exact --id Git.Git --source winget --accept-package-agreements --accept-source-agreements --disable-interactivity --silent | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Git installation failed (exit $LASTEXITCODE)." }
        Refresh-BootstrapPath
        $git = Get-Command git.exe -ErrorAction Stop
    }
    $lfs = $false
    try { & $git.Source lfs version 1>$null 2>$null; $lfs = ($LASTEXITCODE -eq 0) } catch { }
    if (-not $lfs) {
        $winget = Ensure-WinGet
        & $winget install --exact --id GitHub.GitLFS --source winget --accept-package-agreements --accept-source-agreements --disable-interactivity --silent | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Git LFS installation failed (exit $LASTEXITCODE)." }
        Refresh-BootstrapPath
        & $git.Source lfs version | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'Git LFS is still unavailable. Restart install.exe after completing dependency installation.' }
    }
    return $git.Source
}
function Test-FingerprintCheckout {
    param([string]$Path, [string]$Git)
    if (-not $Path -or -not (Test-Path -LiteralPath (Join-Path $Path 'tools\setup_windows.ps1') -PathType Leaf) `
        -or -not (Test-Path -LiteralPath (Join-Path $Path 'CMakeLists.txt') -PathType Leaf)) { return $false }
    $origin = @(& $Git -C $Path remote get-url origin 2>$null)
    if ($LASTEXITCODE -ne 0 -or $origin.Count -ne 1) { return $false }
    return ($origin[0].Trim() -match '^(https://github\.com/|git@github\.com:)12B-theDon/AdvancedOOP_TeamProj(?:\.git)?/?$')
}
function Resolve-BootstrapCheckout {
    param([string]$Hint, [string]$Requested, [string]$Git)
    if ($Requested) {
        $path = [IO.Path]::GetFullPath($Requested)
        if (-not (Test-FingerprintCheckout $path $Git)) { throw "Not a suitable FinalProject checkout: $path" }
        return $path
    }
    if ($Hint) {
        $path = [IO.Path]::GetFullPath($Hint)
        for ($i = 0; $i -lt 8 -and $path; $i++) {
            if (Test-FingerprintCheckout $path $Git) { return $path }
            $parent = Split-Path -Parent $path
            if ($parent -eq $path) { break }
            $path = $parent
        }
    }
    foreach ($candidate in @((Join-Path $env:USERPROFILE 'FinalProject'),
        (Join-Path $env:LOCALAPPDATA 'FingerprintInstaller\source\FinalProject'))) {
        if (Test-FingerprintCheckout $candidate $Git) { return $candidate }
    }
    $destination = Join-Path $env:LOCALAPPDATA 'FingerprintInstaller\source\FinalProject'
    if (Test-Path -LiteralPath $destination) {
        throw "The installation source folder already contains other files: $destination. Use install.exe --source with a suitable checkout. Existing files were preserved."
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    & $Git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git $destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Repository clone failed (exit $LASTEXITCODE). Rerun with --source after resolving the incomplete clone. No files were removed." }
    if (-not (Test-FingerprintCheckout $destination $Git)) { throw 'Cloned repository validation failed.' }
    return $destination
}
function Complete-InstallerLayout {
    param([string]$Checkout, [string]$Runner)
    # Also supports a previously published checkout whose package target predates install.exe.
    $package = Join-Path $Checkout 'dist\FingerprintEvaluator'
    $gui = Join-Path $package 'FingerprintGUI.exe'
    if (-not (Test-Path -LiteralPath $gui -PathType Leaf)) { throw 'The completed GUI package is missing.' }
    $installer = Join-Path $package 'install.exe'
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        Copy-Item -LiteralPath $Runner -Destination $installer -ErrorAction Stop
        $manifest = Join-Path $package 'SHA256SUMS.txt'
        $lines = @(Get-Content -LiteralPath $manifest | Where-Object { $_ -notmatch '  install\.exe$' })
        $lines += ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant() + '  install.exe')
        [IO.File]::WriteAllLines($manifest, ($lines | Sort-Object), [Text.UTF8Encoding]::new($false))
    }
    return $gui
}
function Invoke-FingerprintBootstrap {
    param([string]$SourceHint, [string]$SourcePath, [string]$OriginExecutable,
          [string]$RunnerExecutable, [string]$LogPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$step = 'Start installation'
$transcribing = $false
try {
    if ($env:OS -ne 'Windows_NT' -or [IntPtr]::Size -ne 8) { throw 'Windows x64 and 64-bit Windows PowerShell are required.' }
    if (-not $LogPath) { $LogPath = Join-Path $env:LOCALAPPDATA ('FingerprintInstaller\logs\install_' + (Get-Date -Format yyyyMMdd_HHmmss) + '.log') }
    New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null
    Start-Transcript -LiteralPath $LogPath -Append | Out-Null
    $transcribing = $true
    Write-Host "Installation log: $LogPath"
    $step = 'Prepare Git and Windows Package Manager when needed'
    Write-Host "[Bootstrap 1/3] $step"
    $git = Ensure-BootstrapGit
    $step = 'Find or clone the source checkout'
    Write-Host "[Bootstrap 2/3] $step"
    $checkout = Resolve-BootstrapCheckout $SourceHint $SourcePath $git
    Write-Host "Using source: $checkout (existing checkouts are reused without git pull)"
    $step = 'Install dependencies, build Release and package'
    Write-Host "[Bootstrap 3/3] $step"
    $env:FINGERPRINT_RUNNING_INSTALLER = $OriginExecutable
    $setup = Join-Path $checkout 'tools\setup_windows.ps1'
    # Let bootstrap finish the layout before launching the GUI. Script failures throw.
    & $setup
    $step = 'Complete the evaluator package and launch the GUI'
    $gui = Complete-InstallerLayout $checkout $RunnerExecutable
    Start-Process -FilePath $gui -WorkingDirectory $checkout
    Write-Host "Completed. GUI started: $gui"
    [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $LogPath) 'result.json'),
        (@{ success = $true; checkout = $checkout; gui = $gui; log = $LogPath } | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
} catch {
    Write-Host "Installation failed at: $step" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Log: $LogPath"
    if ($LogPath) {
        [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $LogPath) 'result.json'),
            (@{ success = $false; step = $step; error = $_.Exception.Message; log = $LogPath } | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    }
    exit 1
} finally {
    if ($transcribing) { Stop-Transcript | Out-Null }
}

}
if ($FunctionsOnly) { return }
Invoke-FingerprintBootstrap -SourceHint $SourceHint -SourcePath $SourcePath `
    -OriginExecutable $OriginExecutable -RunnerExecutable $RunnerExecutable -LogPath $LogPath
