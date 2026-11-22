# Fingerprint Evaluator

DeepPrint 기반 지문 알고리즘을 학습하고, K-NBTC 규격 DLL의 유사도·처리 시간·인식 성능을 평가하는 Windows·MSVC 전용 C++17 프로젝트입니다.

## 1. Install

Windows x64의 **PowerShell**에서 아래 순서대로 실행합니다. Git이 없다면 `winget install --exact --id Git.Git`으로 설치한 뒤 PowerShell을 다시 엽니다.

**저장소 복제**

```powershell
cd $env:USERPROFILE
git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git FinalProject
cd FinalProject
```

**자동 설치·빌드·배포 준비**

복제한 폴더에서 아래 블록을 실행합니다. [설치 스크립트](tools/setup_windows.ps1)가 도구·Qt·LibTorch 설치, Git LFS 가중치 다운로드, 빌드와 패키징까지 수행합니다. 학습된 모델 4개를 받으면 로컬 학습은 생략하며, 다운로드 후에도 모델이 없을 때만 자동 학습합니다. 설치 중 Windows 권한 요청이 표시되면 승인합니다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1
```

이미 준비된 도구와 라이브러리는 재사용합니다. 세부 옵션·수동 빌드는 [빌드 안내](docs/build.md)를 참고합니다.

## 2. GUI

설치가 완료되면 프로젝트 루트에서 아래 명령을 실행하거나, `dist/FingerprintEvaluator/FingerprintGUI.exe`를 더블클릭합니다.

```powershell
.\dist\FingerprintEvaluator\FingerprintGUI.exe
```

소스 저장소 안에서 GUI를 실행했는데 이 프로젝트의 DLL·학습 모델이 빠져 있으면 자동으로 준비 프로그램을 실행합니다. **LFS 다운로드 → 없는 경우에만 학습 → 패키징 → GUI 재실행** 순서로 복구합니다. 완성된 배포본은 학습 없이 바로 열립니다. 다른 PC로 옮길 때는 [dist/FingerprintAlgorithm/](docs/build.md#package)과 [dist/FingerprintEvaluator/](docs/build.md#package)를 함께 복사합니다.

GUI에서 **DB → 등록 이미지 → DLL·옵션·Load·Template 생성 → 비교 종류 → 비교 규칙 → 실행 → 결과·보고서** 순서로 진행합니다. Step 3에서 파일명에 관계없이 K-NBTC DLL을 선택하고 workers·timeout을 설정한 뒤 **Load selected DLL with these settings**를 누릅니다. 이 프로젝트는 [FingerprintAlgorithm.dll](src/Kisa/readme.md)을 선택합니다. 표에 `Loaded`가 표시되면 **Create enrollment templates**로 상태·크기·오류·생성 시간(ms)을 확인합니다. DLL·설정을 바꾸면 Load를 다시 눌러 적용합니다. Results에서 임계값(초기 0.5)을 바꾸고 CSV·HTML·Word 호환 RTF·SVG를 저장합니다. Load·Template 생성·비교·보고서 저장은 진행률과 **Stop**을 제공합니다.

[학습 옵션](src/Training/readme.md) · [Noise·Threshold 평가](docs/experiments/noise_followup.md) · [Mild noise 증강 실험](docs/experiments/weighted_noise.md) · [GUI·지표](src/GUI/readme.md) · [검증 상태](docs/verification.md)

## 3. Project structure

주요 소스와 배포 구조입니다. `dist/` 외의 빌드·증강 생성물과 로컬 개발 도구는 제외했습니다.

```text
FinalProject/
├─ src/
│  ├─ main.cpp       # GUI 실행 진입점
│  ├─ Public/        # 공개 KISA 함수 선언, FVC 매핑, 이미지 자료형
│  ├─ Algorithm/     # Unicode 모델 I/O·전처리·템플릿 생성·유사도 계산
│  ├─ Training/      # 데이터 증강·학습·학습 결과 검증
│  ├─ Kisa/          # 공개 DLL wrapper와 알고리즘 Core DLL
│  ├─ Engine/        # 비교 계획·템플릿 캐시·병렬 평가·원시 결과 저장
│  ├─ Worker/        # Windows DLL 실행 프로세스와 IPC 규약
│  └─ GUI/           # 단계별 화면·결과 표시·지표·보고서
├─ data/
│  └─ original/      # 보존 대상 원본 FVC 데이터
├─ models/
│  ├─ pretrained/     # 학습 초기화용 DeepPrint 가중치
│  └─ trained_result/ # Git LFS로 배포하는 학습 모델 4개·검증 manifest
├─ dist/             # 배포 결과 (패키징 시 생성, Git 제외)
│  ├─ FingerprintAlgorithm/ # 알고리즘 DLL·모델·runtime
│  └─ FingerprintEvaluator/ # GUI·Worker·Qt runtime·plugins
├─ docs/             # 빌드·검증·출처 문서, 구조도, 보존 기준
├─ cmake/            # 알고리즘·평가 프로그램 배포 폴더 생성 (dist/)
├─ tools/            # Windows 설치·빌드·패키징·배포 파일 검사
│  ├─ setup_windows.ps1  # 도구 설치·모델 다운로드·필요 시 학습·패키징
│  └─ verify_manifest.py # 배포 파일 누락·SHA-256 검사
├─ third_party/      # DeepPrint 소스·patch, Qt·LibTorch 라이선스 고지
└─ CMakeLists.txt    # 빌드 대상·의존성·패키징 명령 정의
```

패키징을 마치면 `dist/` 아래에 두 배포 폴더가 생성됩니다. 외부 DLL은 GUI Step 3에서 선택합니다. 이 프로젝트의 알고리즘을 배포할 때는 `dist/FingerprintAlgorithm/` 전체를 함께 배포합니다. DLL·모델을 교체한 뒤에는 GUI를 다시 실행하고 템플릿을 재생성합니다.

주요 설정은 각 모듈의 `*Config.h`에 있습니다. `encoderConfig.h`는 전처리·모델 차원, `trainConfig.h`·`augmentConfig.h`는 학습·증강, `evaluationConfig.h`는 학습 검증, `engineConfig.h`·`guiConfig.h`는 평가·화면 기본값을 정의합니다. 파일별 설명은 [소스 안내](src/readme.md)에서 확인할 수 있습니다.

<img src="docs/diagrams/overview.png" alt="평가 GUI·Engine·Worker·알고리즘 DLL과 학습 모듈의 전체 연결 구조" width="100%">

## 4. Libraries

주요 라이브러리와 대표 `#include`입니다.

| 라이브러리 | 대표 `#include` | 목적 |
|---|---|---|
| Qt Widgets | `<QApplication>`, `<QWizard>` | GUI |
| Qt Gui | `<QImage>`, `<QPainter>` | 이미지·augmentation |
| Qt Core | `<QFile>`, `<QSaveFile>`, `<QJsonDocument>`, `<QProcess>`, `<QCryptographicHash>` | 파일·JSON·Process·SHA-256 |
| LibTorch·ATen | `<torch/torch.h>`, `<torch/script.h>`, `<ATen/core/Tensor.h>` | 학습·inference·Tensor |
| C++ Standard Library | `<vector>`, `<thread>`, `<future>`, `<mutex>`, `<condition_variable>`, `<random>` | 자료구조·병렬·동기화·sampling |
| Windows API | `<windows.h>` | DLL·Process·IPC |

[구조도·모듈 안내](docs/diagrams/readme.md) · [출처·라이선스](docs/THIRD_PARTY.md)
