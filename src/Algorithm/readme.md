# Algorithm

[Root](../../readme.md) · [Engine](../Engine/readme.md) · [GUI](../GUI/readme.md)

Source diagram: [SVG](../../docs/diagrams/algorithm.svg) · [PNG](../../docs/diagrams/algorithm.png). 증강·학습 과정과 실행 중 특징을 추출하는 경로를 함께 보여준다.

![Algorithm structure](../../docs/diagrams/algorithm.png)

## Responsibility / Files

지문 이미지의 전처리와 특징 추출을 담당한다. Enrollment와 Attempt에 같은 처리를 적용하고, 두 벡터의 Cosine을 `(cosine + 1) / 2`로 바꿔 0~1 유사도로 반환한다. 이 값은 동일 지문일 확률이 아니다. 증강과 Fine-tuning은 GUI 추론과 분리된 C++ 프로그램 `FingerprintTrain`에서 수행한다.

| 파일 | 역할 |
|---|---|
| [Algorithm.h](Algorithm.h) | Engine이 사용하는 Abstract API와 `AlgorithmImage`, `PreparedImage`를 정의한다. |
| [encoder.h](encoder.h) | `FingerprintEncoder` 선언이다. PImpl로 Torch type을 다른 Header에서 숨긴다. |
| [encoder.cpp](encoder.cpp) | Weight/Metadata 확인, 동일 Preprocessing, Encoding, Template 직렬화, Cosine comparison을 구현한다. |
| [encoderConfig.h](encoderConfig.h) | Architecture·Input size·Feature size·Gabor·검증 Tolerance·CPU thread 설정이다. |
| [augment.h](augment.h) | 원본·증강본의 경로, Identity, Split을 담는 `TrainingImage`와 준비 함수를 선언한다. |
| [augment.cpp](augment.cpp) | 설정에 맞는 Cache를 찾고 PNG·Manifest를 생성하거나 재사용한다. |
| [augmentConfig.h](augmentConfig.h) | 변형 범위·확률·개수·Seed·Clipping 제한을 정한다. |
| [train.cpp](train.cpp) | `FingerprintTrain` 진입점이다. 네 조건의 독립 학습·저장·Reload·Engine 검증을 수행한다. |
| [trainConfig.h](trainConfig.h) | 학습 Layer 범위·Optimizer·LR·Batch·Epoch·Split 설정이다. |
| [prepare_deepprint.py](../../tools/prepare_deepprint.py) | 원본 Python checkpoint를 Trainable TorchScript로 1회 변환한다. |

## Includes

| 사용 위치 | 주요 Include | 사용 이유 |
|---|---|---|
| API | `string`, `vector` | 모델 ID·오류 문자열·Pixel·Feature buffer를 주고받는다. |
| encoder.h | `memory` | `unique_ptr<State>`로 Module 보관부의 수명을 관리한다. |
| encoder.cpp | `torch/script.h`, `torch/torch.h` | TorchScript Module, Tensor, `NoGradGuard`, CPU thread를 사용한다. |
| encoder.cpp | `ATen/ops/_upsample_bilinear2d_aa.h` | Antialiased bilinear resize를 원본 Preprocessing과 맞춘다. |
| encoder.cpp | `QJsonDocument`, `QJsonObject`, `cmath`, `cstring` | Metadata 확인, Norm/Sqrt, Float32와 Byte buffer 간 복사를 수행한다. `QFile`은 현재 Include만 있고 직접 사용하지 않는다. |
| augment.cpp | `QImage`, `QPainter`, `random` | Gray8 image의 기하 변환과 Gaussian noise를 만든다. |
| augment.cpp | `QDir`, `QFile`, `QCryptographicHash`, `QJson*`, `QSaveFile`, `QtEndian` | Cache 탐색, SHA-256, Manifest, 안전한 기록, 파일별 Seed 구성을 처리한다. |
| train.cpp | `torch/*`, `QCoreApplication`, `QSaveFile`, `QCryptographicHash` | Autograd·Optimizer, CLI 인자, Log/Metadata 저장, Checkpoint 식별을 처리한다. |
| Offline 경로 | [Engine.h](../Engine/Engine.h), [engineConfig.h](../Engine/engineConfig.h) | DB·모델 ID를 공유하고 같은 Engine 경로로 저장 결과를 검증한다. |

