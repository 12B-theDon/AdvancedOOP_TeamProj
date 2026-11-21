# Build / Deploy

## Windows 자동 설치·빌드·패키징

[저장소 복제](../readme.md#1-install)를 마친 Windows x64 PowerShell에서 실행합니다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1
```

[setup_windows.ps1](../tools/setup_windows.ps1)은 Git LFS·CMake·Python 3.11·MSVC 2022·Qt 6.8.3(Widgets·TIFF)·LibTorch CPU Release를 준비하고 submodule·Git LFS 모델 다운로드, MSVC x64 환경 설정, Release 빌드와 두 배포 폴더 패키징을 수행합니다. LibTorch 버전은 pretrained 모델의 `deepprint.json`에 맞춥니다. Qt 계정 로그인은 필요하지 않습니다.

기본 의존성 경로는 `%LOCALAPPDATA%\FingerprintDeps`, 빌드 출력은 `build/Release`입니다. `-DependenciesPath`로 의존성 폴더, `-Jobs`로 병렬 빌드 수(기본 4)를 바꿀 수 있습니다. 재실행하면 준비된 도구·라이브러리를 재사용하며, 실패한 명령에서 중단합니다. 설치 프로그램이 재부팅을 요구하면 재부팅 후 같은 스크립트를 다시 실행합니다.

`models/trained_result`의 모델 4개는 Git LFS로 배포합니다. 정상 다운로드된 모델은 metadata·SHA-256 확인 후 재사용합니다. 파일이 없거나 호환되지 않으면 기존 폴더를 `trained_result_incomplete_*`로 보관하고, 원래 학습 설정으로 모델 4개를 학습한 뒤 `trained_result`에 저장합니다. 패키징한 두 폴더의 SHA-256 검사까지 통과해야 완료됩니다.

완료 후 [GUI 실행](../readme.md#2-gui)으로 진행합니다. `-LaunchGui`를 붙이면 준비 완료 후 GUI도 엽니다. 소스 저장소에서 GUI가 누락 파일을 발견하면 이 스크립트를 자동 실행하며, 실행 중인 EXE를 교체하지 않도록 이전 GUI가 종료된 뒤 준비하고 다시 엽니다. 소스가 없는 독립 배포본은 두 배포 폴더를 완전하게 함께 복사합니다.

참고: [WinGet](https://learn.microsoft.com/en-us/windows/package-manager/winget/install) · [MSVC](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio) · [aqtinstall](https://aqtinstall.readthedocs.io/en/stable/getting_started.html) · [LibTorch](https://download.pytorch.org/libtorch/cpu/)

## 환경

C++17, CMake 3.21+, Qt 5.15/6 Widgets·TIFF 플러그인이 필요합니다. 알고리즘·학습은 호환되는 LibTorch CPU를 사용합니다. Windows는 MSVC와 동일한 architecture의 Qt·LibTorch를 준비하고, 빌드 디렉터리 실행 시 Qt `bin`·LibTorch `lib`를 PATH에 추가합니다. 외부 32-bit DLL은 32-bit evaluator·worker가 필요합니다.

알고리즘·학습을 준비할 때는 submodule과 LFS 파일을 가져옵니다. upstream 수정 적용은 [NOTICE](../third_party/deepprint.NOTICE.txt)를 따릅니다. Python 3.11+은 선택적인 검사·manifest 도구에 사용됩니다.

```sh
git submodule update --init --recursive
git lfs pull
cmake -S . -B build -DCMAKE_PREFIX_PATH="<Qt>;<LibTorch>" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

## 실행

기본 Windows 빌드는 `build/Release/FingerprintGUI.exe`·`FingerprintTrain.exe`, 단일 구성 빌드는 `build/`의 실행파일을 사용합니다. 학습 명령·옵션은 [Training](../src/Training/readme.md)에 정리했습니다.

GUI는 `data/original`을 작업 폴더 또는 실행파일의 상위 폴더에서 찾습니다. DB 버튼으로 경로를 지정하거나 외부 폴더를 선택할 수도 있습니다. [FVC 매핑](../src/Public/readme.md)에 맞는 원본 gray8을 사용합니다. DLL 평가는 Windows에서 실행하며, 학습·검증 도구는 macOS/Linux에서도 빌드할 수 있습니다.

## Evaluator only

외부 DLL만 평가할 때는 Qt만 준비하고 아래 옵션을 사용합니다.

```sh
cmake -S . -B build-evaluator -DCMAKE_PREFIX_PATH="<Qt>" -DFINGERPRINT_BUILD_ALGORITHM=OFF -DFINGERPRINT_BUILD_TRAINING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-evaluator --config Release --parallel 4
```

## Package

Windows에서 기존 [모델 4개](../models/readme.md)가 있는 폴더를 지정합니다. 기본값은 `models/trained_result`입니다.

```sh
cmake -S . -B build -DFINGERPRINT_PACKAGE_MODELS="<학습 출력 폴더>"
cmake --build build --config Release --target FingerprintAlgorithmPackage
cmake --build build --config Release --target FingerprintEvaluatorPackage
python tools/verify_manifest.py dist/FingerprintAlgorithm
```

- `dist/FingerprintAlgorithm`: public DLL·Core·worker·모델·런타임·라이선스·SHA-256 목록.
- `dist/FingerprintEvaluator`: GUI·worker와 windeployqt로 수집한 Qt·TIFF·런타임·라이선스·SHA-256 목록.

DLL과 모델은 같은 배포 폴더에 둡니다. 필수 파일이 빠지면 기존 배포 폴더를 교체하지 않습니다. [검증 상태](verification.md) · [출처·라이선스](THIRD_PARTY.md)
