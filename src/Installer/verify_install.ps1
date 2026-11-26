#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Installer, [Parameter(Mandatory)][string]$Checker,
      [Parameter(Mandatory)][string]$Results)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Installer = (Resolve-Path -LiteralPath $Installer).Path
$Uninstaller = (Resolve-Path -LiteralPath (Join-Path (Split-Path -Parent $Installer) 'Uninstall.exe')).Path
$Checker = (Resolve-Path -LiteralPath $Checker).Path
$Results = [IO.Path]::GetFullPath($Results)
New-Item -ItemType Directory -Path $Results -Force | Out-Null
$target = Join-Path $env:TEMP ('Fingerprint wizard 한글 space_' + [guid]::NewGuid().ToString('N'))
$oldPath = $env:Path
$oldPlugin = $env:QT_PLUGIN_PATH
$oldPlatform = $env:QT_QPA_PLATFORM
Start-Transcript -LiteralPath (Join-Path $Results 'verification.log') -Force | Out-Null
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
public class WizardWindow { public IntPtr Handle; public string Text; public string Class; }
public static class WizardUI {
    public delegate bool Callback(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback c, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, Callback c, IntPtr p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder b, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    public static WizardWindow[] Find(IntPtr parent) {
        List<WizardWindow> list = new List<WizardWindow>();
        Callback callback = delegate(IntPtr h, IntPtr unused) {
            if (!IsWindowVisible(h) || !IsWindowEnabled(h)) return true;
            StringBuilder text = new StringBuilder(1024), cls = new StringBuilder(128);
            GetWindowText(h,text,text.Capacity); GetClassName(h,cls,cls.Capacity);
            list.Add(new WizardWindow { Handle=h,Text=text.ToString(),Class=cls.ToString() }); return true;
        };
        if (parent==IntPtr.Zero) EnumWindows(callback,IntPtr.Zero); else EnumChildWindows(parent,callback,IntPtr.Zero);
        return list.ToArray();
    }
    public static void Click(IntPtr h) { if(!PostMessage(h,0x00F5,IntPtr.Zero,IntPtr.Zero)) throw new Exception("Button click failed"); }
    public static void Capture(IntPtr h,string path) {
        Rect r; if(!GetWindowRect(h,out r)) throw new Exception("Window rectangle missing");
        using(Bitmap bitmap=new Bitmap(r.R-r.L,r.B-r.T)) {
            using(Graphics graphics=Graphics.FromImage(bitmap)) {
                IntPtr dc=graphics.GetHdc(); bool ok=PrintWindow(h,dc,2); graphics.ReleaseHdc(dc);
                if(!ok) throw new Exception("Window capture failed");
            }
            bitmap.Save(path,ImageFormat.Png);
        }
    }
}
'@
function Check([bool]$Ok, [string]$Message) { if (-not $Ok) { throw $Message } }
function Wait-For([scriptblock]$Action, [int]$Seconds=60, [switch]$AllowDialogs) {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    do {
        $value=& $Action; if($value){return $value}
        $dialog=[WizardUI]::Find([IntPtr]::Zero) | Where-Object { $_.Class -eq '#32770' } | Select-Object -First 1
        if($dialog -and -not $AllowDialogs) {
            $details=([WizardUI]::Find($dialog.Handle) | ForEach-Object { $_.Text }) -join ' '
            throw "Unexpected installation dialog: $($dialog.Text) $details"
        }
        Start-Sleep -Milliseconds 200
    } while($timer.Elapsed.TotalSeconds -lt $Seconds)
    throw 'Timed out waiting for installer/application state'
}
function Wizard { return [WizardUI]::Find([IntPtr]::Zero) | Where-Object { $_.Class -eq 'TWizardForm' -and $_.Text -like '*Fingerprint Evaluator*' } | Select-Object -First 1 }
function Button([IntPtr]$Window,[string]$Pattern) {
    return [WizardUI]::Find($Window) | Where-Object { $_.Class -like '*Button*' -and $_.Text -match $Pattern } | Select-Object -First 1
}
function Verify-Files([string]$Folder) {
    $base=[IO.Path]::GetFullPath($Folder).TrimEnd('\')+'\'
    foreach($line in Get-Content -LiteralPath (Join-Path $Folder 'SHA256SUMS.txt')) {
        Check ($line -match '^([0-9a-f]{64})  (.+)$') 'Malformed package file list'
        $expected=$Matches[1]; $relative=$Matches[2]; $file=[IO.Path]::GetFullPath((Join-Path $Folder $relative))
        Check ($file.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)) 'Unsafe package file path'
        Check (Test-Path -LiteralPath $file -PathType Leaf) "Missing installed file: $relative"
        Check ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -eq $expected) "Installed file hash differs: $relative"
    }
}
function Complete-Wizard([string]$Prefix) {
    Write-Host "Opening wizard: $Prefix"
    $wizard=Wait-For {
        $language=[WizardUI]::Find([IntPtr]::Zero) | Where-Object { $_.Class -eq 'TSelectLanguageForm' } | Select-Object -First 1
        if($language) {
            [WizardUI]::Capture($language.Handle,(Join-Path $Results "$Prefix-language.png"))
            $ok=Button $language.Handle '^확인|^OK|^&OK'
            Check ($null -ne $ok) 'Language selection OK button missing'
            [WizardUI]::Click($ok.Handle)
            return $null
        }
        Wizard
    }
    [WizardUI]::Capture($wizard.Handle,(Join-Path $Results "$Prefix-welcome.png"))
    for($page=0;$page -lt 6;$page++) {
        $installButton=Button $wizard.Handle '^설치|^Install'
        if($installButton){break}
        $next=Wait-For { Button $wizard.Handle '^다음|^Next' }
        [WizardUI]::Click($next.Handle); Start-Sleep -Milliseconds 400
        [WizardUI]::Capture($wizard.Handle,(Join-Path $Results ("$Prefix-page${page}.png")))
    }
    Check ($null -ne $installButton) 'Install button was not reached'
    [WizardUI]::Capture($wizard.Handle,(Join-Path $Results "$Prefix-ready.png"))
    [WizardUI]::Click($installButton.Handle); Start-Sleep -Milliseconds 750
    [WizardUI]::Capture($wizard.Handle,(Join-Path $Results "$Prefix-progress.png"))
    $finish=Wait-For { Button $wizard.Handle '^마침|^Finish' } 300
    [WizardUI]::Capture($wizard.Handle,(Join-Path $Results "$Prefix-finished.png"))
    [WizardUI]::Click($finish.Handle)
}
function Start-UninstallConfirmation {
    $launcher=Start-Process -FilePath $Uninstaller -PassThru
    $confirmation=Wait-For {
        foreach($window in [WizardUI]::Find([IntPtr]::Zero)) {
            if($window.Text -like '*Fingerprint Evaluator*' -and
                (Button $window.Handle '예|Yes') -and (Button $window.Handle '아니요|No')) { return $window }
        }
    } -AllowDialogs
    Check ($launcher.WaitForExit(10000) -and $launcher.ExitCode -eq 0) 'Uninstall launcher failed'
    return $confirmation
}
try {
    $env:Path="$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
    $env:QT_PLUGIN_PATH=$null; $env:QT_QPA_PLATFORM=$null
    $absent=Start-Process -FilePath $Uninstaller -PassThru
    $notice=Wait-For { [WizardUI]::Find([IntPtr]::Zero) | Where-Object { $_.Text -eq 'Fingerprint Evaluator Uninstall' } | Select-Object -First 1 } -AllowDialogs
    [WizardUI]::Capture($notice.Handle,(Join-Path $Results '00-not-installed.png'))
    $ok=Button $notice.Handle '확인|OK'
    Check ($null -ne $ok) 'Not-installed message missing'
    [WizardUI]::Click($ok.Handle)
    Check ($absent.WaitForExit(10000) -and $absent.ExitCode -eq 1168) 'Not-installed launcher result differs'
    $installLog=Join-Path $Results 'install.log'
    $process=Start-Process -FilePath $Installer -ArgumentList @('/SP-','/LANG=korean',('/DIR="'+$target+'"'),('/LOG="'+$installLog+'"'),'/TASKS=desktopicon','/NORESTART') -PassThru
    Complete-Wizard '01-install'
    Check ($process.WaitForExit(60000) -and $process.ExitCode -eq 0) 'Interactive installer failed'
    $gui=Wait-For { Get-Process FingerprintGUI -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe') } | Select-Object -First 1 }
    $window=Wait-For { $gui.Refresh(); if($gui.MainWindowHandle -ne [IntPtr]::Zero){$gui.MainWindowHandle} }
    [WizardUI]::Capture($window,(Join-Path $Results '06-application.png'))
    $gui.CloseMainWindow() | Out-Null
    if(-not $gui.WaitForExit(10000)){$gui.Kill()}
    Verify-Files (Join-Path $target 'FingerprintAlgorithm')
    Verify-Files (Join-Path $target 'FingerprintEvaluator')
    Check (@(Get-ChildItem -LiteralPath (Join-Path $target 'data\original') -Filter '*.tif' -Recurse).Count -eq 240) 'Original examples missing'
    $key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{B21A42C6-0BCE-4C73-A734-1566AB3CD911}_is1'
    Check (Test-Path $key) 'Windows Apps uninstall registration missing'
    Check ((Get-ItemProperty $key).DisplayName -like 'Fingerprint Evaluator*') 'Wrong installed application entry'
    Check ((Get-FileHash -LiteralPath (Join-Path $target 'Uninstall.exe')).Hash -eq (Get-FileHash -LiteralPath $Uninstaller).Hash) 'Installed Uninstall.exe differs'
    $cancel=Start-UninstallConfirmation
    [WizardUI]::Capture($cancel.Handle,(Join-Path $Results '06-uninstall-cancel.png'))
    [WizardUI]::Click((Button $cancel.Handle '아니요|No').Handle)
    Wait-For { -not (Button $cancel.Handle '예|Yes') } -AllowDialogs | Out-Null
    Check ((Test-Path $key) -and (Test-Path -LiteralPath (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe'))) 'Cancel removed the installation'
    $env:Path=(Join-Path $target 'FingerprintEvaluator')+';'+$env:Path
    $env:QT_PLUGIN_PATH=Join-Path $target 'FingerprintEvaluator'
    & $Checker $target (Join-Path $Results 'public-kisa.json')
    Check ($LASTEXITCODE -eq 0) 'Installed public KISA API test failed'
    Write-Host 'Installed public KISA API checks passed'
    $probe=Get-Content -LiteralPath (Join-Path $Results 'public-kisa.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($module in $probe.loaded_modules) {
        if($module -match '\\(Qt6.*|torch.*|c10|libiomp5md)\.dll$') {
            Check ($module.StartsWith($target,[StringComparison]::OrdinalIgnoreCase)) "Runtime escaped installed package: $module"
        }
    }
    $report=Join-Path $target 'my-report.txt'; [IO.File]::WriteAllText($report,'preserve user report')
    $cached=Join-Path $target 'FingerprintEvaluator\install.exe'
    Check ((Get-FileHash -LiteralPath $cached).Hash -eq (Get-FileHash -LiteralPath $Installer).Hash) 'Repair installer differs'
    Write-Host 'Testing GUI-triggered repair'
    Remove-Item -LiteralPath (Join-Path $target 'FingerprintAlgorithm\combined.bin') -Force
    # Start the installed GUI with a missing model: it must open the graphical repair wizard.
    $recoveringGui=Start-Process -FilePath (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe') -PassThru
    Check ($recoveringGui.WaitForExit(60000) -and $recoveringGui.ExitCode -eq 0) 'GUI did not hand off to the repair wizard'
    Complete-Wizard '07-repair'
    Wait-For { -not (Wizard) } | Out-Null
    $repairedGui=Wait-For { Get-Process FingerprintGUI -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe') } | Select-Object -First 1 }
    $repairedWindow=Wait-For { $repairedGui.Refresh(); if($repairedGui.MainWindowHandle -ne [IntPtr]::Zero){$repairedGui.MainWindowHandle} }
    [WizardUI]::Capture($repairedWindow,(Join-Path $Results '08-repaired-application.png'))
    $repairedGui.CloseMainWindow() | Out-Null
    if(-not $repairedGui.WaitForExit(10000)){$repairedGui.Kill()}
    Verify-Files (Join-Path $target 'FingerprintAlgorithm')
    Verify-Files (Join-Path $target 'FingerprintEvaluator')
    Check ((Get-Content -LiteralPath $report -Raw) -eq 'preserve user report') 'Repair changed user data'
    Write-Host 'Repair passed; testing the downloaded Uninstall.exe and user file preservation'
    $changedModel=Join-Path $target 'FingerprintAlgorithm\fvc2000_db3.bin'
    [IO.File]::WriteAllText($changedModel,'controlled modified model')
    $changedHash=(Get-FileHash -LiteralPath $changedModel).Hash
    $confirmation=Start-UninstallConfirmation
    [WizardUI]::Capture($confirmation.Handle,(Join-Path $Results '09-uninstall-confirm.png'))
    [WizardUI]::Click((Button $confirmation.Handle '예|Yes').Handle)
    Wait-For {
        foreach($window in [WizardUI]::Find([IntPtr]::Zero)) {
            if($window.Text -like '*Fingerprint Evaluator*') {
                $done=Button $window.Handle '확인|OK'
                if($done) {
                    [WizardUI]::Capture($window.Handle,(Join-Path $Results '10-uninstall-complete.png'))
                    [WizardUI]::Click($done.Handle)
                    return $null
                }
            }
        }
        (-not (Test-Path $key)) -and (-not (Test-Path -LiteralPath (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe')))
    } 120 -AllowDialogs | Out-Null
    Check (-not (Test-Path $key)) 'Windows Apps entry survived uninstall'
    Check (-not (Test-Path (Join-Path $target 'FingerprintEvaluator\FingerprintGUI.exe'))) 'GUI survived uninstall'
    Check ((Get-Content -LiteralPath $report -Raw) -eq 'preserve user report') 'Uninstall removed user report'
    $saved=@(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'FingerprintEvaluator\backups') -Filter 'fvc2000_db3.bin' -Recurse | Where-Object { (Get-FileHash -LiteralPath $_.FullName).Hash -eq $changedHash })
    Check ($saved.Count -gt 0) 'Uninstall did not preserve the modified model'
    @{ wizard_pages=$true; korean_space_path=$true; original_examples=240; public_api_databases=3;
       installed_runtime_only=$true; finish_launch=$true; cached_repair=$true; gui_triggered_repair=$true; uninstall_registration=$true;
       uninstall_launcher=$true; uninstall_not_installed=$true; uninstall_cancel=$true;
       user_report_preserved=$true; modified_model_preserved=$true; installer_sha256=(Get-FileHash -LiteralPath $Installer).Hash } |
       ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Results 'result.json') -Encoding UTF8
} catch {
    Write-Host $_.ScriptStackTrace
    try {
        $windows=@([WizardUI]::Find([IntPtr]::Zero))
        $windows | Select-Object Text,Class | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Results 'failure-windows.json') -Encoding UTF8
        for($i=0;$i -lt $windows.Count;$i++) {
            try { [WizardUI]::Capture($windows[$i].Handle,(Join-Path $Results "failure-window-$i.png")) } catch { Write-Host "Skipped an empty diagnostic window" }
        }
        $logs=Join-Path $env:LOCALAPPDATA 'FingerprintEvaluator\logs'
        if(Test-Path -LiteralPath $logs) { Copy-Item -LiteralPath $logs -Destination (Join-Path $Results 'saved-setup-logs') -Recurse -Force }
        Get-ChildItem -LiteralPath $env:TEMP -Filter 'Setup Log *.txt' | Copy-Item -Destination $Results -Force
    } catch { Write-Host "Additional failure diagnostics: $($_.Exception.Message)" }
    throw
} finally {
    $env:Path=$oldPath; $env:QT_PLUGIN_PATH=$oldPlugin; $env:QT_QPA_PLATFORM=$oldPlatform
    Stop-Transcript | Out-Null
}
