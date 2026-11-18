# 모델 보관 안내

GUI에서 사용할 모델은 **`current/`**, 재학습의 초기값은 **`pretrained/deepprint.bin`**에 있습니다. 나머지는 이전 모델과 실험·검증 결과를 보관하는 폴더입니다. 폴더 이름은 용도를 나타내며, 학습 설정과 평가 결과는 각 실행의 JSON·CSV에서 확인할 수 있습니다.

`validation/`은 코드 변경 전후를 비교한 로컬 실행 기록이며, 데이터셋의 Train/Validation 분할 폴더가 아닙니다. 이 README와 사전학습 파일을 제외한 모델·실험 폴더는 Git에서 제외합니다.

## 폴더 구성

| 경로 | 용도 |
|---|---|
| `current/` | 현재 GUI에서 사용하는 네 모델과 추천 임계값, 학습·평가 기록. Partial FT + Cosine Classifier + 30차원 Softmax 출력입니다. |
| `pretrained/` | 공개 사전학습 체크포인트와 C++ 학습 초기 파일. GUI 비교용 모델이 아닙니다. |
| `baselines/` | 전처리 변경 단계별로 보존한 구형 512차원 모델. 현재 방식과 비교하기 위한 자료입니다. |
| `experiments/` | 전처리, Adapter·LoRA, MLP, 출력 크기, 오류 분석 등 개발 중 수행한 실험. |
| `validation/` | 코드 정리 후 독립적으로 재학습하고 출력·성능이 유지되는지 확인한 결과. |
| `archive/initial_metadata/` | 이전에 `models` 바로 아래 있던 학습 정보·CSV·전처리 점검 자료. 완전한 모델 세트는 아닙니다. |

### 이전 모델과 검증 결과

| 경로 | 내용 |
|---|---|
| `baselines/square_padding/` | 흰색 정사각형 패딩을 사용한 이전 512차원 모델. |
| `baselines/center_crop_after_augmentation/` | 증강 후 FVC2004 중앙 Crop을 적용했던 단계의 512차원 모델. |
| `baselines/center_crop_before_augmentation/` | FVC2004를 먼저 중앙 Crop한 뒤 증강한 512차원 기준 모델. 현재 30차원 방식과 비교할 때 사용합니다. |
| `validation/code_cleanup/` | 이전 코드 정리 후 재학습·회귀 검증 기록. |
| `validation/oop_refactor/` | 수업 개념에 맞춘 소유권·헤더·제어 흐름 정리 후 네 모델을 재학습한 기록. `oop_review.json`에 비교 결과가 있습니다. |
| `validation/directory_layout/` | 이번 폴더 정리 전후의 전체 파일 해시, 빌드·GUI 경로·등록·비교·리포트 저장 검증 기록. |

### 실험 자료

| `experiments/` 아래 경로 | 내용 |
|---|---|
| `preprocessing/geometry_comparison/` | 이미지 크기·비율과 전처리 방식 비교. |
| `preprocessing/crop_before_augmentation/` | 중앙 Crop을 증강 전에 적용한 결과 비교. |
| `parameter_efficient_tuning/` | Partial FT·Adapter·LoRA 비교. 검증된 `features.json`·`features.f32` 캐시도 여기에 있습니다. |
| `adapter_comparison/` | Adapter와 Partial FT의 추가 비교. |
| `error_analysis/` | 이전 모델의 Genuine·Impostor 오류와 임계값 분석. |
| `mlp_3layer/` | 3-layer MLP와 Cosine 방식 비교. |
| `mlp_depth/` | MLP 깊이와 학습률·Seed별 비교. |
| `output_dimensions/` | 모든 모델의 출력을 30차원으로 통일하고 Classifier 유지 여부 등을 비교한 실험. |
| `source_archive/` | 정리 이전의 소스·문서·빌드 스냅샷. 운영 코드에서 호출하지 않습니다. |

각 실험 내부의 학습률·Seed별 폴더명은 비교 조건을 구분하기 위해 유지합니다. 과거 실험의 설정과 결과는 현재 운영 모델의 설정과 다를 수 있습니다.

