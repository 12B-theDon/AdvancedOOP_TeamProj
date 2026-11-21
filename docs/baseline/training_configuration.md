# 리팩터링 전 설정 기록

2026-11-21에 commit `ccfe7b5`에서 확인한 설정입니다. `implementation.json`은 소스와 당시 존재한 가중치의 SHA-256을, `original_data.json`은 원본 파일·디렉터리 전체를 기록합니다. 리팩터링 전 상태를 비교하는 baseline이며 새 학습 절차를 정의하는 문서는 아닙니다.

Windows 한글 경로 대응으로 모델 입출력 호출을 `LoadArchive()`·`SaveArchive()`로 교체했습니다. 보존 검사는 이 호출 표현만 이전 형태로 환원한 뒤 encoder·학습·검증 코드와 비교합니다. 아래 계산 설정과 원본 snapshot은 그대로 비교 기준으로 사용합니다.

- **Architecture**: pretrained `DeepPrint_TexMinu_512`을 사용합니다. upstream commit은 `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13`입니다. 공통 Inception stem에서 texture 1536·minutia 1024 features를 추출합니다. Linear projection은 각각 1536→256·1024→256이며, 각 branch의 L2 normalization 후 concatenate하고 다시 L2 normalization하여 512차원 embedding을 만듭니다.
- **Initialization**: 모델 4개가 각각 `models/pretrained/deepprint.bin`을 독립적으로 다시 로딩합니다. 앞에서 fine-tuning한 모델을 다음 모델의 초기값으로 사용하지 않습니다. CPU에서 Torch thread 4개로 실행합니다.
- **Frozen·Trainable**: stem, texture·minutia Backbone, Gabor와 모든 BatchNorm buffer는 고정합니다. dropout·BatchNorm을 포함한 encoder는 `eval` 상태를 유지합니다. `texture_projection.*`, `minutia_projection.*`, bias가 없는 512→30 cosine classifier만 학습합니다. classifier에는 hidden layer가 없습니다.
- **Inference preprocessing**: tightly packed gray8을 255로 나눕니다. FVC2004는 양옆 80 px를 center crop하여 480×480으로 만들고, 다른 DB는 `max(width,height)` 크기로 white padding합니다. antialiased bilinear resize로 299×299를 만들며 `align_corners=false`입니다. upstream Gabor ridge width는 1.8이고 inference에서는 augmentation을 적용하지 않습니다.
- **Offline augmentation v2**: 원본마다 12개를 생성하며 seed는 42입니다. FVC2004는 augmentation 전에 center crop합니다. rotation은 p=.9·±15°, scale은 p=.9·[0.90,1.10]이며 FVC2004는 [1.00,1.30]입니다. noise는 p=.7·sigma[1,5], brightness는 p=.8·gain[.90,1.10]입니다. foreground<235, 최대 clipped fraction .05를 사용하며 geometry를 32회 시도한 뒤 실패하면 identity transform을 적용합니다. 원본 파일은 덮어쓰지 않습니다.
- **Split**: augmentation 전에 원본 capture를 나눕니다. capture 1–6은 원본과 각각의 augmentation 12개를 학습에 사용하고, capture 7–8은 validation 원본만 사용합니다. DB 3개×identity 10개를 global label 30개 순서로 관리합니다. DB별 모델은 train 780·validation 20개, combined는 2340·60개입니다. 모델 ID는 `fvc2000_db3`, `fvc2002_db3`, `fvc2004_db1`, `combined`입니다.
- **Loss·Export**: normalized embedding과 normalized classifier weight의 내적에 16을 곱한 뒤 cross entropy를 계산합니다. export에는 classifier와 Softmax30을 유지합니다. Template은 float32 확률 30개와 producer·label·preprocessing metadata(v2)를 저장합니다.
- **Optimizer**: Adam, lr=.0001, weight_decay=.0001, 기본 betas·epsilon을 사용합니다. batch는 32, frozen feature extraction batch는 4, epochs는 40, seed는 42입니다. classifier 초기화 seed는 42이며, shuffle RNG를 42로 다시 설정하고 원래 active-class head의 초기화에 해당하는 난수를 소비한 뒤 `randperm` 순서 40개를 미리 만듭니다. 이 난수 소비 순서를 보존합니다. scheduler·early stopping·best-epoch 교체 없이 final epoch를 export합니다.
- **Similarity**: 확률 벡터의 cosine을 double precision으로 계산하고 [0,1]로 clamp한 뒤 `(cos+1)/2`로 변환합니다. matching-DB·combined 중 성공한 점수의 최솟값으로 판정하고, 알 수 없는 DB는 combined를 사용합니다. 다른 DB 모델의 점수는 진단용입니다. Genuine·Impostor에 같은 규칙을 적용합니다.
- **Training validation**: 각 DB에서 capture 1–6을 gallery, 7–8을 probe로 사용하여 DB마다 1200쌍을 비교합니다. 학습과 알려진 identity를 공유하므로 독립 test identity는 아닙니다. calibration target은 FAR 1%이고 소수점 4자리에서 올림합니다. calibration 파일은 학습 출력에 유지하며 Evaluator의 기본 threshold는 합의된 0.5로 설정합니다.

리팩터링 전에는 pretrained encoder만 있었으며 production `.bin` 4개는 없었습니다. 새로 학습한 파일은 이 과거 baseline을 대체하지 않습니다. `inference.json`은 각 DB의 `101_1`에 대한 실제 preprocessing과 pretrained 512차원 embedding hash를 기록합니다. 리팩터링 전 production 점수·가중치의 회귀 검증에는 당시 가중치 4개가 필요합니다.
