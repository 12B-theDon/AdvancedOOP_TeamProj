# Windows 설치 마법사

[install.exe 다운로드](https://github.com/12B-theDon/AdvancedOOP_TeamProj/releases/download/windows-ui-installer-20261127-preview/install.exe) · [Uninstall.exe 다운로드](https://github.com/12B-theDon/AdvancedOOP_TeamProj/releases/download/windows-ui-installer-20261127-preview/Uninstall.exe) — Windows x64 Preview

다운로드한 파일을 더블클릭하고 **설치 위치 → 바로가기 → 설치 → 완료**를 진행합니다. 마지막 화면의 실행 항목으로 GUI를 엽니다. 설치 과정에 Terminal·PowerShell 창을 띄우지 않으며, Git·Python·CMake·MSVC·Qt SDK·LibTorch SDK를 사용자 PC에 설치하지 않습니다. 프로그램과 필요한 runtime DLL, 기존 학습 모델 4개, 원본 예제 240장을 설치 파일에 포함합니다. 다운로드 이후 설치 중 별도 인터넷 연결은 사용하지 않습니다.

기본 위치는 `%LOCALAPPDATA%\Programs\FingerprintEvaluator`입니다. 사용자별 설치이며 기본 경로에서는 관리자 권한을 요구하지 않습니다. Windows x64·Windows 10 이상을 대상으로 합니다. 파일은 코드 서명되지 않은 Preview이므로 검증 범위는 Release의 기록을 참고합니다.

```text
FingerprintEvaluator/
├─ FingerprintEvaluator/ # GUI·Worker·Qt runtime·복구용 install.exe
├─ FingerprintAlgorithm/ # 공개 DLL·Core·모델 4개·필수 runtime
├─ data/original/        # 원본 예제 이미지
├─ Uninstall.exe         # 제거 확인 창 실행
├─ unins000.exe          # Windows에 등록된 실제 제거 프로그램
└─ unins000.dat          # 설치 파일·제거 정보
```

## 복구·업데이트·제거

Start 메뉴의 **Fingerprint Evaluator 복구 설치** 또는 설치 폴더의 `FingerprintEvaluator/install.exe`를 실행하면 같은 마법사에서 다시 설치합니다. 기존 사용자 보고서·추가 파일은 삭제하지 않습니다. 교체하거나 제거하는 모델이 배포 모델과 다르면 `%LOCALAPPDATA%\FingerprintEvaluator\backups`에 먼저 보관합니다. 원본 예제 경로에 이미 있는 이미지는 덮어쓰지 않습니다.

GUI가 설치 파일·모델 누락을 발견하면 복구 마법사를 열고 종료합니다. 소스 저장소에서 실행한 개발용 GUI의 경우에는 기존 개발용 준비 스크립트를 사용합니다. 복구용 EXE 자체는 현재 실행 중인 파일과 같으면 덮어쓰지 않습니다.

다운로드한 `Uninstall.exe`, 설치 폴더의 `Uninstall.exe` 또는 Start 메뉴의 **Fingerprint Evaluator 제거**를 실행하고 제거 확인 창에서 승인합니다. Windows **설정 → 앱 → 설치된 앱 → Fingerprint Evaluator → 제거**에서도 동일한 제거 프로그램을 실행합니다. 취소하면 설치 상태를 유지하며, 설치되어 있지 않으면 안내만 표시합니다.

`Uninstall.exe`는 Windows에 등록된 이 프로그램의 설치 위치를 찾으므로 다른 폴더에서 실행해도 됩니다. 기존 설치 마법사로 설치한 버전에도 사용할 수 있습니다. 직접 작성한 보고서와 보관한 모델은 유지합니다. 소스 개발용으로 별도 설치한 Git·Python·CMake·MSVC·Qt·LibTorch는 이 제거 대상에 포함하지 않습니다. 설치·제거 로그는 `%LOCALAPPDATA%\FingerprintEvaluator\logs`에 보관합니다. Windows Temp에도 `Setup Log ...txt`·`Uninstall Log ...txt`로 남습니다.

## 개발자 빌드

Windows에서 애플리케이션을 Release로 빌드하고 별도 payload를 만든 뒤 Inno Setup으로 하나의 EXE를 만듭니다. 사용자 데이터가 섞일 수 있는 기존 `dist` 폴더 대신 새 패키징 stage에서 만든 파일만 포함합니다.

Inno Setup 6.3+가 있는 개발용 PowerShell에서 프로젝트 루트 기준으로 실행합니다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1 -DistributedModelsOnly -BuildInstaller
```

`-DistributedModelsOnly`는 기존 배포 가중치만 사용하고 Training 프로그램·pretrained 모델 다운로드·새 학습을 생략합니다. 결과는 `dist/FingerprintSetup/`의 `install.exe`·`Uninstall.exe`·`SHA256SUMS.txt`입니다. portable Evaluator 폴더에도 복구용 설치기를 복사합니다. Inno Setup은 패키지를 만드는 개발자에게만 필요합니다.

`Uninstall.exe`가 포함된 `build/installer-payload`가 이미 있다면 Qt·LibTorch·C++ compiler 없이 별도로 패키징할 수도 있습니다.

```powershell
cmake -S src/Installer -B build-installer "-DFINGERPRINT_INSTALLER_PAYLOAD=$((Resolve-Path build/installer-payload).Path)"
cmake --build build-installer --target FingerprintInstaller
```

`.github/workflows/windows-installer.yml`은 Windows에서 설치·제거 EXE를 빌드하고 설치 마법사·한글/공백 경로·공개 KISA API·TIFF plugin·GUI 실행·복구·제거를 검사합니다. `Uninstall.exe`의 미설치 안내·제거 취소·확인 후 제거와 사용자 파일 보존도 검사합니다. 성공한 파일만 Preview Release에 공개합니다. 실제 개인 PC의 모든 정책·보안 제품·사용자 상태를 대신하는 검사는 아닙니다.

[Inno Setup 문서](https://jrsoftware.org/ishelp/) · [소스 빌드 안내](build.md)
