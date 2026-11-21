# Algorithm

`FingerprintEncoder`가 TorchScript 모델 로딩, 전처리, 템플릿 생성과 비교를 담당합니다. 학습·검증과 `FingerprintAlgorithmCore`가 이 모듈을 사용합니다.

![공개 KISA 계약에서 Core DLL과 FingerprintEncoder로 이어지는 알고리즘 구조](../../docs/diagrams/algorithm.png)

- `encoder.h/.cpp`: 모델별 실행과 확률 템플릿 직렬화.
- `encoderConfig.h`: 입력 크기, 모델 차원, 전처리·템플릿 형식.
- `Algorithm.h`: native encoder 인터페이스와 입력·메타데이터 자료형.

512차원 embedding을 cosine classifier ×16과 Softmax에 통과시킨 30차원 확률 벡터를 사용합니다. 모델별 점수는 `(cosine + 1) / 2`입니다.

<details>
<summary>DeepPrint Inception·Reduction 블록 상세</summary>

![DeepPrint의 Inception A·B·C와 Reduction A·B 내부 구조 및 블록 반복 방식](../../docs/diagrams/deepprint_blocks.png)

[블록 그림 SVG 원본](../../docs/diagrams/deepprint_blocks.svg)

</details>

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg) · [전처리·학습 설정](../../docs/baseline/training_configuration.md) · [DLL 구성](../Kisa/readme.md)
