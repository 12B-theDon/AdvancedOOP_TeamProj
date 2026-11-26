# Fingerprint Evaluator

DeepPrint 기반 지문 알고리즘을 학습하고, K-NBTC 규격 DLL의 유사도·처리 시간·인식 성능을 평가하는 Windows·MSVC 전용 C++17 프로젝트입니다.

## 1. 설치

**[install.exe 다운로드](https://github.com/12B-theDon/AdvancedOOP_TeamProj/releases/download/windows-ui-installer-20261127-preview/install.exe)** · **[Uninstall.exe 다운로드](https://github.com/12B-theDon/AdvancedOOP_TeamProj/releases/download/windows-ui-installer-20261127-preview/Uninstall.exe)** — Windows 10 이상 · x64

`install.exe`를 더블클릭하고 **설치 위치 → 바로가기 → 설치 → 완료·GUI 실행** 순서로 진행합니다. 프로그램·필수 runtime·학습 모델 4개·원본 예제 240장을 포함하므로, 소스 복제나 개발 도구 설치 없이 사용할 수 있습니다.

설치 후에는 Start 메뉴 또는 바탕 화면의 **Fingerprint Evaluator**를 실행합니다. 제거할 때는 `Uninstall.exe`를 실행하고 제거 확인 창에서 승인합니다. Windows **설정 → 앱 → Fingerprint Evaluator → 제거**에서도 같은 방식으로 삭제하며, 사용자 보고서와 변경한 모델의 보관본은 유지합니다. [설치·복구·제거 안내](docs/installer.md)

<details>
<summary>소스에서 직접 설치·빌드하기 (Windows x64 · PowerShell)</summary>

Git이 없다면 아래 명령으로 설치한 뒤 PowerShell을 다시 엽니다.

```powershell
winget install --exact --id Git.Git
```

저장소를 복제하고 프로젝트 폴더로 이동합니다.

```powershell
cd $env:USERPROFILE
git clone --recurse-submodules https://github.com/12B-theDon/AdvancedOOP_TeamProj.git FinalProject
cd FinalProject
```

아래 명령은 도구·Qt·LibTorch 설치, Git LFS 모델 다운로드, 빌드·패키징을 수행합니다. 모델 4개를 받으면 로컬 학습을 생략하고, 다운로드 후에도 모델이 없을 때만 학습합니다. 이미 준비된 도구와 라이브러리는 재사용합니다.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\tools\setup_windows.ps1
```

빌드가 끝나면 프로젝트 루트에서 GUI를 실행합니다. `dist/FingerprintEvaluator/FingerprintGUI.exe`를 더블클릭해도 됩니다.

```powershell
.\dist\FingerprintEvaluator\FingerprintGUI.exe
```

소스 저장소에서 실행할 때 DLL·모델이 빠져 있으면 **LFS 다운로드 → 필요한 경우 학습 → 패키징 → GUI 재실행** 순서로 준비합니다.

</details>

세부 옵션과 수동 빌드는 [빌드 안내](docs/build.md), 학습 설정은 [학습 옵션](src/Training/readme.md)을 참고합니다.

## 2. GUI 사용

**DB → 등록 이미지 → DLL·설정·Load·Template 생성 → 비교 종류 → 비교 규칙 → 실행 → 결과·보고서** 순서로 진행합니다.

Step 3에서 K-NBTC DLL을 선택하고 workers·timeout을 설정한 뒤 **Load selected DLL with these settings**를 누릅니다. 이 프로젝트는 `FingerprintAlgorithm/` 폴더의 **FingerprintAlgorithm.dll**을 선택합니다. `Loaded`가 표시되면 **Create enrollment templates**를 눌러 상태·크기·오류·생성 시간(ms)을 확인합니다. DLL이나 설정을 바꾸면 Load를 다시 누릅니다.

Results에서는 Threshold(초기 **0.30**, 다른 팀 DLL은 별도 조절)를 바꾸고 CSV·HTML·Word 호환 RTF·SVG를 저장합니다. Load·Template 생성·비교·보고서 저장은 진행률과 **Stop**을 제공합니다. [GUI·지표 안내](src/GUI/readme.md) · [DLL 규약](src/Kisa/readme.md)

## 3. 디렉터리 구성

주요 소스와 배포 폴더입니다. 빌드 cache·증강 생성물·로컬 개발 도구는 생략했습니다.

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
│  ├─ GUI/           # 단계별 화면·결과 표시·지표·보고서
│  └─ Installer/     # Windows 설치 마법사·빌드·배포 검사
├─ data/
│  └─ original/      # 보존 대상 원본 FVC 데이터
├─ models/
│  ├─ pretrained/     # 학습 초기화용 DeepPrint 가중치
│  └─ trained_result/ # Git LFS로 배포하는 학습 모델 4개·파일 검사값 목록
├─ dist/             # 배포 결과 (패키징 시 생성, Git 제외)
│  ├─ FingerprintAlgorithm/ # 알고리즘 DLL·모델·runtime
│  ├─ FingerprintEvaluator/ # GUI·Worker·Qt runtime·plugins
│  └─ FingerprintSetup/     # install.exe·Uninstall.exe
├─ docs/             # 빌드·검증·출처 문서, 구조도, 보존 기준
├─ cmake/            # 알고리즘·평가 프로그램 배포 폴더 생성 (dist/)
├─ tools/            # Windows 설치·빌드·패키징·배포 파일 검사
│  ├─ bootstrap_windows.ps1 # 개발용 설치의 WinGet 준비
│  ├─ setup_windows.ps1  # 도구 설치·모델 다운로드·필요 시 학습·패키징
│  └─ verify_manifest.py # 배포 파일 누락·SHA-256 검사
├─ third_party/      # DeepPrint 소스·patch, Qt·LibTorch 라이선스 고지
└─ CMakeLists.txt    # 빌드 대상·의존성·패키징 명령 정의
```

패키징하면 `dist/` 아래에 Algorithm·Evaluator 폴더가 생성되고, 설치기 빌드 시 Setup 폴더에 설치·제거 EXE가 만들어집니다. 다른 팀에 알고리즘을 전달할 때는 `dist/FingerprintAlgorithm/` 전체를 전달합니다. DLL·모델을 교체한 뒤에는 GUI를 다시 실행하고 Template을 재생성합니다.

주요 설정은 각 모듈의 `*Config.h`에서 관리합니다. `encoderConfig.h`는 전처리·모델 차원, `trainConfig.h`·`augmentConfig.h`는 학습·증강, `evaluationConfig.h`는 학습 검증, `engineConfig.h`·`guiConfig.h`는 평가·화면 기본값을 정의합니다. 파일별 설명은 [소스 안내](src/readme.md)에서 확인할 수 있습니다.

<img src="docs/diagrams/overview.png" alt="평가 GUI·Engine·Worker·알고리즘 DLL과 학습 모듈의 전체 연결 구조" width="100%">

## 4. 사용 라이브러리

주요 라이브러리의 용도와 대표 `#include`입니다.

| 라이브러리 | 대표 `#include` | 목적 |
|---|---|---|
| Qt Widgets | `<QApplication>`, `<QWizard>` | GUI |
| Qt Gui | `<QImage>`, `<QPainter>` | 이미지·augmentation |
| Qt Core | `<QFile>`, `<QSaveFile>`, `<QJsonDocument>`, `<QProcess>`, `<QCryptographicHash>` | 파일·JSON·Process·지문 ID 목록 검사 |
| LibTorch·ATen | `<torch/torch.h>`, `<torch/script.h>`, `<ATen/core/Tensor.h>` | 학습·inference·Tensor |
| C++ Standard Library | `<vector>`, `<thread>`, `<future>`, `<mutex>`, `<condition_variable>`, `<random>` | 자료구조·병렬·동기화·sampling |
| Windows API | `<windows.h>`, `<bcrypt.h>` | DLL·Process·IPC·파일 검사 |

SHA-256은 파일 내용을 64자리 16진수 검사값으로 나타내는 방법입니다. 모델 파일의 변경 여부와 Template을 만든 모델을 확인할 때 사용합니다. 계산에는 Windows 기본 기능인 BCrypt를 사용합니다.

[구조도·모듈 안내](docs/diagrams/readme.md) · [출처·라이선스](docs/THIRD_PARTY.md)

## 5. 주요 성능

<table width="100%" style="width:100%;table-layout:fixed">
  <thead>
    <tr>
      <th width="28%" align="left">평가 지표</th>
      <th width="18%">원본</th>
      <th width="18%">원본 + Gaussian noise σ10</th>
      <th width="18%">원본 + Gaussian noise σ30</th>
      <th width="18%">원본 + Gaussian noise σ50</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>EER (%)</td><td align="right">0.046</td><td align="right">0.244</td><td align="right">1.208</td><td align="right">6.518</td></tr>
    <tr><td>FMR (%)</td><td align="right">0.185</td><td align="right">0.249</td><td align="right">2.082</td><td align="right">7.448</td></tr>
    <tr><td>FNMR (%)</td><td align="right">0.000</td><td align="right">0.381</td><td align="right">0.851</td><td align="right">5.798</td></tr>
    <tr><td>Balanced Accuracy (%)</td><td align="right">99.907</td><td align="right">99.685</td><td align="right">98.534</td><td align="right">93.377</td></tr>
  </tbody>
</table>

> 세 DB의 원본 240장을 사용해 DB 내부의 전체 비교쌍을 평가한 뒤, **DB별 지표를 같은 비중으로 평균**했습니다. 등록 이미지는 원본으로 유지하고 인식 이미지에만 pixel-wise Gaussian noise를 추가했습니다. 각 노이즈 조건은 서로 다른 10개 seed로 만든 잡음 패턴의 평균입니다. FMR·FNMR·Balanced Accuracy는 **Threshold 0.30** 기준이며, EER는 본인·타인에 같은 임계값을 적용하면서 이를 바꿔 계산합니다.

기존 모델 4개를 사용하며, 점수는 **raw cosine → DB 전용·combined 최솟값 → 소수 셋째 자리 반올림** 순서로 계산합니다. 학습에 사용한 원본이 포함된 평가이므로, 학습하지 않은 새 지문 identity에 대한 성능과 구분합니다.

<details>
<summary>이전 점수 방식의 결과 보존</summary>

아래는 이전 `(cosine + 1) / 2`·Threshold **0.65**의 기록입니다. 세 DB의 비교쌍을 합산한 통합 지표이고 노이즈 seed도 달라, 위의 DB별 평균과 직접 비교하지 않습니다.

<table width="100%" style="width:100%;table-layout:fixed">
  <thead>
    <tr>
      <th width="28%" align="left">평가 지표</th>
      <th width="18%">원본</th>
      <th width="18%">원본 + Gaussian noise σ10</th>
      <th width="18%">원본 + Gaussian noise σ30</th>
      <th width="18%">원본 + Gaussian noise σ50</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>EER (%)</td><td align="right">0.152</td><td align="right">0.302</td><td align="right">1.391</td><td align="right">6.467</td></tr>
    <tr><td>FMR (%)</td><td align="right">0.185</td><td align="right">0.214</td><td align="right">2.071</td><td align="right">7.553</td></tr>
    <tr><td>FNMR (%)</td><td align="right">0.000</td><td align="right">0.351</td><td align="right">0.893</td><td align="right">5.714</td></tr>
    <tr><td>Balanced Accuracy (%)</td><td align="right">99.907</td><td align="right">99.718</td><td align="right">98.518</td><td align="right">93.366</td></tr>
  </tbody>
</table>

</details>
