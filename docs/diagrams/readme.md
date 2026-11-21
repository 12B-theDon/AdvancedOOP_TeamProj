# 다이어그램 안내

SVG는 편집 원본, PNG는 README 표시용입니다. 본문 그림은 본문 폭에 맞춰 aspect ratio를 유지합니다. 아래 thumbnail을 누르면 SVG 원본을 엽니다. 실선은 호출·데이터 흐름이며, 점선은 선택한 직접 `#include` 관계입니다.

| 그림 | 내용 | 모듈 안내 |
|---|---|---|
| <a href="overview.svg"><img src="overview.png" width="220" alt="전체 평가·학습 구조"></a><br>Overview | 평가·worker·DLL·학습 경계 | [소스 안내](../../src/readme.md) · [빌드](../build.md) |
| <a href="algorithm.svg"><img src="algorithm.png" width="220" alt="알고리즘 DLL 구조"></a><br>Algorithm | 공개 계약, wrapper·Core·encoder | [Public](../../src/Public/readme.md) · [Kisa](../../src/Kisa/readme.md) · [Algorithm](../../src/Algorithm/readme.md) |
| <a href="engine.svg"><img src="engine.png" width="220" alt="평가 Engine 구조"></a><br>Engine | 인터페이스·cache·DLL adapter | [Engine](../../src/Engine/readme.md) |
| <a href="gui.svg"><img src="gui.png" width="220" alt="GUI·보고서 구조"></a><br>GUI | 소유 관계·백그라운드 실행·보고서 | [GUI](../../src/GUI/readme.md) |
| <a href="training.svg"><img src="training.png" width="220" alt="증강·학습·검증 구조"></a><br>Training | 증강·학습·검증 | [Training](../../src/Training/readme.md) |
| <a href="worker.svg"><img src="worker.png" width="220" alt="Windows worker 구조"></a><br>Worker | IPC·버퍼·timeout·재시작 | [Worker](../../src/Worker/readme.md) |
| <a href="deepprint_finetuning.svg"><img src="deepprint_finetuning.png" width="220" alt="DeepPrint 부분 학습 구조"></a><br>Fine-tuning | Backbone·projection·cosine classifier | [Training](../../src/Training/readme.md) · [학습 설정](../baseline/training_configuration.md) |
| <a href="deepprint_blocks.svg"><img src="deepprint_blocks.png" width="220" alt="Inception·Reduction 내부 구조"></a><br>Blocks | Inception·Reduction 내부 경로 | [Algorithm](../../src/Algorithm/readme.md) · [upstream 소스](../../third_party/deepprint/flx/models/InceptionV4.py) |

`source_manifest.json`은 기준 소스의 SHA-256을 기록합니다. [스타일](../diagram_style.md) · [검증 상태](../verification.md)
