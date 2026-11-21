#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$DependenciesPath,
    [ValidateRange(1, 64)][int]$Jobs = 4,
    [ValidateRange(0, 2147483647)][int]$WaitForProcessId = 0,
    [switch]$LaunchGui
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$Command, [string[]]$ArgumentList)
    & $Command @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed (exit $LASTEXITCODE). Fix the error above and rerun setup_windows.ps1."
    }
}

function Update-ProcessPath {
    param([string[]]$Prepend = @())
    $values = @($Prepend) + @(
        $env:Path,
        [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User')
    )
    $seen = @{}
    $entries = foreach ($value in $values) {
        foreach ($entry in ($value -split [regex]::Escape([IO.Path]::PathSeparator))) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $expanded = [Environment]::ExpandEnvironmentVariables($entry.Trim())
                if (-not $seen.ContainsKey($expanded)) {
                    $seen[$expanded] = $true
                    $expanded
                }
            }
        }
    }
    $env:Path = $entries -join [IO.Path]::PathSeparator
}

function Install-Tool {
    param([string]$Id, [string]$Override)
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw 'WinGet is missing. Install Microsoft App Installer, then rerun this script: https://aka.ms/getwinget'
    }
    $installerArgs = @('install', '--exact', '--id', $Id, '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
    if ($Override) { $installerArgs += @('--override', $Override) }
    else { $installerArgs += '--silent' }
    Write-Host "Installing $Id..."
    Invoke-Checked $winget.Source $installerArgs
    Update-ProcessPath
}

function Find-Python311 {
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        try {
            $result = & $launcher.Source -3.11 -c 'import sys; print(sys.executable)' 2>$null
            if ($LASTEXITCODE -eq 0 -and $result) {
                $executable = (@($result)[-1]).Trim()
                if (Test-Path -LiteralPath $executable -PathType Leaf) { return $executable }
            }
        } catch { }
    }
    foreach ($folder in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python311'),
        (Join-Path $env:ProgramFiles 'Python311')
    )) {
        $executable = Join-Path $folder 'python.exe'
        if (Test-Path -LiteralPath $executable -PathType Leaf) { return $executable }
    }
    return $null
}

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        $versionLine = @(& $command.Source --version)[0]
        if ($LASTEXITCODE -eq 0 -and $versionLine -match '(\d+\.\d+\.\d+)') {
            if ([version]$Matches[1] -ge [version]'3.21.0') { return $command.Source }
        }
    }
    return $null
}

function Find-Vs2022 {
    param([switch]$AnyBuildTools)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $query = @('-latest', '-version', '[17.0,18.0)', '-property', 'installationPath')
        if ($AnyBuildTools) { $query += @('-products', 'Microsoft.VisualStudio.Product.BuildTools') }
        else { $query += @('-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64') }
        $result = & $vswhere @query
        if ($LASTEXITCODE -eq 0 -and $result) { return (@($result)[-1]).Trim() }
    }
    return $null
}

function Test-GitLfs {
    param([string]$Git)
    try {
        & $Git lfs version 1>$null 2>$null
        return ($LASTEXITCODE -eq 0)
    } catch { return $false }
}

function Test-QtLayout {
    param([string]$Root)
    foreach ($relative in @('bin\qmake.exe', 'bin\windeployqt.exe',
        'lib\cmake\Qt6Widgets\Qt6WidgetsConfig.cmake', 'plugins\imageformats\qtiff.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $relative) -PathType Leaf)) { return $false }
    }
    return $true
}

function Test-TorchLayout {
    param([string]$Root, [string]$Version)
    foreach ($relative in @('lib\torch.dll', 'lib\torch_cpu.dll', 'share\cmake\Torch\TorchConfig.cmake',
        'share\cmake\Torch\TorchConfigVersion.cmake')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $relative) -PathType Leaf)) { return $false }
    }
    $versionFile = Get-Content -LiteralPath (Join-Path $Root 'share\cmake\Torch\TorchConfigVersion.cmake') -Raw
    return ($versionFile -match ('set\s*\(\s*PACKAGE_VERSION\s+"?' + [regex]::Escape($Version) + '(?:\+cpu)?["\s)]'))
}

