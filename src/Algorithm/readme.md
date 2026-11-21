# Algorithm 모듈

`FingerprintEncoder`가 TorchScript 모델을 로딩하고 전처리, Template 생성과 비교를 수행합니다. 학습·검증 도구와 `FingerprintAlgorithmCore`가 이 모듈을 사용합니다.

<img src="../../docs/diagrams/algorithm.png" alt="공개 KISA 계약에서 Core DLL과 FingerprintEncoder로 이어지는 알고리즘 구조" width="100%">

- `encoder.h/.cpp`: 모델을 실행하고 확률 Template을 직렬화합니다.
- `encoderConfig.h`: 입력 크기, 모델 차원, 전처리·Template 형식을 정의합니다.
- `Algorithm.h`: native encoder 인터페이스와 입력·metadata 자료형을 정의합니다.

512차원 embedding을 cosine classifier ×16과 Softmax에 통과시킨 30차원 확률 벡터를 사용합니다. 모델별 점수는 `(cosine + 1) / 2`입니다.

<details>
<summary>DeepPrint Inception·Reduction 블록 상세</summary>

<img src="../../docs/diagrams/deepprint_blocks.png" alt="DeepPrint의 Inception A·B·C와 Reduction A·B 내부 구조 및 블록 반복 방식" width="100%">

[블록 그림 SVG 원본](../../docs/diagrams/deepprint_blocks.svg)

</details>

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg) · [전처리·학습 설정](../../docs/baseline/training_configuration.md) · [DLL 구성](../Kisa/readme.md)
