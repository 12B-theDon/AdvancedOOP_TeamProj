# Training 모듈

모델 입출력은 Algorithm의 `LoadArchive()`·`SaveArchive()`를 공유하며 Windows 한글·공백 경로를 처리합니다. 학습 계산·seed·epoch·optimizer·전처리 설정은 유지합니다.

현재 Gaussian noise는 **적용 확률 70%·σ=[1,80]**입니다. 노이즈 상한 10개 수준을 비교하고, 원본 EER 유지 조건과 선택 후 확인 평가를 거쳐 모델 4개를 갱신했습니다. 회전·배율·밝기·crop·원본당 증강 12개와 학습 설정은 유지합니다. [비교 분석·그래프](../../docs/experiments/noise_augmentation.md)에 후보별·모델별 결과와 추가 seed 검증을 기록했습니다.

- `augment.cpp`: 원본에서 `data/augmented`를 생성합니다.
- `train.cpp`: 검증된 feature cache로 두 projection과 cosine classifier를 학습합니다.
- `validation.cpp`·`ValidationEngine`: 저장 모델, 고정 parameter·buffer, native 점수와 calibration을 검증합니다.

[후속 전수·Grouped CV 평가](../../docs/experiments/noise_followup.md)는 원본 240장·고유 기존/재생성 증강·별도 Test noise와 120개의 capture 보류 학습 bundle을 비교합니다. Noise FRR는 개선됐지만 모든 DB의 원본 성능을 보존하는 엄격한 조건은 통과하지 못해, 현재 production 모델·학습 코드는 유지했습니다.

배포 모델은 Git LFS로 받아 재사용합니다. 설치 스크립트는 모델이 없을 때만 자동 학습합니다. 직접 재학습할 때는 새 출력 폴더를 사용합니다.

```powershell
.\build\Release\FingerprintTrain.exe data\original models\pretrained\deepprint.bin models\trained_result_new
```

기존 배포 모델만 검증하려면 다음 명령을 사용합니다.

```powershell
.\build\Release\FingerprintTrain.exe data\original models\pretrained\deepprint.bin models\trained_result --verify-only
```

기존 가중치는 덮어쓰지 않으므로 학습에는 새 출력 폴더를 사용합니다. `--augment-only`는 증강만 실행하며, cache 재사용에는 원본·전처리·가중치의 일치가 필요합니다.

<img src="../../docs/diagrams/training.png" alt="데이터 증강·feature cache·모델 4개 학습·검증 및 export 흐름" width="100%">

<details>
<summary>DeepPrint 전처리·고정 Backbone·부분 학습 구조</summary>

<img src="../../docs/diagrams/deepprint_finetuning.png" alt="전처리에서 고정 Backbone과 학습 가능한 projection·cosine classifier를 거쳐 30차원 템플릿을 만드는 과정" width="100%">

[부분 학습 그림 SVG 원본](../../docs/diagrams/deepprint_finetuning.svg)

</details>

[구조도 SVG 원본](../../docs/diagrams/training.svg) · [학습 설정](../../docs/baseline/training_configuration.md) · [모델 파일](../../models/readme.md)