## GUI에서 사용할 때

프로젝트 루트 기준으로 GUI의 모델 폴더를 `models/current`로 선택합니다. 네 `.bin`과 해당 JSON, `calibration.json`은 함께 보관합니다.

```text
fvc2000_db3.bin
fvc2002_db3.bin
fvc2004_db1.bin
combined.bin
calibration.json
```

모델 파일명과 내부 Model ID는 바꾸지 않았습니다. 폴더 정리 때 파일 내용은 변경하지 않았으므로, 같은 Weight로 만든 Template의 파일 형식과 모델 식별 정보는 유지됩니다. GUI에서 모델을 다시 불러오면 기존 등록 상태를 초기화하므로 화면에서는 Template을 다시 생성합니다. 다른 Weight나 구형 512차원 Template을 섞어 사용할 수 있다는 뜻은 아닙니다.

## 새로 학습할 때

기존 모델 폴더를 덮어쓰지 않고 새 출력 폴더를 지정합니다. 예를 들어 다음 명령은 `models/run_01`에 네 모델을 학습합니다. 이미 존재하는 실행 폴더라면 다른 이름을 사용합니다.

```sh
./build/FingerprintTrain data/original models/pretrained/deepprint.bin models/run_01
```

현재 로컬에 보존된 캐시와 512차원 기준 모델을 함께 사용하려면 다음과 같이 실행할 수 있습니다. 캐시의 원본·전처리·사전학습 Weight가 일치하는지는 프로그램에서 검사합니다.

```sh
./build/FingerprintTrain data/original models/pretrained/deepprint.bin models/run_02 --feature-cache models/experiments/parameter_efficient_tuning --baseline models/baselines/center_crop_before_augmentation
```

학습 설정과 검증 범위는 [Algorithm README](../src/Algorithm/readme.md), GUI 조작은 [GUI README](../src/GUI/readme.md)를 참고합니다.

## 이전 경로와의 대응

| 이전 폴더명 | 현재 경로 (`models/` 기준) |
|---|---|
| `production_cosine30_20261118` | `current/` |
| `code_cleanup_validation_20261118` | `validation/code_cleanup/` |
| `oop_course_validation_20261118` | `validation/oop_refactor/` |
| `retrain_20261117_235258` | `baselines/square_padding/` |
| `retrain_20261118_fvc2004_crop` | `baselines/center_crop_after_augmentation/` |
| `retrain_20261118_crop_before_augment` | `baselines/center_crop_before_augmentation/` |
| `experiments/peft_20261118` | `experiments/parameter_efficient_tuning/` |
| `experiments/adapter_review_20261118` | `experiments/adapter_comparison/` |
| `experiments/current_error_audit_20261118` | `experiments/error_analysis/` |
| `experiments/mlp3_id_20261118` | `experiments/mlp_3layer/` |
| `experiments/mlp_depths_20261118` | `experiments/mlp_depth/` |
| `experiments/mlp_uniform30_20261118` | `experiments/output_dimensions/` |
| `experiments/preprocessing_20261118` | `experiments/preprocessing/` |
| `experiments/source_archive_20261118` | `experiments/source_archive/` |

과거 로그·리포트·소스 스냅샷은 실행 당시의 기록이므로 내용에 이전 경로가 남아 있을 수 있습니다. 다시 실행할 때는 위 표의 현재 경로로 바꿔 사용합니다. 운영 코드와 문서의 실행 명령은 현재 경로를 사용합니다. 문서용 캡처 도구는 정리했으며, 기존 화면과 결과는 보존합니다.

## Git 관리

이 README와 `pretrained/deepprint.bin`·`deepprint.json`만 저장소에 포함합니다. 사전학습 `.bin`은 기존 Git LFS 정책을 유지합니다. `current`의 학습 Weight, 이전 모델, 캐시, 실험·검증 결과와 원본 `best_model.pyt`는 로컬에 보존하고 Git에서 제외합니다. 따라서 새 Clone에는 `current/`가 없으며 직접 학습하거나 별도로 받은 모델 폴더를 선택해야 합니다.