function Save-Download {
    param([string]$Uri, [string]$Destination)
    if (Test-Path -LiteralPath $Destination -PathType Leaf) { return }
    $partial = "$Destination.partial"
    Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $partial
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}

function Read-ProductionMetadata {
    param([string]$Path)
    $archive = $null
    $reader = $null
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -lt 1024) { return $null }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($Path)
        $entry = @($archive.Entries | Where-Object { $_.FullName -match '(^|/)extra/metadata.json$' })
        if ($entry.Count -ne 1) { return $null }
        $reader = [IO.StreamReader]::new($entry[0].Open())
        return ($reader.ReadToEnd() | ConvertFrom-Json)
    } catch { return $null }
    finally {
        if ($reader) { $reader.Dispose() }
        if ($archive) { $archive.Dispose() }
    }
}

function Test-ProductionModels {
    param([string]$Root)
    try {
        $labelsHash = $null
        foreach ($id in @('fvc2000_db3', 'fvc2002_db3', 'fvc2004_db1', 'combined')) {
            $metadata = Read-ProductionMetadata (Join-Path $Root "$id.bin")
            if (-not $metadata -or $metadata.format -ne 'deepprint-cosine-probabilities-v2' `
                -or $metadata.architecture -ne 'DeepPrint_TexMinu_512_Cosine30' `
                -or $metadata.model_id -ne $id -or $metadata.embedding_size -ne 512 `
                -or $metadata.output_dim -ne 30 -or $metadata.labels.Count -ne 30 `
                -or $metadata.preprocessing -ne 'gray01-fvc2004-center-square-aa299-gabor1.8-v2') { return $false }
            if (-not $labelsHash) { $labelsHash = $metadata.label_order_sha256 }
            if ($metadata.label_order_sha256 -ne $labelsHash) { return $false }
        }
        $manifestPath = Join-Path $Root 'weights.json'
        if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if ($manifest.format -ne 'fingerprint-production-weights-v1') { return $false }
            foreach ($id in @('fvc2000_db3', 'fvc2002_db3', 'fvc2004_db1', 'combined')) {
                $name = "$id.bin"
                $record = $manifest.files.PSObject.Properties[$name]
                $file = Join-Path $Root $name
                if (-not $record -or (Get-Item -LiteralPath $file).Length -ne $record.Value.bytes `
                    -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $record.Value.sha256) { return $false }
            }
        }
        return $true
    } catch { return $false }
}

function Write-WeightsManifest {
    param([string]$Root, [string]$TorchVersion)
    $files = [ordered]@{}
    foreach ($id in @('fvc2000_db3', 'fvc2002_db3', 'fvc2004_db1', 'combined')) {
        $name = "$id.bin"
        $file = Join-Path $Root $name
        $files[$name] = [ordered]@{
            bytes = (Get-Item -LiteralPath $file).Length
            sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $manifest = [ordered]@{ format = 'fingerprint-production-weights-v1'; torch_version = $TorchVersion; files = $files }
    [IO.File]::WriteAllText((Join-Path $Root 'weights.json'), ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}

if ($env:OS -ne 'Windows_NT' -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or [IntPtr]::Size -ne 8) {
    throw 'Run this script in 64-bit PowerShell on Windows x64.'
}
if (-not $DependenciesPath) { $DependenciesPath = Join-Path $env:LOCALAPPDATA 'FingerprintDeps' }
$DependenciesPath = [IO.Path]::GetFullPath($DependenciesPath)
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$modelsRoot = Join-Path $projectRoot 'models\trained_result'
$qtVersion = '6.8.3'
$qtRoot = Join-Path $DependenciesPath "Qt\$qtVersion\msvc2022_64"
$metadataPath = Join-Path $projectRoot 'models\pretrained\deepprint.json'
$torchVersion = (Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json).torch_version
if ($torchVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid torch_version in deepprint.json.' }
$torchDestination = Join-Path $DependenciesPath "libtorch-$torchVersion"
$torchRoot = Join-Path $torchDestination 'libtorch'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$hasher = [Security.Cryptography.SHA256]::Create()
try { $setupKey = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($projectRoot.ToLowerInvariant()))).Replace('-', '').Substring(0, 24) }
finally { $hasher.Dispose() }
$setupMutex = [Threading.Mutex]::new($false, "FingerprintSetup_$setupKey")
try { $ownsSetup = $setupMutex.WaitOne(0) }
catch [Threading.AbandonedMutexException] { $ownsSetup = $true }
if (-not $ownsSetup) {
    $setupMutex.Dispose()
    throw 'Setup for this checkout is already running. Wait until it finishes, then launch the GUI again.'
}
Push-Location $projectRoot
try {
    if ($WaitForProcessId -gt 0) {
        $previousGui = Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
        if ($previousGui -and $previousGui.ProcessName -eq 'FingerprintGUI' -and -not $previousGui.WaitForExit(60000)) {
            throw 'The previous GUI has not exited. Close it and rerun setup.'
        }
    }
    Write-Host '[1/9] Build tools'
    Update-ProcessPath -Prepend @((Join-Path $env:ProgramFiles 'CMake\bin'))
    $git = (Get-Command git.exe -ErrorAction Stop).Source
    if (-not (Test-GitLfs $git)) { Install-Tool 'GitHub.GitLFS' }
    if (-not (Test-GitLfs $git)) { throw 'Git LFS is not available after installation.' }
    $cmake = Find-CMake
    if (-not $cmake) {
        Install-Tool 'Kitware.CMake'
        Update-ProcessPath -Prepend @((Join-Path $env:ProgramFiles 'CMake\bin'))
        $cmake = Find-CMake
    }
    $python = Find-Python311
    if (-not $python) { Install-Tool 'Python.Python.3.11'; $python = Find-Python311 }
    $vsRoot = Find-Vs2022
    if (-not $vsRoot) {
        $existingBuildTools = Find-Vs2022 -AnyBuildTools
        if ($existingBuildTools) {
            Write-Host 'Adding the C++ workload to existing Visual Studio Build Tools...'
            $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            $modifyArgs = @('modify', '--installPath', ('"' + $existingBuildTools + '"'),
                '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended', '--passive', '--norestart')
            $installation = Start-Process -FilePath $installer -ArgumentList $modifyArgs -Verb RunAs -Wait -PassThru
            if ($installation.ExitCode -ne 0) {
                throw "MSVC setup returned $($installation.ExitCode). Complete any requested restart, then rerun this script."
            }
        } else {
            Install-Tool 'Microsoft.VisualStudio.2022.BuildTools' `
                '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
        }
        $vsRoot = Find-Vs2022
    }
    if (-not $cmake -or -not $python -or -not $vsRoot) {
        throw 'A required build tool is missing. Complete any requested Windows restart, then rerun this script.'
    }

    Write-Host '[2/9] Submodules and Git LFS weights (download before training)'
    Invoke-Checked $git @('submodule', 'update', '--init', '--recursive')
    Invoke-Checked $git @('lfs', 'install', '--local')
    try { Invoke-Checked $git @('lfs', 'pull') }
    catch { Write-Warning "Git LFS download failed. Available local models will be reused; missing models will require training. $($_.Exception.Message)" }
    $checkpoint = Join-Path $projectRoot 'models\pretrained\deepprint.bin'
    if ((-not (Test-Path -LiteralPath $checkpoint -PathType Leaf) -or (Get-Item -LiteralPath $checkpoint).Length -lt 1024) `
        -and -not (Test-ProductionModels $modelsRoot)) {
        throw 'No production weights are available and Git LFS did not provide the pretrained model needed for fallback training.'
    }

    Write-Host "[3/9] Qt $qtVersion (Widgets and TIFF)"
    if (-not (Test-QtLayout $qtRoot)) {
        Invoke-Checked $python @('-m', 'pip', 'install', 'aqtinstall==3.3.0')
        Invoke-Checked $python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $qtVersion,
            'win64_msvc2022_64', '-O', (Join-Path $DependenciesPath 'Qt'), '-m', 'qtimageformats')
        if (-not (Test-QtLayout $qtRoot)) { throw 'Qt installation is incomplete (Widgets, TIFF or windeployqt missing).' }
    }

    Write-Host "[4/9] LibTorch $torchVersion CPU Release"
    if (-not (Test-TorchLayout $torchRoot $torchVersion)) {
        $downloadRoot = Join-Path $DependenciesPath 'downloads'
        New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
        $archive = Join-Path $downloadRoot "libtorch-$torchVersion-cpu.zip"
        Save-Download "https://download.pytorch.org/libtorch/cpu/libtorch-win-shared-with-deps-$torchVersion%2Bcpu.zip" $archive
        try {
            Expand-Archive -LiteralPath $archive -DestinationPath $torchDestination -Force
        } catch {
            Remove-Item -LiteralPath $archive -Force
            throw "Could not extract LibTorch. The cached ZIP was removed; rerun this script. $($_.Exception.Message)"
        }
        if (-not (Test-TorchLayout $torchRoot $torchVersion)) { throw 'LibTorch installation is incomplete or has a different version.' }
    }

    Write-Host '[5/9] MSVC x64 and runtime environment'
    Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    Update-ProcessPath -Prepend @((Join-Path $qtRoot 'bin'), (Join-Path $torchRoot 'lib'),
        (Split-Path -Parent $cmake), (Split-Path -Parent $python))
    $env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
    $env:CMAKE_PREFIX_PATH = "$qtRoot;$torchRoot"

    Write-Host '[6/9] Configure and build Release'
    Invoke-Checked $cmake @('-S', $projectRoot, '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$qtRoot;$torchRoot", "-DPython3_EXECUTABLE=$python", "-DFINGERPRINT_PACKAGE_MODELS=$modelsRoot",
        '-DFINGERPRINT_BUILD_ALGORITHM=ON', '-DFINGERPRINT_BUILD_TRAINING=ON', '-DFINGERPRINT_BUILD_GUI=ON')
    Invoke-Checked $cmake @('--build', $buildRoot, '--config', 'Release', '--parallel', "$Jobs")

    Write-Host '[7/9] Production models'
    if (Test-ProductionModels $modelsRoot) {
        Write-Host 'All four production models are available; training skipped.'
    } else {
        if (Test-Path -LiteralPath $modelsRoot) {
            $backup = "${modelsRoot}_incomplete_$(Get-Date -Format yyyyMMdd_HHmmss)_$([guid]::NewGuid().ToString('N').Substring(0, 8))"
            Move-Item -LiteralPath $modelsRoot -Destination $backup
            Write-Host "Preserved incomplete models: $backup"
        }
        Write-Host 'No complete downloaded model set. Training all four models with the original settings...'
        Invoke-Checked (Join-Path $buildRoot 'Release\FingerprintTrain.exe') @(
            (Join-Path $projectRoot 'data\original'), $checkpoint, $modelsRoot)
        if (-not (Test-ProductionModels $modelsRoot)) { throw 'Training did not produce four compatible production models.' }
        Write-WeightsManifest $modelsRoot $torchVersion
    }

    Write-Host '[8/9] Package algorithm and evaluator'
    Invoke-Checked $cmake @('--build', $buildRoot, '--config', 'Release', '--target',
        'FingerprintAlgorithmPackage', 'FingerprintEvaluatorPackage')
    Write-Host '[9/9] Verify deployment manifests'
    foreach ($package in @('FingerprintAlgorithm', 'FingerprintEvaluator')) {
        Invoke-Checked $python @((Join-Path $projectRoot 'tools\verify_manifest.py'),
            (Join-Path $projectRoot "dist\$package"))
    }
    $gui = Join-Path $projectRoot 'dist\FingerprintEvaluator\FingerprintGUI.exe'
    if (-not (Test-Path -LiteralPath $gui -PathType Leaf)) { throw 'The packaged GUI is missing.' }
    Write-Host "Ready: $gui"
    if ($LaunchGui) { Start-Process -FilePath $gui -WorkingDirectory $projectRoot }
} finally {
    Pop-Location
    $setupMutex.ReleaseMutex()
    $setupMutex.Dispose()
}
