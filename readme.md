# Fingerprint Evaluator

지문 이미지를 등록하고 다른 촬영본과 비교해 유사도·처리 시간·인식 성능을 확인하는 C++ 프로젝트입니다. DeepPrint 기반 모델을 Partial Fine-tuning하고, 학습한 Cosine Classifier의 30차원 Softmax 출력을 템플릿으로 사용합니다.

## 1. Build / Run

C++17, Git·Git LFS, CMake 3.21 이상, Qt Widgets·TIFF 플러그인, OS·CPU에 맞는 LibTorch가 필요합니다. 아래는 **터미널 예시**입니다. Linux/macOS에서 Qt·LibTorch 설치 위치에 맞게 `/path/to/qt`와 `/path/to/libtorch`를 바꿉니다. [Windows / Visual Studio](docs/repository_preparation.md#2-windows--visual-studio) · [터미널 상세 설정](docs/repository_preparation.md#3-macos--linux)

**1.1. 의존성 설치 — 없는 항목만 설치**

macOS(Homebrew):

```sh
brew install git git-lfs cmake qt@5
```

Windows(PowerShell / WinGet):

```powershell
winget install --exact --id Git.Git
winget install --exact --id GitHub.GitLFS
winget install --exact --id Kitware.CMake
winget install --exact --id Microsoft.VisualStudio.2022.Community --override "--wait --passive --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
```

위 명령 외에 **LibTorch CPU ZIP**을 준비하고, Windows에서는 **Qt MSVC 2022 64-bit·TIFF 플러그인**도 설치합니다. Homebrew·WinGet 준비, Qt 설치와 LibTorch 다운로드·압축 해제는 [Windows 설치](docs/repository_preparation.md#21-의존성-설치) · [macOS 설치](docs/repository_preparation.md#31-의존성-설치)를 따릅니다. 설치가 끝나면 새 터미널을 엽니다.

**1.2. Clone**

```sh
git lfs install
git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git
cd AdvancedOOP_TeamProj
```

**1.3. Build**

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/qt;/path/to/libtorch" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FingerprintTrain FingerprintGUI --parallel 2
```

**1.4. Train → GUI**

```sh
./build/FingerprintTrain data/original models/pretrained/deepprint.bin models/run_01
./build/FingerprintGUI
```

Clone에는 **학습 초기 파일**만 포함됩니다. 기본 학습은 40 Epoch이며, 학습이 끝나면 GUI의 Step 3에서 **Select weights folder...**로 출력 폴더 `models/run_01`을 선택합니다. 이미 학습한 호환 모델이 있다면 그 폴더를 선택하고 학습을 생략할 수 있습니다. 재학습에는 새 출력 폴더명을 사용합니다. [학습·검증 옵션](src/Algorithm/readme.md#conversion--offline-run) · [모델 준비와 보관](models/readme.md)

macOS arm64에서 빌드·학습·추론과 GUI 데모 녹화를 수행했으며, 자동 GUI 검증은 Qt offscreen으로 진행했습니다. Windows/Linux 실행은 아직 검증하지 않았습니다.

## 2. Architecture

Algorithm은 전처리·학습·특징 추출, Engine은 모델 실행·비교, GUI는 사용자 입력·결과 표시·리포트를 담당합니다.

[![Project overview](docs/diagrams/overview.png)](docs/diagrams/overview.svg)

### 2.1. Algorithm

Backbone을 고정하고 Texture·Minutia Projection과 Cosine Classifier를 학습합니다. 등록·비교 이미지에서 각각 생성한 30차원 템플릿을 코사인 유사도로 비교합니다. [구조·전처리·학습·실험 결과](src/Algorithm/readme.md)

[![Algorithm](docs/diagrams/algorithm.png)](docs/diagrams/algorithm.svg)

**모델 구조와 학습 범위**

[![DeepPrint 모델 구조와 Partial Fine-tuning 범위](docs/diagrams/deepprint_finetuning.png)](docs/diagrams/deepprint_finetuning.svg)

**Inception·Reduction 블록 내부 구조**

[![Inception과 Reduction 블록 내부 구조](docs/diagrams/deepprint_blocks.png)](docs/diagrams/deepprint_blocks.svg)

### 2.2. Engine

비교할 이미지쌍을 구성하고 선택한 모델을 실행합니다. 모델별 템플릿·점수·시간·오류를 모아 저장합니다. Genuine·Impostor 모두 같은 DB·통합 모델의 최솟값으로 판정하고, 다른 DB 모델의 점수는 참고용으로 표시합니다. [자료형·비교 규칙·파일 형식·확장](src/Engine/readme.md)

[![Engine](docs/diagrams/engine.png)](docs/diagrams/engine.svg)

### 2.3. GUI Flow / Report

**1. DB 선택 → 2. 이미지 등록 → 3. 템플릿 생성 → 4. 비교 종류 → 5. 비교 규칙 → 6. 실행 → 7. 결과 확인** 순서로 사용합니다. 결과 화면에서 Genuine·Impostor 판정 기준을 각각 조절하고 지표·CSV·HTML 리포트를 저장합니다. [화면별 사용법·지표·리포트](src/GUI/readme.md)

[![GUI and report](docs/diagrams/gui.png)](docs/diagrams/gui.svg)

![GUI demo](demo.gif)

위 GIF는 **40 Epoch로 학습한 Cosine Classifier·30차원 Template 모델**의 등록·비교 흐름입니다. [리포트 예시](Report_example/report.html)는 40 Epoch 모델 4개로 Genuine 28쌍·Impostor 288쌍을 비교한 저장 기록입니다. 같은 DB·통합 모델의 최솟값과 임계값 0.5133을 사용합니다. 단계별 화면은 [전체 흐름](docs/GUI_flows/00_overview.png) · [한 장 등록](docs/GUI_flows/01_single_enrollment.png) · [여러 장 등록](docs/GUI_flows/02_multiple_enrollment.png) · [외부 DB](docs/GUI_flows/03_external_db.png)에서 확인할 수 있습니다.

점수는 동일 지문일 확률이 아닙니다. 성능 수치는 학습에 등장한 손가락의 다른 촬영본 평가이며, 독립 Test나 미학습 손가락 성능과 구분합니다. [평가 결과와 한계](src/Algorithm/readme.md#final-production-run--calibration)

## 3. Libraries / Headers

| 라이브러리 | 대표 헤더 | 용도 |
|---|---|---|
| Qt Core | `QDir`, `QFile`, `QJsonDocument` | 파일·경로·메타데이터 |
| Qt Gui | `QImage`, `QPainter` | TIFF 입출력·증강·이미지 표시 |
| Qt Widgets | `QWizard`, `QTableWidget`, `QFileDialog` | 단계별 GUI·표·입력 |
| LibTorch / ATen | `torch/script.h`, `torch/torch.h`, `ATen/core/Tensor.h` | 모델 저장·실행·Tensor 연산·학습 |
| C++ 표준 라이브러리 | `vector`, `string`, `cmath`, `chrono` | 컨테이너·문자열·수학·시간 측정 |

증강·학습·추론은 C++로 실행합니다. Python은 공개 원본 체크포인트를 직접 변환할 때만 필요합니다. 실제 빌드 의존성은 [CMakeLists.txt](CMakeLists.txt)를 참고합니다.

## 4. License / Submodule

[DeepPrint 논문](https://arxiv.org/abs/1909.09901)과 [공개 재구현](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors)을 기반으로 합니다. 소스 출처·수정 사항은 [NOTICE](third_party/deepprint.NOTICE.txt), 구성 요소별 라이선스와 배포 범위는 [저장소 안내](docs/repository_preparation.md#4-license--submodule)에 정리했습니다. 공개 체크포인트의 별도 라이선스는 확인되지 않았습니다.
