# 빌드·실행과 저장소 준비

저장소: [AdvancedOOP_TeamProj](https://github.com/12B-theDon/AdvancedOOP_TeamProj), 기본 브랜치: `main`.

## 1. Clone과 의존성

Git LFS를 설치하고 `git lfs install`을 실행한 뒤 `git clone --recurse-submodules`로 받습니다. 사전학습 파일이 내려오지 않았다면 프로젝트 루트에서 `git lfs pull`을 실행합니다. 빠른 실행 순서는 [메인 README](../readme.md#1-build--run)에, OS별 설치 경로와 DLL 준비·IDE 실행 방법은 아래에 정리했습니다.

| 항목 | 내용 |
|---|---|
| Submodule | `third_party/deepprint` |
| 원본 저장소 | `https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors.git` |
| 고정 Commit | `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13` |
| 로컬 수정 Patch | [deepprint.patch](../third_party/deepprint.patch) |
| 출처·수정 설명 | [deepprint.NOTICE.txt](../third_party/deepprint.NOTICE.txt) |

외부 소스는 복사본 대신 Submodule의 Git 항목으로 관리합니다. Patch는 원본 체크포인트를 직접 변환할 때 한 번 적용하며, 이미 배포된 사전학습 `.bin`으로 C++ 학습을 실행할 때는 필요하지 않습니다. 변환 방법과 Patch 적용 명령은 [Algorithm 안내](../src/Algorithm/readme.md#conversion--offline-run)에 있습니다. 이 로컬 수정을 외부 저장소에 커밋하지 않습니다.

Qt와 LibTorch는 별도로 설치합니다. 학습 초기 파일 `models/pretrained/deepprint.bin`은 Git LFS로, 정보 파일 `deepprint.json`은 일반 파일로 포함합니다. GUI용으로 Fine-tuning한 모델은 배포하지 않으므로 새 Clone에서는 먼저 학습하거나 별도로 받은 호환 모델을 선택해야 합니다.

## 2. Windows / Visual Studio

**Visual Studio 2022 IDE** 기준입니다. Installer에서 **Desktop development with C++**, MSVC v143 x64, Windows SDK, **C++ CMake tools for Windows**를 설치합니다.

Git·Git LFS, CMake 3.21 이상, MSVC x64용 Qt Widgets·TIFF plugin, Windows x64 CPU용 LibTorch가 필요합니다. LibTorch와 프로젝트는 **Release** 구성을 사용합니다. [Visual Studio 안내](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170) · [LibTorch 설치](https://docs.pytorch.org/cppdocs/installing.html)

아래 명령은 **Developer Command Prompt for VS 2022**에서 실행합니다. Windows 실행은 아직 검증하지 않았습니다.

### 2.1. 의존성 설치

이미 설치한 항목은 생략합니다. 아래 설치 명령은 **PowerShell**에서 실행합니다. `winget`이 없으면 Microsoft Store의 [앱 설치 관리자](https://learn.microsoft.com/en-us/windows/package-manager/winget/#install-winget)를 먼저 설치·갱신합니다.

**Git·Git LFS·CMake·MSVC**

```powershell
winget install --exact --id Git.Git
winget install --exact --id GitHub.GitLFS
winget install --exact --id Kitware.CMake
winget install --exact --id Microsoft.VisualStudio.2022.Community --override "--wait --passive --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
```

Visual Studio가 이미 있다면 Installer의 수정 화면에서 **Desktop development with C++**, MSVC v143 x64, Windows SDK, C++ CMake 도구를 확인합니다. 설치 후 터미널을 다시 엽니다. [WinGet 사용법](https://learn.microsoft.com/en-us/windows/package-manager/winget/) · [Visual Studio 설치 옵션](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022)

**Qt와 TIFF 플러그인**

공식 설치 프로그램을 내려받아 실행합니다. Qt 계정 로그인과 구성 요소 선택은 설치 화면에서 진행합니다.

```powershell
Invoke-WebRequest -Uri "https://download.qt.io/official_releases/online_installers/qt-online-installer-windows-x64-online.exe" -OutFile "$env:TEMP\fingerprint-qt-installer.exe"
& "$env:TEMP\fingerprint-qt-installer.exe" --root C:\deps\Qt
```

설치할 Qt 6 버전에서 **MSVC 2022 64-bit**와 **Qt Image Formats**를 선택합니다. MinGW·ARM64 패키지와 혼용하지 않습니다. Widgets는 Qt 기본 모듈에, TIFF 지원은 Image Formats 플러그인에 포함됩니다. 설치 후 `C:\deps\Qt\<설치 버전>\msvc2022_64\plugins\imageformats\qtiff.dll`을 확인합니다. 아래 빌드 예시의 `6.x.y`는 실제 설치 버전으로 바꿉니다. [Qt 설치 안내](https://doc.qt.io/qt-6/get-and-install-qt.html) · [TIFF 플러그인](https://doc.qt.io/qt-6/qtimageformats-index.html)

**LibTorch — Windows x64 CPU Release**

[공식 LibTorch 안내](https://docs.pytorch.org/cppdocs/installing.html)의 다운로드 선택기에서 **C++/LibTorch, Windows, CPU, Release(shared with dependencies)**에 맞는 ZIP 링크를 복사합니다. 아래 명령의 입력 창에 그 URL을 붙여 넣으면 `C:\deps\libtorch`에 압축을 풉니다. 이미 설치된 폴더에는 다시 덮어 풀지 않습니다.

```powershell
$FingerprintLibTorchUrl = Read-Host "Windows x64 CPU Release LibTorch ZIP URL"
New-Item -ItemType Directory -Force C:\deps | Out-Null
Invoke-WebRequest -Uri $FingerprintLibTorchUrl -OutFile "$env:TEMP\fingerprint-libtorch.zip"
Expand-Archive -Path "$env:TEMP\fingerprint-libtorch.zip" -DestinationPath C:\deps
Test-Path C:\deps\libtorch\share\cmake\Torch\TorchConfig.cmake
```

Python용 `pip install torch`만으로 아래 C++ 설치 경로가 준비되지는 않습니다. 배포된 학습 초기 파일은 PyTorch **2.15.0**에서 변환했으므로 LibTorch 버전 호환성도 확인합니다. 다른 버전으로 빌드·로딩한 결과를 검증된 환경과 동일하다고 단정하지 않습니다.

설치 확인 후 **Developer Command Prompt for VS 2022**로 바꿔 아래 Clone·Build 명령을 실행합니다.

### 2.2. Clone

```bat
git lfs install && git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git && cd AdvancedOOP_TeamProj
```

### 2.3. Build

Qt의 `6.x.y`를 실제 설치 버전으로 바꿉니다. 아래는 **Developer Command Prompt for VS 2022** 문법입니다.

```bat
set "FINGERPRINT_QT_PREFIX=C:\deps\Qt\6.x.y\msvc2022_64"
set "FINGERPRINT_TORCH_PREFIX=C:\deps\libtorch"
set "PATH=%FINGERPRINT_QT_PREFIX%\bin;%FINGERPRINT_TORCH_PREFIX%\lib;%PATH%"
set "QT_PLUGIN_PATH=%FINGERPRINT_QT_PREFIX%\plugins"
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%FINGERPRINT_QT_PREFIX%;%FINGERPRINT_TORCH_PREFIX%"
cmake --build build-vs --config Release --target FingerprintGUI FingerprintTrain --parallel 2
```

빌드가 성공하면 실행에 필요한 DLL과 Qt plugin을 준비합니다.

```bat
copy /Y "%FINGERPRINT_TORCH_PREFIX%\lib\*.dll" "build-vs\Release\"
"%FINGERPRINT_QT_PREFIX%\bin\windeployqt.exe" --release "build-vs\Release\FingerprintGUI.exe"
```

`build-vs/FingerprintEvaluator.sln`을 Visual Studio에서 열면 IDE에서도 빌드할 수 있습니다. 구성은 **Release / x64**로 선택합니다. [Qt 배포 안내](https://doc.qt.io/qt-6/windows-deployment.html)

### 2.4. Training

사전학습 파일 **`models/pretrained/deepprint.bin`**과 정보 파일 `deepprint.json`은 Clone할 때 함께 받습니다. 모델이 내려오지 않았다면 프로젝트 폴더에서 `git lfs pull`을 실행합니다. Python 변환은 필요하지 않습니다. [사전학습 파일 안내](../src/Algorithm/readme.md#conversion--offline-run)

**Clone 직후에는 `models/run_01`이 없습니다.** 아래 명령을 실행하면 출력 폴더가 자동 생성되고, 학습이 완료되면 GUI에서 사용할 네 모델이 저장됩니다. `deepprint.bin`은 학습 초기값이며 GUI에서 직접 선택하는 비교용 모델은 아닙니다.

```bat
build-vs\Release\FingerprintTrain.exe data\original models\pretrained\deepprint.bin models\run_01
```

세 DB의 개별 모델과 통합 모델을 각각 학습합니다. 네 모델 모두 30개 값을 출력하며, 판정 기준을 담은 `calibration.json`도 함께 저장합니다. 재학습에는 `run_02`처럼 **새 출력 폴더명**을 사용합니다. 이미 학습된 네 모델을 별도로 받은 경우에는 이 단계를 생략할 수 있습니다. [IDE에서 학습하기](../src/Algorithm/readme.md#windows--visual-studio)

### 2.5. Inference

```bat
build-vs\Release\FingerprintGUI.exe
```

GUI의 Step 3에서 `Select weights folder...`를 누르고 **학습이 완료된 모델 폴더**를 선택합니다. 위 Training 명령을 완료했다면 `models/run_01`, 다른 경로로 학습했거나 모델을 공유받았다면 해당 폴더를 선택합니다. Clone만 한 상태에서는 학습을 먼저 완료해야 비교할 수 있습니다.

IDE에서는 **FingerprintGUI → Set as Startup Project → Ctrl+F5**로 실행합니다. 새 Command Prompt를 열었다면 2.3 Build의 경로 설정을 다시 실행합니다.

## 3. macOS / Linux

C++17 compiler, Git·Git LFS, CMake 3.21 이상, Qt Widgets·TIFF plugin, OS·CPU에 맞는 LibTorch가 필요합니다. macOS arm64에서 Qt 5.15.17·LibTorch 2.15.0으로 빌드·학습·추론을 확인했습니다. Linux 실행은 아직 검증하지 않았습니다.

### 3.1. 의존성 설치

**macOS: Command Line Tools·Homebrew·Qt**

Command Line Tools와 Homebrew가 없다면 순서대로 설치합니다. 각 설치가 끝난 뒤 다음 명령을 진행하고, Homebrew가 안내하는 PATH 설정도 적용합니다.

```sh
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Homebrew가 준비되면 없는 패키지를 설치합니다. `qt@5`는 이 프로젝트에서 사용한 Qt 5 계열이며, 실제 설치되는 Patch 버전은 달라질 수 있습니다.

```sh
brew install git git-lfs cmake qt@5
xcode-select -p
cmake --version
git lfs version
ls "$(brew --prefix qt@5)/plugins/imageformats/"*tiff*
```

Homebrew의 Qt 5는 TIFF 라이브러리와 이미지 플러그인을 함께 제공합니다. [Homebrew 설치](https://brew.sh/) · [Command Line Tools 조건](https://docs.brew.sh/Installation) · [Qt 5 패키지](https://formulae.brew.sh/formula/qt%405)

**macOS: LibTorch CPU ZIP**

[공식 LibTorch 다운로드 안내](https://docs.pytorch.org/cppdocs/installing.html)에서 OS·CPU에 맞는 ZIP을 선택합니다. Apple Silicon은 **arm64**, Intel Mac은 **x86_64** 배포본을 사용하며 Qt·컴파일러의 아키텍처와 맞춥니다. 원하는 호환 버전의 ZIP 링크로 아래 `공식 LibTorch ZIP URL`을 바꿉니다.

```sh
FINGERPRINT_LIBTORCH_URL='공식 LibTorch ZIP URL'
curl -fL "$FINGERPRINT_LIBTORCH_URL" -o fingerprint-libtorch.zip
mkdir -p "$HOME/deps"
unzip fingerprint-libtorch.zip -d "$HOME/deps"
ls "$HOME/deps/libtorch/share/cmake/Torch/TorchConfig.cmake"
```

기존 `libtorch` 폴더에는 덮어 풀지 않습니다. 배포된 학습 초기 파일은 PyTorch **2.15.0**에서 변환했으므로 호환되는 LibTorch를 선택합니다. 새로 설치한 다른 버전의 동작은 별도로 확인해야 합니다. 아래 macOS 빌드 명령은 이 ZIP의 설치 위치를 사용합니다.

**Linux**는 배포판 패키지 관리자로 C++17 컴파일러·Git·Git LFS·CMake·Qt 개발 패키지와 TIFF 플러그인을 준비합니다. LibTorch는 Linux CPU용 배포본을 별도로 설치하고, 아래 `/opt/libtorch`를 실제 경로로 바꿉니다. Linux의 설치·실행은 아직 검증하지 않았습니다.

### 3.2. Clone

```sh
git lfs install && git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git && cd AdvancedOOP_TeamProj
```

### 3.3. Build

**macOS:** 위 설치 방법의 Homebrew Qt 5와 `$HOME/deps/libtorch`를 사용합니다. 설치 위치가 다르면 경로를 수정합니다.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5);$HOME/deps/libtorch" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FingerprintGUI FingerprintTrain --parallel 2
```

**Linux:** Qt가 `/usr`, LibTorch가 `/opt/libtorch`에 설치된 경우입니다.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/usr;/opt/libtorch" -DCMAKE_BUILD_TYPE=Release && cmake --build build --target FingerprintGUI FingerprintTrain --parallel 2
```

### 3.4. Training

`models/pretrained/deepprint.bin`은 Git LFS로 함께 받습니다. 파일이 없다면 `git lfs pull`을 실행합니다. Python 변환 없이 아래 명령으로 학습할 수 있습니다. [사전학습 파일 안내](../src/Algorithm/readme.md#conversion--offline-run)

**`models/run_01`은 Clone에 포함된 폴더가 아니라 학습 명령이 생성하는 출력 폴더입니다.**

```sh
./build/FingerprintTrain data/original models/pretrained/deepprint.bin models/run_01
```

데이터 증강, 네 모델의 학습과 저장, 저장된 모델의 검증, 판정 기준 산정까지 차례로 진행합니다. 출력 폴더의 네 `.bin`과 `calibration.json`을 함께 GUI에 사용합니다. 재학습에는 새 출력 폴더명을 사용합니다. `--augment-only`를 붙이면 증강만 생성하며, GUI용 모델은 생성하지 않습니다.

### 3.5. Inference

```sh
./build/FingerprintGUI
```

Step 3에서 **학습이 완료된 모델 폴더**를 선택합니다. 위 명령으로 학습했다면 `models/run_01`입니다. 학습을 생략하려면 별도로 받은 Fine-tuned 모델 폴더가 필요하며, `models/pretrained`는 GUI 비교용 폴더가 아닙니다. Linux에서는 GUI를 표시할 데스크톱 세션이 필요합니다.

## 4. License / Submodule

공개 [DeepPrint 재구현](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors)을 Submodule로 사용합니다. 위의 Clone 명령은 해당 소스도 함께 받습니다. 출처와 수정 사항은 [NOTICE](../third_party/deepprint.NOTICE.txt)에 있습니다.

| 구성 요소 | 확인 가능한 License |
|---|---|
| DeepPrint 재구현 | [LGPL-3.0](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/LICENSE) |
| InceptionV4 / Remi Cadene | [BSD-3-Clause](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/InceptionV4.py) |
| Qt | [LGPL-3.0 / GPL / Commercial 조건](https://doc.qt.io/qt-6/licensing.html), 사용하는 모듈과 배포본에 따라 확인 |
| LibTorch / PyTorch | [BSD-style 및 포함된 Third-party License](https://github.com/pytorch/pytorch/blob/main/LICENSE) |

공개 사전학습 weight의 별도 License는 확인되지 않았습니다. 소스 License와 구분하며, 준비 방법은 [Algorithm README](../src/Algorithm/readme.md#conversion--offline-run)를 참고합니다.

## 5. 포함·제외 대상

| 저장소에 포함 | 로컬에 보존하고 제외 |
|---|---|
| C++ 소스·CMake·원본 TIFF DB | 빌드·IDE·Python 캐시, 증강 이미지 |
| 학습 초기 `.bin`과 정보 JSON, `models/readme.md` | 직접 생성한 학습 모델·특징 캐시, `models/validation`의 결과 기록, 원본 체크포인트 |
| 구조도 SVG·PNG, GUI 화면 모음 PNG | `docs/GUI_flows/captures`의 원본 화면·실행 결과·샘플 이미지·요약 JSON |
| `docs/GUI_flows/captures/readme.md`, `Report_example` | `tools/readme.md`의 로컬 스타일 기록 |

`Report_example/templates/*.fpt`는 30차원 출력과 모델 식별 정보를 담은 작은 등록 템플릿이며 학습 Weight가 아닙니다. 완료된 학습 Weight·캐시·실험 코드는 설정·결과를 확인한 뒤 정리하고, 기록은 `models/validation`에 모았습니다. 재학습용 사전학습 파일은 유지하며, GUI용 모델은 직접 학습한 출력 폴더에서 선택합니다. 현재 폴더의 역할은 [모델 안내](../models/readme.md)를 참고합니다.

## 6. 예시와 검증 기록

`docs/GUI_flows`의 화면 모음은 2026-11-18의 30차원 모델 실행을 보여줍니다. 원본 캡처와 원시 결과는 로컬 자료이며 [캡처 기록 안내](GUI_flows/captures/readme.md)에서 범위를 설명합니다. 문서용 그림·캡처 생성 도구는 정리했고 결과 이미지는 유지합니다.

[메인 데모 GIF](../demo.gif)는 40 Epoch 학습을 완료한 Cosine30 모델의 macOS GUI 녹화입니다. [Report_example](../Report_example/report.html)은 40 Epoch의 네 모델로 Genuine 28쌍·Impostor 288쌍을 비교한 기록입니다. FVC2000·통합 모델의 최솟값과 임계값 0.5133을 사용하며, 다른 DB 모델은 참고용입니다. 리포트의 당시 경로·수치·판정은 보존합니다. 녹화 실행 로그와 재현 방법은 [GUI 안내](../src/GUI/readme.md#데모와-저장-리포트)를 참고합니다.

현재 최종 모델의 학습과 코드를 정리한 뒤 수행한 검증은 [Algorithm의 검증 범위](../src/Algorithm/readme.md#verification--limits)에 구분해 기록했습니다. 예시는 독립 Test 성능을 뜻하지 않습니다. Train/Validation은 같은 손가락의 촬영본 1–6 / 7–8로 나뉘며, 별도 Test 분할은 없습니다. 자동 GUI 검증은 Qt offscreen으로 수행했고 macOS에서는 데모를 직접 녹화했습니다. Windows/Linux 실행은 아직 검증하지 않았습니다.

배포 전에는 staged 파일에 로컬 산출물이 들어가지 않았는지, 문서의 상대 링크가 커밋될 파일을 가리키는지, Submodule Commit과 Git LFS 설정이 유지되는지 확인합니다. 처음 실행할 때는 [메인 README](../readme.md#1-build--run)의 순서를 따라가며, 플랫폼별 세부 설정은 이 문서를 참고합니다.
