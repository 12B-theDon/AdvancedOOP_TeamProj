# 빌드·배포

## Windows 자동 설치·빌드·패키징

[저장소 복제](../readme.md#1-install)를 마친 Windows x64 PowerShell에서 실행합니다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1
```

[setup_windows.ps1](../tools/setup_windows.ps1)은 Git LFS·CMake·Python 3.11·MSVC 2022·Qt 6.8.3(Widgets·TIFF)·LibTorch CPU Release를 준비하고 submodule·Git LFS 모델 다운로드, MSVC x64 환경 설정, Release 빌드와 두 배포 폴더 패키징을 수행합니다. LibTorch 버전은 pretrained 모델의 `deepprint.json`에 맞춥니다. Qt 계정 로그인은 필요하지 않습니다.

기본 의존성 경로는 `%ProgramData%\FingerprintDeps`(일반적으로 `C:\ProgramData\FingerprintDeps`), 빌드 출력은 `build/Release`입니다. `-DependenciesPath`로 의존성 폴더, `-Jobs`로 병렬 빌드 수(기본 4)를 바꿀 수 있습니다. 의존성 경로는 영문·숫자 등 ASCII 문자만 사용하며 공백은 허용합니다. 프로젝트 폴더는 한글 경로를 사용할 수 있습니다. 재실행하면 준비된 도구·라이브러리를 재사용하며, 실패한 명령에서 중단합니다. 설치 프로그램이 재부팅을 요구하면 재부팅 후 같은 스크립트를 다시 실행합니다.

`models/trained_result`의 모델 4개는 Git LFS로 배포합니다. 정상 다운로드된 모델은 metadata·SHA-256 확인 후 재사용합니다. 파일이 없거나 호환되지 않으면 기존 폴더를 `trained_result_incomplete_*`로 보관하고, 원래 학습 설정으로 모델 4개를 학습한 뒤 `trained_result`에 저장합니다. 패키징한 두 폴더의 SHA-256 검사까지 통과해야 완료됩니다.

완료 후 [GUI 실행](../readme.md#2-gui)으로 진행합니다. `-LaunchGui`를 붙이면 준비 완료 후 GUI도 엽니다. 소스 저장소에서 GUI가 누락 파일을 발견하면 이 스크립트를 자동 실행하며, 실행 중인 EXE를 교체하지 않도록 이전 GUI가 종료된 뒤 준비하고 다시 엽니다. 소스가 없는 독립 배포본은 두 배포 폴더를 완전하게 함께 복사합니다.

참고: [WinGet](https://learn.microsoft.com/en-us/windows/package-manager/winget/install) · [MSVC](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio) · [aqtinstall](https://aqtinstall.readthedocs.io/en/stable/getting_started.html) · [LibTorch](https://download.pytorch.org/libtorch/cpu/)

### Qt 설치 오류 후 재실행

한글 Windows 사용자 경로에 Qt를 설치하면 `aqtinstall`이 `qmake` 출력을 UTF-8로 해석하는 단계에서 `UnicodeDecodeError`가 발생할 수 있습니다. 기본 의존성 경로를 사용자 폴더 밖의 ASCII 경로로 변경했습니다. [aqtinstall의 출력 해석 코드](https://github.com/miurahr/aqtinstall/blob/v3.3.0/aqt/updater.py)를 바탕으로 적용한 우회입니다.

프로젝트 폴더의 PowerShell에서 수정본을 받고 다시 실행합니다. 이미 받은 모델 4개는 재사용합니다.

```powershell
git pull --ff-only
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1
```

Qt는 설치 명령 성공 후 `qmake`의 설치 경로·버전, Widgets·TIFF 파일을 확인하고 완료 기록을 남깁니다. 완료 기록이 없는 설치는 `msvc2022_64_incomplete_*`로 보관한 뒤 재설치합니다. 이전 `%LOCALAPPDATA%\FingerprintDeps`는 그대로 남기며 새 기본 경로에서는 사용하지 않습니다. CMake의 이전 Qt·LibTorch 경로 cache도 새 설치 경로로 갱신합니다.

## 환경

Windows·MSVC 전용입니다. C++17, CMake 3.21+, Qt 5.15/6 Widgets·TIFF 플러그인이 필요하며 알고리즘·학습은 호환되는 LibTorch CPU를 사용합니다. MSVC와 동일한 architecture의 Qt·LibTorch를 준비하고, 빌드 디렉터리 실행 시 Qt `bin`·LibTorch `lib`를 PATH에 추가합니다. 자동 설치는 Windows x64를 대상으로 합니다. 외부 32-bit DLL은 32-bit Evaluator·Worker를 별도로 빌드해야 합니다.

알고리즘·학습을 준비할 때는 submodule과 LFS 파일을 가져옵니다. upstream 수정 적용은 [NOTICE](../third_party/deepprint.NOTICE.txt)를 따릅니다. Python 3.11+은 배포 파일의 SHA-256 검사에 사용됩니다.

```powershell
git submodule update --init --recursive
git lfs pull
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=<Qt>;<LibTorch>"
cmake --build build --config Release --parallel 4
```

## 실행

Release 빌드는 `build/Release/FingerprintGUI.exe`·`FingerprintTrain.exe`를 생성합니다. 학습 명령·옵션은 [Training](../src/Training/readme.md)에 정리했습니다.

GUI는 `data/original`을 작업 폴더 또는 실행파일의 상위 폴더에서 찾습니다. DB 버튼으로 경로를 지정하거나 외부 폴더를 선택할 수도 있습니다. [FVC 매핑](../src/Public/readme.md)에 맞는 원본 gray8을 사용합니다. GUI·Worker·알고리즘 DLL·학습·검증 도구는 모두 Windows에서 실행합니다.

## Evaluator 전용 빌드

외부 DLL만 평가할 때는 Qt만 준비하고 아래 옵션을 사용합니다.

```powershell
cmake -S . -B build-evaluator -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=<Qt>" -DFINGERPRINT_BUILD_ALGORITHM=OFF -DFINGERPRINT_BUILD_TRAINING=OFF
cmake --build build-evaluator --config Release --parallel 4
```

<a id="package"></a>

## 패키징

Windows에서 기존 [모델 4개](../models/readme.md)가 있는 폴더를 지정합니다. 기본값은 `models/trained_result`입니다.

```powershell
cmake -S . -B build "-DFINGERPRINT_PACKAGE_MODELS=<학습 출력 폴더>"
cmake --build build --config Release --target FingerprintAlgorithmPackage
cmake --build build --config Release --target FingerprintEvaluatorPackage
python tools/verify_manifest.py dist/FingerprintAlgorithm
```

- `dist/FingerprintAlgorithm`: 공개 DLL·Core·Worker·모델·runtime·라이선스·SHA-256 목록을 포함합니다.
- `dist/FingerprintEvaluator`: GUI·Worker와 windeployqt로 수집한 Qt·TIFF·runtime·라이선스·SHA-256 목록을 포함합니다.

DLL과 모델은 같은 배포 폴더에 둡니다. 필수 파일이 빠지면 기존 배포 폴더를 교체하지 않습니다. [검증 상태](verification.md) · [출처·라이선스](THIRD_PARTY.md)

`LibTorch license files missing`으로 중단된 경우 수정본을 `git pull --ff-only`로 받고 설치 스크립트를 다시 실행합니다. 기존 도구·라이브러리·정상 모델은 재사용합니다. 공식 Windows CPU LibTorch ZIP에 고지가 없으면 저장소에 보관한 동일 버전·build의 고지를 포함하므로 별도 다운로드는 필요하지 않습니다. 버전·build·SHA-256이 맞지 않으면 패키징을 중단하며 기존 배포 폴더를 보존합니다. CMake 4.3+에서는 [CMP0207](https://cmake.org/cmake/help/latest/policy/CMP0207.html)의 경로 정규화 동작을 명시해 반복 정책 경고를 방지합니다.
