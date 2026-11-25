# Algorithm 모듈

`FingerprintEncoder`가 TorchScript 모델을 로딩하고 전처리, Template 생성과 비교를 수행합니다. 학습·검증 도구와 `FingerprintAlgorithmCore`가 이 모듈을 사용합니다.

<img src="../../docs/diagrams/algorithm.png" alt="공개 KISA 계약에서 Core DLL과 FingerprintEncoder로 이어지는 알고리즘 구조" width="100%">

- `encoder.h/.cpp`: 모델 실행, 확률 Template 저장, 모델 파일의 검사값 계산을 담당합니다.
- `archive.h/.cpp`: Qt `QFile`·`QSaveFile`로 한글·공백 경로를 열고 LibTorch reader·writer API에 연결합니다. 파일을 한꺼번에 메모리 복사하지 않고 읽으며 저장은 commit 후 교체합니다. Core·학습·검증이 같은 입출력을 사용합니다.
- `encoderConfig.h`: 입력 크기, 모델 차원, 전처리·Template 형식을 정의합니다.
- `Algorithm.h`: native encoder 인터페이스와 입력·metadata 자료형을 정의합니다.

512차원 embedding을 cosine classifier ×16과 Softmax에 통과시킨 30차원 확률 벡터를 사용합니다. 모델별 점수는 `(cosine + 1) / 2`입니다.

**SHA-256**은 파일 내용을 64자리 16진수 검사값으로 요약하는 방법입니다. 같은 내용에서는 같은 검사값이 나옵니다. 모델 파일에서 계산한 값은 Template에도 저장됩니다. 비교할 때 저장된 값과 현재 모델의 값이 다르면 해당 모델의 Template을 거부하므로, 모델 교체 후에는 Template을 다시 생성해야 합니다.

모델 파일은 Qt `QFile`로 나누어 읽고, Windows에 기본으로 포함된 **BCrypt**로 SHA-256을 계산합니다. 첫 호출에서 파일 검사에 걸리는 시간을 줄이기 위한 변경이며, 별도 프로그램 설치는 필요하지 않습니다. 학습된 가중치·전처리·유사도 계산 방식은 그대로입니다.

<details>
<summary>DeepPrint Inception·Reduction 블록 상세</summary>

<img src="../../docs/diagrams/deepprint_blocks.png" alt="DeepPrint의 Inception A·B·C와 Reduction A·B 내부 구조 및 블록 반복 방식" width="100%">

[블록 그림 SVG 원본](../../docs/diagrams/deepprint_blocks.svg)

</details>

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg) · [전처리·학습 설정](../../docs/baseline/training_configuration.md) · [DLL 구성](../Kisa/readme.md)
