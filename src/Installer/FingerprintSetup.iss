#ifndef Payload
  #error Payload must point to the clean, packaged Windows runtime files.
#endif
#ifndef SetupOutput
  #define SetupOutput "."
#endif

[Setup]
AppId={{B21A42C6-0BCE-4C73-A734-1566AB3CD911}
AppName=Fingerprint Evaluator
AppVersion=1.0.0
AppPublisher=AdvancedOOP Team
DefaultDirName={localappdata}\Programs\FingerprintEvaluator
DefaultGroupName=Fingerprint Evaluator
DisableDirPage=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
OutputDir={#SetupOutput}
OutputBaseFilename=install
Compression=lzma2/fast
SolidCompression=no
DiskSpanning=no
SetupLogging=yes
UninstallLogging=yes
UninstallDisplayIcon={app}\FingerprintEvaluator\FingerprintGUI.exe
CloseApplications=yes
RestartApplications=no
SetupMutex=FingerprintEvaluatorSetupWizard
ExtraDiskSpaceRequired=1600000000

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
korean.DesktopShortcut=바탕 화면에 바로가기 만들기
korean.RunApplication=Fingerprint Evaluator 실행
korean.RepairShortcut=Fingerprint Evaluator 복구 설치
korean.Welcome=Fingerprint Evaluator를 설치합니다.%n%n프로그램, 실행에 필요한 파일, 학습된 모델과 예제 지문 이미지를 함께 설치합니다.%nGit 설치나 직접 빌드할 필요가 없습니다.
korean.Verify=설치한 파일을 확인하는 중입니다...
korean.BackupError=기존 모델을 보관하지 못했습니다. 사용 중인 프로그램을 닫고 다시 시도하세요.
korean.CacheError=복구용 설치 파일을 저장하지 못했습니다. 다른 설치 창을 닫고 다시 시도하세요.
korean.InvalidFile=설치 파일을 확인하지 못했습니다: %1
english.DesktopShortcut=Create a desktop shortcut
english.RunApplication=Launch Fingerprint Evaluator
english.RepairShortcut=Repair Fingerprint Evaluator
english.Welcome=Setup will install Fingerprint Evaluator.%n%nThe application, required runtime files, trained models and example fingerprint images are included.%nNo Git installation or manual build is required.
english.Verify=Verifying installed files...
english.BackupError=Could not preserve an existing model. Close applications using it and try again.
english.CacheError=Could not save the repair installer. Close other setup windows and try again.
english.InvalidFile=Installed file verification failed: %1

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
Source: "{#Payload}\FingerprintEvaluator\*"; DestDir: "{app}\FingerprintEvaluator"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\FingerprintAlgorithm\*"; DestDir: "{app}\FingerprintAlgorithm"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Payload}\data\original\*"; DestDir: "{app}\data\original"; Flags: onlyifdoesntexist recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Fingerprint Evaluator"; Filename: "{app}\FingerprintEvaluator\FingerprintGUI.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:RepairShortcut}"; Filename: "{app}\FingerprintEvaluator\install.exe"; Parameters: "/DIR=""{app}"""; WorkingDir: "{app}"
Name: "{autodesktop}\Fingerprint Evaluator"; Filename: "{app}\FingerprintEvaluator\FingerprintGUI.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\FingerprintEvaluator\FingerprintGUI.exe"; WorkingDir: "{app}"; Description: "{cm:RunApplication}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\FingerprintEvaluator\install.exe"

[Code]
var
  ExpectedFiles, ExpectedHashes: array of String;
  BackupDone: Boolean;

function CopyWindowsFile(Source, Destination: String; FailIfExists: Integer): Integer;
  external 'CopyFileW@kernel32.dll stdcall';

#include Payload + "\expected_files.iss"

function IsModel(const Name: String): Boolean;
begin
  Result := (Pos('FingerprintAlgorithm\', Name) = 1) and (CompareText(ExtractFileExt(Name), '.bin') = 0);
end;

procedure PreserveChangedModels;
var
  I, Attempt: Integer;
  Current, BackupRoot, BackupFile: String;
begin
  if BackupDone then Exit;
  BackupRoot := ExpandConstant('{localappdata}\FingerprintEvaluator\backups\') + GetDateTimeString('yyyymmdd-hhnnss', '-', '-');
  Attempt := 0;
  while DirExists(BackupRoot + '-' + IntToStr(Attempt)) do Attempt := Attempt + 1;
  BackupRoot := BackupRoot + '-' + IntToStr(Attempt);
  for I := 0 to GetArrayLength(ExpectedFiles) - 1 do begin
    if IsModel(ExpectedFiles[I]) then begin
      Current := ExpandConstant('{app}\') + ExpectedFiles[I];
      if FileExists(Current) then begin
        if CompareText(GetSHA256OfFile(Current), ExpectedHashes[I]) <> 0 then begin
          BackupFile := BackupRoot + '\' + ExtractFileName(Current);
          if not ForceDirectories(BackupRoot) then RaiseException(CustomMessage('BackupError'));
          if CopyWindowsFile(Current, BackupFile, 1) = 0 then RaiseException(CustomMessage('BackupError'));
          Log('Preserved changed model: ' + BackupFile);
        end;
      end;
    end;
  end;
  BackupDone := True;
end;

procedure InitializeWizard;
begin
  PopulateFiles;
  WizardForm.WelcomeLabel2.Caption := CustomMessage('Welcome');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  try
    PreserveChangedModels;
  except
    Result := GetExceptionMessage;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  I: Integer;
  Current, Source, Target, Manifest, Line: String;
  SameInstaller: Boolean;
begin
  if CurStep <> ssPostInstall then Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('Verify');
  for I := 0 to GetArrayLength(ExpectedFiles) - 1 do begin
    { Existing example images belong to the user and are never overwritten. }
    if Pos('data\original\', ExpectedFiles[I]) <> 1 then begin
      Current := ExpandConstant('{app}\') + ExpectedFiles[I];
      if not FileExists(Current) then RaiseException(FmtMessage(CustomMessage('InvalidFile'), [ExpectedFiles[I]]));
      if CompareText(GetSHA256OfFile(Current), ExpectedHashes[I]) <> 0 then
        RaiseException(FmtMessage(CustomMessage('InvalidFile'), [ExpectedFiles[I]]));
    end;
  end;
  Source := ExpandConstant('{srcexe}');
  Target := ExpandConstant('{app}\FingerprintEvaluator\install.exe');
  { Windows can name the same running EXE with a short or long profile path. }
  SameInstaller := CompareText(Source, Target) = 0;
  if (not SameInstaller) and FileExists(Target) then
    SameInstaller := CompareText(GetSHA256OfFile(Source), GetSHA256OfFile(Target)) = 0;
  Log('Setup source: ' + Source + '; repair target: ' + Target);
  if not SameInstaller then begin
    if CopyWindowsFile(Source, Target, 0) = 0 then RaiseException(CustomMessage('CacheError'));
  end;
  Manifest := ExpandConstant('{app}\FingerprintEvaluator\SHA256SUMS.txt');
  Line := Lowercase(GetSHA256OfFile(Target)) + '  install.exe' + #10;
  if not SaveStringToFile(Manifest, Line, True) then RaiseException(CustomMessage('CacheError'));
  Log('Repair installer: ' + Target);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    PopulateFiles;
    PreserveChangedModels;
  end;
end;

procedure PreserveLog;
var
  Folder, Name, Current: String;
begin
  try
    Current := ExpandConstant('{log}');
    if (Current <> '') and FileExists(Current) then begin
      Folder := ExpandConstant('{localappdata}\FingerprintEvaluator\logs');
      if ForceDirectories(Folder) then begin
        Name := Folder + '\setup-' + GetDateTimeString('yyyymmdd-hhnnss', '-', '-') + '.log';
        CopyWindowsFile(Current, Name, 0);
      end;
    end;
  except
    Log('Could not preserve an additional log copy.');
  end;
end;

procedure DeinitializeSetup;
begin
  PreserveLog;
end;

procedure DeinitializeUninstall;
begin
  PreserveLog;
end;
