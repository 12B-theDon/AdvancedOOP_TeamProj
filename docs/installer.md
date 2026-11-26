# Windows 설치기

`install.exe` 하나로 Windows x64에서 설치를 시작합니다. Qt·LibTorch·Git·CMake·MSVC를 미리 설치할 필요가 없습니다. Windows 기본 64-bit PowerShell 5.1과 초기 다운로드를 위한 인터넷을 사용합니다.

실행하면 Git·Git LFS를 확인하고, 적절한 기존 checkout을 찾아 재사용하거나 저장소를 복제합니다. 기존 checkout에서 `git pull`·reset·파일 삭제를 자동 실행하지 않습니다. 이어 `tools/setup_windows.ps1`을 실행해 기존 도구·모델을 확인하고 부족한 항목만 준비한 뒤 Release 빌드·패키징과 GUI 실행을 마칩니다. 정상 모델 4개는 재학습하지 않습니다.

Git·Git LFS·CMake·Python 3.11·MSVC 2022는 기존 WinGet package ID를 사용합니다. Qt 6.8.3·aqtinstall 3.3.0과 pretrained metadata의 LibTorch CPU 버전도 기존 설치 스크립트를 따릅니다. WinGet이 없으면 Microsoft의 `Microsoft.WinGet.Client`를 현재 사용자 범위에 준비합니다. 설치기 자체는 `asInvoker`로 실행하고, 필요한 의존성 설치 프로그램만 Windows 권한 승인을 요청합니다. [Microsoft WinGet 안내](https://learn.microsoft.com/en-us/windows/package-manager/winget/)

기존 소스 위치를 직접 지정할 수도 있습니다.

```powershell
.\install.exe --source "C:\Users\사용자\FinalProject"
```

소스를 찾지 못하면 `%LOCALAPPDATA%\FingerprintInstaller\source\FinalProject`에 복제합니다. 그 위치에 다른 파일이나 불완전한 checkout이 있으면 이를 보존하고 경로 지정 방법을 표시합니다. 프로젝트 경로의 공백·한글을 허용하며, Qt·LibTorch 설치 경로는 기존처럼 ASCII 경로를 사용합니다.

## 로그와 재실행

실행별 기록은 `%LOCALAPPDATA%\FingerprintInstaller\runs\<날짜_시각_PID>\`에 남습니다.

- `installation.log`: bootstrap·기존 setup의 진행과 오류.
- `result.json`: 완료 여부, checkout·GUI 경로 또는 실패 단계.
- `bootstrap_windows.ps1`: 해당 EXE에 포함된 초기 설치 논리.

PowerShell을 시작하기 전 native 단계에서 실패한 경우에도 가능한 경우 이 로그에 실패 단계를 기록합니다. 실행별 폴더를 만들기 전 오류는 `%LOCALAPPDATA%\FingerprintInstaller\logs\native_installation.log`에 남습니다.

설치기는 자신을 이 작업 폴더로 복사한 뒤 원래 EXE를 종료하므로, 빌드·패키징 때 실행 파일을 덮어쓰지 않습니다. 원래 명령의 종료를 설치 완료로 해석하지 말고, 자동화에서는 `result.json`을 확인합니다. 오류가 나면 실패 단계와 로그 위치를 표시하고 Enter를 누를 때까지 창을 유지합니다. 자동 검사는 `--no-pause`를 사용할 수 있습니다.

재실행 시 기존 의존성·정상 모델을 재사용합니다. 배포 갱신은 사용자 데이터·보고서를 지우지 않으며, 실패한 교체는 이전 파일로 복원합니다. 실행 중인 원래 `install.exe`가 배포 대상에 있으면 그 파일도 보존합니다. GUI·Worker 등이 사용 중인 파일을 교체할 수 없으면 프로그램을 닫은 뒤 다시 실행합니다.

## Qt·LibTorch 없는 독립 빌드

MSVC x64와 CMake가 준비된 개발용 PowerShell에서 실행합니다. 이 빌드는 애플리케이션의 Qt·LibTorch를 검색하지 않습니다.

```powershell
cmake -S src\Installer -B build-installer -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "설치기 구성 실패" }
cmake --build build-installer --config Release --target FingerprintInstaller
if ($LASTEXITCODE -ne 0) { throw "설치기 빌드 실패" }
```

결과는 `build-installer\Release\install.exe`입니다. root 구성의 `-DFINGERPRINT_INSTALLER_ONLY=ON`도 같은 독립 target을 제공합니다. `FingerprintInstaller`는 `OUTPUT_NAME install`과 static C++ runtime을 사용하고, 초기 PowerShell script와 실행 권한 manifest를 Windows resource에 포함합니다.

전체 프로그램의 `FingerprintEvaluatorPackage` target은 아래 배치를 생성합니다. 알고리즘 DLL·모델의 기존 별도 배포 위치는 유지합니다.

```text
 dist/
 ├─ FingerprintEvaluator/
 │  ├─ install.exe
 │  ├─ FingerprintGUI.exe
 │  ├─ FingerprintWorker.exe
 │  └─ Qt runtime·plugins·licenses·SHA256SUMS.txt
 └─ FingerprintAlgorithm/
    └─ 기존 DLL·모델·runtime 구성
```

## 현재 검증 범위

2026-11-26 macOS 환경에서 LLVM-MinGW로 Windows x64 Release PE를 cross-build했고, 초기 script·`asInvoker` resource와 system DLL import를 확인했습니다. PowerShell 제어 검사에서는 checkout 재사용·복제 분기, 공백·한글 경로, 기존 파일 보존, 실패 단계·로그, 성공 후 GUI 실행 분기를 확인했습니다. 외부 의존성 설치와 GUI 실행은 이 검사에서 대체했습니다.

실제 Windows에서 MSVC로 만든 설치기의 실행, 새 PC의 WinGet 복구·UAC·재부팅·전체 다운로드·GUI 기동, Windows 파일 잠금 조건은 아직 실행 검증하지 못했습니다. 현재 제공된 standalone EXE는 unsigned cross-build이며 포함된 고지는 `install.exe --licenses`로 확인합니다. 애플리케이션의 Windows GUI 배포본은 Windows에서 전체 패키징 target으로 생성해야 합니다.
