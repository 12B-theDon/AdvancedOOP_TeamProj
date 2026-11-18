# 모델 준비와 실험 기록

배포에는 재학습의 초기값인 **`pretrained/deepprint.bin`**을 포함합니다. GUI용 모델은 직접 학습해 준비하고, GUI Step 3에서 학습이 완료된 출력 폴더를 선택합니다. 특정 폴더를 최신 모델로 자동 선택하지 않습니다.

## 보관 구조

| 경로 | 내용 |
|---|---|
| `pretrained/` | C++ 학습 초기 파일과 메타데이터. 로컬 원본 체크포인트 `best_model.pyt`는 선택적인 재변환용이며 Git에서 제외합니다. |
| `validation/` | 실험 설정·학습 로그·평가·임계값·회귀 검증 기록. 데이터셋의 Train/Validation 분할 폴더가 아닙니다. |
| `validation/runs/cosine30_seed42_20epoch/` | 이전 `current`의 20 Epoch 실행 기록. |
| `validation/runs/cosine30_seed42_40epoch/` | 이전 `run_check_20261119_011503`의 40 Epoch 실행 기록. |
| `validation/baselines/` | 전처리 변경 단계별 512차원 모델의 설정과 학습 로그. |
| `validation/experiments/` | 전처리, Partial FT·Adapter·LoRA, MLP 깊이·출력 크기·분류기 유지 여부와 오류 분석 결과. |
| `validation/archive/` | 초기 메타데이터와 Weight·캐시 정리 내역. |
| `validation/code_cleanup`, `oop_refactor`, `directory_layout` | 기존 코드 정리·재학습·폴더 변경의 회귀 검증 기록. |

완료된 실행의 학습 Weight, 특징·배치 순서 캐시, 중간 배열과 실험 전용 코드·이전 소스 스냅샷은 정리했습니다. 설정·평가 CSV·JSON·HTML·그래프와 기존 검증 화면은 보존했습니다. 기록에 남은 Weight 해시와 캐시 메타데이터는 실행 당시의 정보이며, 파일이 현재 존재한다는 뜻은 아닙니다. 삭제한 모델의 추론을 다시 확인하려면 재학습해야 합니다.

## 학습과 GUI 실행

프로젝트 루트에서 실행합니다. 기본 학습 설정은 Adam, LR·Weight decay 각각 0.0001, Batch 32, **40 Epoch, Seed 42**입니다. 네 모델은 같은 사전학습 초기값에서 각각 독립적으로 학습하며, 기존 Weight가 있는 출력 폴더는 덮어쓰지 않습니다.

```sh
./build/FingerprintTrain data/original models/pretrained/deepprint.bin models/run_01
./build/FingerprintGUI
```

GUI에서 **Select weights folder...**를 눌러 `models/run_01`을 선택합니다. 새로 학습한 폴더에는 다음 네 모델과 대응하는 JSON, `calibration.json`이 함께 있어야 합니다.

```text
fvc2000_db3.bin
fvc2002_db3.bin
fvc2004_db1.bin
combined.bin
calibration.json
```

`validation`은 결과 기록만 보관하는 곳이므로 GUI 모델 폴더로 선택하지 않습니다. 모델 파일과 그 모델의 추천 임계값은 항상 같은 실행의 파일을 사용합니다. 현재 보정은 최솟값 집계의 `fingerprint-calibration-v3`이며, 이전 최댓값용 v2 파일은 `--verify-only`로 다시 생성합니다. 다른 Weight를 불러오면 등록 템플릿을 다시 생성합니다.

새 학습에서는 기본적으로 `<출력 폴더>/feature_cache`를 생성합니다. 이후 같은 입력·전처리·사전학습 파일의 캐시가 남아 있다면 `--feature-cache`로 재사용할 수 있습니다. 배포나 기존 결과 기록에 특징 캐시가 있다는 가정은 하지 않습니다.

[학습·검증 상세](../src/Algorithm/readme.md) · [GUI 사용법](../src/GUI/readme.md)

## 기록 정리와 해석

2026-11-19에 완료된 실험 체크포인트 969개와 이전·검증 모델 20개를 먼저 정리했습니다. 이후 배포 시 직접 학습하는 흐름에 맞춰 20·40 Epoch 실행의 Weight 8개와 불필요한 캐시·실험 코드를 추가로 정리하고, 기록을 `validation` 아래로 모았습니다. 세부 파일 목록·크기·SHA-256·경로 이동 대응은 `validation/archive/weight_cleanup.json`과 `model_artifact_cleanup.json`에 남깁니다.

과거 보고서와 로그의 경로·실행 설정은 당시 기록으로 유지합니다. 예전 코드 스냅샷·Weight·캐시를 가리키는 경로는 재실행 명령으로 사용하지 않습니다. 실험별 하위 폴더와 원본 결과 파일은 유지해 같은 이름의 `summary.csv`, `metrics.csv`가 섞이지 않도록 했습니다.

20 Epoch 저장 모델의 단일 Seed 결과, 40 Epoch 저장 모델의 단일 Seed 결과, 여러 Seed의 평균을 구분합니다. 현재 평가는 학습에 등장한 손가락의 다른 촬영본에 대한 결과이며, 독립 Test나 미학습 손가락 성능을 보장하지 않습니다. 추천 임계값 역시 해당 실행의 검증 점수로 산정한 값입니다.

## Git 관리

이 README와 `pretrained/deepprint.bin`·`deepprint.json`만 저장소에 포함합니다. 사전학습 `.bin`의 기존 Git LFS 정책은 유지합니다. 직접 생성한 모델·특징 캐시·검증 기록과 원본 `best_model.pyt`는 Git에서 제외합니다. 로컬 결과 정리는 Git 이력이나 LFS 설정을 변경하지 않습니다.