## Pretrained Model / Training Provenance

### Original DeepPrint

아래는 [Engelsma·Cao·Jain의 원본 논문](https://arxiv.org/html/1909.09901v2) III·XIII 절의 내용이다. 현재 파일을 학습한 기록과 구분한다.

| 항목 | 논문에 보고된 내용 |
|---|---|
| Training data | Longitudinal rolled fingerprint 약 455K장, 38,291개 Finger identity. |
| Architecture | Localization/Alignment → Inception-v4 Stem → Texture·Minutiae branch. 두 96-dimensional vector를 합쳐 192-dimensional representation을 만든다. |
| Supervision | 두 Identity classification, Center loss, Minutiae map MSE를 결합한다. Loss weight는 1, 0.00125, 0.095다. |
| Training | RMSProp, Batch 30, 140K steps, 약 25시간. Dropout keep probability 0.8, Weight decay 0.00004. |
| Resource | TensorFlow 1.14.0, NVIDIA GeForce RTX 2080 Ti 2대. Search/Authentication 실험용 Core i9-7900X·RAM 32 GB는 Training 자원과 구분한다. |

이 프로젝트는 이 논문의 원본 Weight를 로드하지 않는다. 논문의 200-byte 압축 Template도 구현하지 않는다.

### Public Reimplementation / Checkpoint

실제 Source는 [Rohwedder et al.의 fixed-length-fingerprint-extractors](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/tree/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13)다. Commit은 `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13`이다.

| 근거 | 확인한 Training data·방법 |
|---|---|
| [BIOSIG 2023 논문](https://arxiv.org/html/2307.08615v1) III-A | SFinGe 4,000 fingers × 10 = 40,000장과 MCYT 2,000 fingers × 12 × 2 sensors = 48,000장. 합계 88,000장으로 서술한다. Input은 299×299 [px], Gabor enhancement와 Rotation·Translation·Brightness/Contrast augmentation을 사용한다. |
| [저장소 README](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/README.md)·[datasets.py](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/setup/datasets.py) | SFinGe Training 6,000 fingers × 10 = 60,000장, MCYT Training 2,000 fingers × 12 × 2 = 48,000장이다. Identity는 Sensor 간 공유하므로 총 8,000 classes다. SFinGe Validation은 별도 2,000 fingers × 4장이다. |
| [deep_print_loss.py](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/deep_print_loss.py) | Branch별 Cross entropy + Center loss, Minutiae map MSE를 사용한다. 구현의 계수는 각각 1, 0.125, 0.3이다. 원본 논문 계수와 다르다. |
| [model_training.py](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/model_training.py) | Adam을 사용한다. 저장 Code는 Validation EER가 가장 좋은 시점의 Checkpoint를 보관한다. README의 종료 설명만으로 정확한 학습 Epoch을 확정할 수 없다. |

논문의 4,000 synthetic fingers와 저장소의 6,000은 다르다. 다운로드 파일의 Classifier shape은 `[8000,256]`이므로 저장소 설정과는 부합한다. 그러나 Class 수만으로 실제 Training image 목록을 증명할 수는 없다. 이 Checkpoint의 완전한 Dataset manifest·Training log는 제공 파일에서 확인되지 않았다.

| 실제 다운로드 파일 | 확인 내용 |
|---|---|
| `models/pretrained/best_model.pyt` | 저자 [Google Drive 공개 파일](https://drive.google.com/file/d/1R9v73hFBuy0iihz7v8eDlBcGrMDmQ9TH/view). Python의 `model_state_dict`, `loss_state_dict`, `optimizer_state_dict`를 포함한다. |
| SHA-256 | `2683a04427bacd54adc00cfdc97474625b1e11e5a9e6672c5129f033018f8a28` |
| Architecture 확인 | `DeepPrint_TexMinu(8000,256,256)`에 `strict=True`로 전체 State dict가 일치했다. |
| Optimizer snapshot 확인 | LR 0.0015, Betas `(0.9,0.999)`, Epsilon `1e-8`, Weight decay 0, Step 526,500. 이는 저장 시점의 State이며 전체 LR schedule·총 Epoch을 뜻하지 않는다. |
| Converted archive | `models/pretrained/deepprint.bin`, `models/pretrained/deepprint.json`. `model_id=pretrained`이며 Fine-tuning 초기값으로 사용한다. GUI의 네 Model ID와는 다르다. |

### Pretraining Resource / Known Limits

| 근거 | 확인 범위 |
|---|---|
| Upstream README | A100에서 Epoch당 약 10–15분, 최대 약 100 Epoch의 안내가 있다. CPU 8 cores 이상을 권장한다. 해당 Checkpoint의 실측 시간으로 단정할 수 없다. |
| [torch_helpers.py](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/torch_helpers.py) | Training Batch 16, CUDA device 0, A100 분기와 40 GB 관련 주석이 있다. 실제 GPU 수·GPU 사용량·Peak RAM/VRAM 기록은 아니다. |
| Checkpoint 자체 | GPU model·수량, OS, 정확한 PyTorch version, Wall time, Peak memory, Epoch 이력은 확인되지 않았다. 이를 현재 PC의 실행 환경으로 대신 채우지 않는다. |

## Encoder Architecture / Preprocessing

근거는 [원본 Architecture](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/deep_print_arch.py), [InceptionV4](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/InceptionV4.py), [Conversion wrapper](../../tools/prepare_deepprint.py)다. 선택한 `TexMinu` 변형에는 `Loc` Alignment network가 없다.

| 경로 | 구성·출력 | 이번 Fine-tuning |
|---|---|---|
| Input | Gray image `N×1×299×299` | 공통 형식을 유지한다. |
| Stem | 3개 BasicConv와 Mixed_3a/4a/5a, 공통 Feature map | 고정한다. |
| Texture backbone | Inception-A×4 → Reduction-A → Inception-B×7 → Reduction-B → Inception-C×3 → Average pool → 1,536 features | 고정한다. |
| Texture projection | Linear `1536→256` + L2 normalize | Weight·Bias를 학습한다. |
| Minutia backbone | Inception-A×6 → Conv `384→768→768→896→1024` → Max pool → 1,024 features | 고정한다. |
| Minutia projection | Linear `1024→256` + L2 normalize | Weight·Bias를 학습한다. |
| Output | 두 Vector concatenate → L2 normalize → 512 features | Float32 512개, 현재 2,048 [bytes]다. |
| Pretraining 전용 | 두 8,000-class head와 Minutiae map decoder | 변환된 Encoder에서 제외한다. |

Preprocessing은 `Gray8/255 → DB별 정사각형 변환 → Antialiased bilinear 299×299 resize → Gabor binarization`이다. **FVC2004_DB1_B는 중앙 Crop**으로 640×480의 좌우 80 [px]를 잘라 480×480으로 만들고, FVC2000·FVC2002·출처가 불명확한 이미지는 White square padding을 사용한다. Gabor ridge width는 1.8 [px]다. Width/Height 차이가 홀수면 Right/Bottom에서 1 [px] 더 제거하거나 Padding한다.

학습·Enrollment·Attempt는 모두 `FingerprintEncoder::Preprocess`를 사용한다. `AlgorithmImage.database_id`로 입력 DB를 전달하며, 선택한 모델이나 Archive 버전에 관계없이 같은 규칙을 적용한다. Engine은 명시된 DB ID를 우선하고, 생략하면 알려진 부모 폴더 이름으로 식별한다. 기존 Archive도 호환되는 Encoder·Gabor 함수를 사용해 그대로 로드하며, Weight 파일은 변경하지 않는다.

Padding과 중앙 Crop을 비교한 결과 FVC2004의 Validation EER이 13.33%에서 6.67%로 개선되어 중앙 Crop을 채택하였다.

Upstream의 Constant-image normalization을 `torch.where`로 바꾸고 불필요한 Display import를 제거했다. Texture branch의 Batch-one `squeeze` 문제를 Wrapper에서 피했다. BatchNorm은 Eval 상태이며 Wrapper에는 Dropout을 포함하지 않는다. [변경 고지](../../third_party/deepprint.NOTICE.txt)를 참고한다.

## Fine-tuning / Data Split

원본은 [data/original](../../data/original)에 보존한다. 각 DB는 10 fingers × 8 captures = 80장이다. 먼저 Capture ID 1–6을 Training, 7–8을 Validation으로 나눈다. 모든 Augmentation은 원본의 Split을 상속한다. Validation에는 원본만 사용한다. 같은 손가락의 촬영본을 나누는 방식이며, 손가락 ID를 분리한 학습이나 별도 Test split은 없다.

학습 결과는 새 폴더에 저장하며 기존 Weight를 덮어쓰지 않는다. 모델·Metadata·학습 CSV는 Git에 포함하지 않으므로 Clone 후 준비해야 한다.

| 모델 | Classes | Training originals | 사용 Augmentation | Training 합계 | Validation originals |
|---|---:|---:|---:|---:|---:|
| `fvc2000_db3.bin` | 10 | 60 | 720 | 780 | 20 |
| `fvc2002_db3.bin` | 10 | 60 | 720 | 780 | 20 |
| `fvc2004_db1.bin` | 10 | 60 | 720 | 780 | 20 |
| `combined.bin` | 30 | 180 | 2,160 | 2,340 | 60 |

통합 Label은 `DB name:finger ID`다. 서로 다른 DB의 같은 숫자 ID를 다른 Class로 취급한다. 전체 원본에 대해 생성한 Augmentation은 2,880장이다. 이 중 Validation 소속 720장은 학습·검증에 넣지 않는다.

1. 같은 `models/pretrained/deepprint.bin`에서 Frozen backbone feature를 추출한다. 2,400개 Sample의 2,560-dimensional feature를 기본 Batch 4로 준비한다.
2. 각 모델마다 동일한 Archive를 다시 로드한다. 앞 모델의 학습 결과를 다음 모델에 넘기지 않는다.
3. `TrainablePrefixes`에 해당하는 두 Projection의 Weight·Bias만 Gradient를 켠다. 학습 Encoder parameter는 655,872개다.
4. 임시 Bias-free Linear classifier `512→10` 또는 `512→30`을 별도로 만든다. 이 Head는 각각 5,120·15,360개 Parameter를 추가로 학습한다.
5. 정규화된 Embedding과 정규화된 Classifier Weight의 Cosine에 16을 곱해 Logit을 만든다. Finger ID의 Cross entropy로 Projection과 임시 Head를 학습한다.
6. 고정 20 Epoch 후 Encoder만 저장한다. Validation loss로 Early stopping하거나 Best Epoch을 선택하는 코드는 없다. 임시 Classifier는 저장하지 않는다.

이번 Fine-tuning에는 Center loss·Minutiae map annotation·Map MSE·Genuine/Impostor pair loss를 사용하지 않는다. Pretrained branch가 학습한 Feature를 Finger classification으로 조정한다. Inference에서는 분류 Head 없이 Feature 간 Cosine을 `[0,1]`로 정규화해 사용한다.

### Hyperparameters

설정은 현재 [trainConfig.h](trainConfig.h), [encoderConfig.h](encoderConfig.h), [augmentConfig.h](augmentConfig.h)에 있다. 현재 선언은 `inline constexpr` 또는 `inline const`다. Source 값을 수정한 뒤 다시 Build해야 반영된다.

| TrainConfig | 현재 값 | 의미 |
|---|---|---|
| `TrainablePrefixes` | `texture_projection.`, `minutia_projection.` | 두 마지막 Linear layer를 학습한다. |
| `Optimizer`, `LearningRate`, `WeightDecay` | Adam, 0.0001, 0.0001 | Optimizer와 업데이트·정규화 강도다. |
| `BatchSize`, `FeatureBatchSize`, `Epochs` | 32, 4, 20 | 학습 Batch·추출 Batch·반복 횟수다. |
| `Seed`, `CosineLogitScale` | 42, 16 | 초기화/Shuffle Seed와 Logit 배율이다. |
| `ValidationFirstCapture` | 7 | 해당 Capture ID부터 Validation으로 둔다. |
| `Momentum` | 0.9 | SGD를 선택했을 때만 사용한다. |
| `ReloadRtol`, `ReloadAtol`, `SelfScoreTolerance` | `1e-5`, `1e-6`, `1e-5` | Reload 출력과 Self comparison의 허용 오차다. |

기본 설정에서는 Frozen feature를 공유 Cache로 학습한다. Prefix를 다른 Layer로 확장하면 Full forward 경로를 사용한다. 해당 확장 설정은 실행 미확인이다. Optimizer/Classifier state를 저장하는 Resume 기능은 없다.

| EncoderConfig | 현재 값 | 의미 |
|---|---|---|
| `InputSize`, `GrayMaximum` | 299 [px], 255 [gray8] | 공통 Input과 Pixel normalization이다. |
| `EmbeddingSize`, `BranchEmbeddingSize` | 512, 256 [features] | 최종 Vector와 Branch Vector 크기다. |
| `FeatureSize`, `TextureFeatureSize` | 2,560, 1,536 [features] | Frozen feature Cache의 크기다. |
| `GaborRidgeWidth`, `NormEpsilon` | 1.8 [px], `1e-12` | Gabor 설정과 Zero-norm 검출 기준이다. |
| `Threads` | 4 [threads] | C++ CPU 연산 Thread 수다. CUDA/MPS는 사용하지 않는다. |
| `ExportAtol`, `ExportRtol` | `1e-5`, `1e-4` | Python/TorchScript 비교 허용 오차다. |
| `CenterCropDatabase` | `FVC2004_DB1_B` | 중앙 정사각형 Crop을 적용할 입력 DB다. |
| `Preprocessing`, `CompatibleArchivePreprocessing` | `…-v2`, `…-v1` | 저장할 전처리 식별자와 기존 Archive의 호환 식별자다. 실행 규칙은 입력 DB로 결정한다. |

Architecture·Preprocessing을 바꾸면 Checkpoint 호환성과 재변환 여부도 확인해야 한다. Config 이름만 바꿔 기존 Weight의 Network 구조가 바뀌는 것은 아니다.

### Augmentation

| AugmentConfig | 현재 값·적용 확률 |
|---|---|
| `CopiesPerOriginal`, `Seed`, `Version` | 12 [images/original], 42, Cache format 2 |
| `RotationDegrees`, `RotationProbability` | −15~+15 [deg], 0.9 |
| `ScaleMin/Max`, `ScaleProbability` | 0.90~1.10 [ratio], 0.9 |
| `Fvc2004ScaleMin/Max` | 1.00~1.30 [ratio]. 중앙 Crop 후 영역에서 Foreground 잘림 제한을 적용한다. |
| `NoiseStdMin/Max`, `NoiseProbability` | Gaussian sigma 1~5 [gray8], 0.7 |
| `BrightnessMin/Max`, `BrightnessProbability` | Pixel gain 0.90~1.10, 0.8 |
| `ForegroundThreshold`, `MaxClippedForeground` | 235 [gray8] 미만을 Foreground로 보고, 화면 밖으로 나가는 비율을 최대 0.05로 제한한다. |
| `GeometryAttempts` | 32회 재추첨 후 조건을 못 맞추면 Identity geometry를 사용한다. |
| `FolderPattern` | `crop_first_%1_per_image_seed%2`. 설정이 다르면 번호 Suffix를 붙인다. |
| `SourceGeometry` | `fvc2004-center-square-before-augmentation` |

FVC2004는 원본 640×480의 중앙 480×480을 먼저 Crop한 뒤 증강한다. 다른 DB는 기존 원본 영역에서 증강한다. 변형 적용 여부는 독립적으로 추첨한다. Rotation·Scale을 합성한 뒤 Brightness gain·Gaussian noise·Gray8 clamp를 적용한다. Foreground 잘림 검사도 Crop 후 영역을 기준으로 한다. 검사는 Threshold 기반 근사이며 정밀 Segmentation은 아니다. FVC2004 증강 PNG는 이미 정사각형이므로 Encoder에서 추가로 잘리는 영역이 없다.

증강 결과는 `data/augmented/crop_first_12_per_image_seed42`처럼 설정을 알아볼 수 있는 폴더에 저장한다. 같은 설정과 원본으로 완성된 Cache만 Hash를 확인해 재사용한다. Manifest에는 설정, 원본·결과의 SHA-256, split, 실제 변형값과 Crop 위치·전후 크기를 기록한다. 설정이 달라지면 다른 번호의 폴더를 사용한다. 이 폴더는 Git에서 제외되며 추론 중에는 읽지 않는다.

### Local Resource / Results

| 항목 | 확인 내용 |
|---|---|
| 실행 환경 | 기록상 macOS arm64 CPU, LibTorch/PyTorch 2.15.0, Qt 5.15.17. Python 3.12.12에서도 Conversion·Reload를 확인했다. |
| GPU / CPU | GPU·CUDA·MPS 미사용, C++ Thread 4. 정확한 CPU model·실제 Core 사용률은 학습 Log에 없다. |
| Parameter / Cache | Exported encoder는 66,120,672 Parameters다. Feature Cache는 `2400×2560×4` = 24,576,000 [bytes], 약 23.44 MiB다. 이는 전체 Process RAM이 아니다. |
| Update 횟수 | Source 설정상 개별 모델은 Epoch당 25 Batch × 20 = 500 updates, 통합은 74 × 20 = 1,480 updates다. |
| Time / Peak memory | 총 Wall time·Peak RAM 측정 기록은 없다. Upstream A100 안내값을 이 실행 시간으로 사용하지 않는다. |
| 검증 | `models/verification.json`: 네 Archive Reload 일치, 2,048-byte Template, Self/Genuine/Impostor inference 확인. `models/implementation_check.json`: Frozen parameter 일치, 두 Projection 변경 확인. |

| 모델 | 마지막 Validation loss | Validation classification accuracy | Training log |
|---|---:|---:|---|
| FVC2000 | 0.100825 | 100% | `models/fvc2000_db3_training.csv` |
| FVC2002 | 0.255753 | 95% | `models/fvc2002_db3_training.csv` |
| FVC2004 | 0.506177 | 95% | `models/fvc2004_db1_training.csv` |
| Combined | 0.247110 | 98.3333% | `models/combined_training.csv` |

표의 수치는 이전 실행 기록이며, 작은 FVC_B Validation에서 임시 Classifier를 평가한 결과다. 독립 Finger/Sensor Test의 FAR·FRR·EER를 뜻하지 않는다. 저장된 모델의 파라미터가 서로 다른 Score 분포를 가질 수 있으며 Calibration은 미구현이다.

## API / Data Exchange

| 함수·위치 | 입력 | 출력·목적 |
|---|---|---|
| `LoadModel` / encoder.cpp | Model ID, Archive 경로 | `bool`, Error. Embedded format·Architecture·Preprocessing·512 dimensions·Model ID와 Probe output을 확인한다. |
| `Preprocess` / encoder.cpp | Model ID, tightly packed `AlgorithmImage`와 `database_id` | `PreparedImage`: 299×299 Float values. 입력 DB로 Crop/Padding을 결정한다. 실패하면 `false`와 Error를 준다. |
| `CreateTemplate` / encoder.cpp | Model ID, `PreparedImage` | Float32 512개의 Byte buffer. Size·Finite·Norm을 확인한다. |
| `Compare` / encoder.cpp | Model ID, Enrollment/Attempt Template | `double` Normalized similarity `[0,1]`, 성공 여부·Error. 높은 값이 더 유사하다. |
| `PrepareAugmentations` / augment.cpp | Original DB root, Cache root, 선택적 Manifest 경로 포인터 | `QVector<TrainingImage>`. 원본·증강본 경로와 Split을 돌려준다. 오류는 Exception으로 전달한다. |
| `main` / train.cpp | Original root, Pretrained archive, Output folder, 선택적 `--augment-only` | 준비 또는 네 모델 학습·저장·검증. 성공 0, 실패 1을 반환한다. |
| `setting` / prepare_deepprint.py | Config Header와 상수 이름 | Python에서 동일한 Encoder/Seed 설정을 읽는다. 단순 Literal 선언 형식을 전제로 한다. |
| `Encoder.features/embed_features/forward/preprocess` / prepare_deepprint.py | Image, Frozen feature 또는 Preprocessing input | Backbone Cache·Projection·전체 Encoder·Gabor 단계를 TorchScript method로 내보낸다. |

Engine은 Image buffer를 전달하고 Algorithm은 Model별 Template·Score·Error를 반환한다. GUI와 직접 통신하지 않는다. Offline trainer만 Engine을 호출해 같은 Inference 경로를 검사한다.

## Conversion / Offline Run

### 초기 Weight 준비

학습 초기값인 `models/pretrained/deepprint.bin`과 정보 파일 `deepprint.json`은 저장소에 포함한다. Git LFS를 설치한 뒤 Clone하면 받으며, 빠졌다면 Project root에서 `git lfs pull`을 실행한다. Clone한 사전학습 파일로 바로 Fine-tuning할 수 있으며 아래 Python 변환 과정은 필요하지 않다. C++ 프로그램은 Archive에 포함된 Encoder와 전처리를 사용한다. 이 초기 모델은 `model_id=pretrained`이므로 GUI의 비교용 모델 목록에는 해당하지 않는다.

아래 과정은 원본 checkpoint를 **직접 다시 변환할 때만** 사용한다. 직접 변환하려면 [공개 원본 Weight](https://drive.google.com/file/d/1R9v73hFBuy0iihz7v8eDlBcGrMDmQ9TH/view)를 내려받아 `models/pretrained/best_model.pyt`에 둔다. 위 Provenance 표의 SHA-256으로 파일을 구분할 수 있다. 원본 `.pyt`는 C++에서 직접 읽는 형식이 아니다.

Project root에서 Submodule을 받고 Patch를 **한 번만** 적용한다. 이미 적용한 작업 폴더라면 두 `apply` 명령은 생략한다.

```sh
git submodule update --init --recursive
git -C third_party/deepprint apply --check ../deepprint.patch
git -C third_party/deepprint apply ../deepprint.patch
```

Patch는 사용하지 않는 Display·Device import를 제거하고 상수 이미지의 정규화를 안전하게 처리한다. 고정된 Submodule commit은 유지하며, 이 로컬 수정을 외부 저장소에 Commit하지 않는다.

### macOS / Linux 변환

Python 3.12에서 확인한 변환 환경이다. 아래 명령은 Project root에서 실행한다. Linux에서의 실제 변환은 아직 확인하지 않았다.

```sh
python3.12 -m venv .venv-conversion
.venv-conversion/bin/python -m pip install torch==2.15.0 torchvision==0.29.2 numpy==2.5.5 pillow==13.0.0
.venv-conversion/bin/python tools/prepare_deepprint.py models/pretrained/best_model.pyt models/pretrained/deepprint.bin
```

### Windows 변환

Python 3.12를 설치한 뒤 PowerShell에서 실행한다. `best_model.pyt`의 위치와 Patch 적용 여부는 위와 같다. Windows 변환은 아직 실행 검증하지 않았다.

```powershell
py -3.12 -m venv .venv-conversion
& ".\.venv-conversion\Scripts\python.exe" -m pip install torch==2.15.0 torchvision==0.29.2 numpy==2.5.5 pillow==13.0.0
& ".\.venv-conversion\Scripts\python.exe" tools/prepare_deepprint.py models/pretrained/best_model.pyt models/pretrained/deepprint.bin
```

Converter는 `weights_only=True`로 읽고 Architecture를 Strict load한다. 원본·Wrapper 출력, Batch 1·2·4, 전처리, Gradient, Save/Reload가 맞는지 확인한다. 출력은 `metadata.json`을 포함한 TorchScript Archive이며 같은 이름의 `.json`도 저장한다. 출력 `.bin`이 이미 있으면 덮어쓰지 않고 중단하므로, 준비된 파일이 있다면 변환을 반복하지 않는다.

TorchScript는 Deprecated/Legacy API지만 현재 C++ 학습·재로딩 경로에서 동작을 확인했다. Python 3.14의 JIT 경고 때문에 변환 안내는 확인한 3.12 기준으로 작성했다. 변환한 PyTorch와 실행할 LibTorch의 호환성도 확인해야 한다.

### C++ 증강·학습·추론

Build 후 [Root 실행 순서](../../readme.md)를 따른다. 학습 입력은 Clone에 포함된 `models/pretrained/deepprint.bin`이다. 출력 폴더는 학습 명령이 자동 생성하며, 학습 완료 후 생성된 네 Fine-tuned 모델을 GUI에서 사용한다. 이 출력 폴더는 Clone에 포함되지 않는다.

| 실행 | 입력과 결과 |
|---|---|
| `FingerprintTrain data/original models/pretrained/deepprint.bin models/retrain_01 --augment-only` | 원본을 보존하면서 증강만 준비한다. |
| `FingerprintTrain data/original models/pretrained/deepprint.bin models/retrain_01` | 증강을 생성하거나 재사용한 뒤 네 모델을 독립 학습하고 재로딩·추론을 검증한다. |
| `FingerprintGUI` | 위 학습을 완료했다면 `models/retrain_01`을 선택한다. 다른 출력 경로를 사용했다면 실제 모델 폴더를 선택한다. |

위 표의 실행 파일은 macOS/Linux에서 `./build/`, Windows에서 `.\build-vs\Release\` 아래에 있다. Windows 파일명에는 `.exe`를 붙인다. 출력 폴더에 네 `.bin` 중 하나라도 있으면 기존 결과를 보호하기 위해 중단한다. 재학습은 새로운 폴더명으로 실행한다.

Fine-tuned Weight에는 Encoder·전처리와 Metadata가 들어가며 `.json`과 학습 CSV를 함께 저장한다. 임시 Classifier와 Optimizer state는 저장하지 않으므로 중간 학습을 이어가는 Resume 기능은 없다. Python은 최초 변환에만 필요하고, 증강·학습·추론은 C++로 실행한다.

## Windows / Visual Studio

Visual Studio에서 `build-vs/FingerprintEvaluator.sln`을 열고 **Release / x64**를 선택한다. `FingerprintTrain`을 Startup Project로 설정한 뒤 **Properties → Debugging**에서 다음 값을 지정한다.

| 항목 | 설정 |
|---|---|
| Working Directory | Clone한 Project root의 절대 경로 |
| Command Arguments | `"data\original" "models\pretrained\deepprint.bin" "models\retrain_vs_01"` |
| Environment | 설치한 Qt `bin`·LibTorch `lib`를 PATH에 추가하고 `QT_PLUGIN_PATH`를 Qt의 `plugins`로 지정 |
| 실행 | Ctrl+F5. 다음 학습에는 새 출력 폴더명을 사용 |

DLL과 TIFF plugin 준비는 [Root의 Windows Build](../../readme.md#windows--visual-studio)를 따른다. GUI만 실행하려면 Startup Project를 `FingerprintGUI`로 바꾼다.

## License / Status

Source License는 [LGPL-3.0](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/LICENSE), Inception Source는 [BSD-3-Clause](https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors/blob/7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13/flx/models/InceptionV4.py)다. 공개 Checkpoint의 별도 License는 확인되지 않았다. 외부 Library는 [Root License](../../readme.md#license--submodule)를 참고한다.

기본 Projection Fine-tuning·네 모델 Reload·Inference는 확인했다. 다른 Trainable layer 범위·SGD 학습·Windows 배포·새 GPU 실행은 미확인이다. Checkpoint 작성자의 정확한 Data manifest·학습 장비별 사용량은 제공 근거가 없어 미확인으로 남긴다.

새 실행의 Verification은 `self_score`, `genuine_score`, `impostor_score`와 `score_type`을 기록한다. 기존 Verification의 `*_cosine` 값은 과거 기록으로 보존한다. 이전 Root Weight 대신 새 Run 폴더에 재학습 결과를 보관한다. Fine-tuned Weight는 Git에서 제외하며, 초기 사전학습 Weight만 Git LFS로 관리한다.
