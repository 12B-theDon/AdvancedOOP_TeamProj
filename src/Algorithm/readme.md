# Algorithm 모듈

`FingerprintEncoder`가 TorchScript 모델을 로딩하고 전처리, Template 생성과 비교를 수행합니다. 학습·검증 도구와 `FingerprintAlgorithmCore`가 이 모듈을 사용합니다.

<img src="../../docs/diagrams/algorithm.png" alt="공개 KISA 계약에서 Core DLL과 FingerprintEncoder로 이어지는 알고리즘 구조" width="100%">

- `encoder.h/.cpp`: 모델을 실행하고 확률 Template을 직렬화합니다. 파일 SHA-256은 Qt `QFile`로 1 MiB씩 읽어 Windows CNG `BCrypt`로 계산합니다. 기존과 같은 소문자 64자리 hash를 반환하며, 파일 읽기·CNG 오류 시 실패하고 handle을 정리합니다. 작은 label JSON의 SHA-256은 Qt를 유지합니다.
- `archive.h/.cpp`: Qt `QFile`·`QSaveFile`로 한글·공백 경로를 열고 LibTorch reader·writer API에 연결합니다. 파일을 한꺼번에 메모리 복사하지 않고 읽으며 저장은 commit 후 교체합니다. Core·학습·검증이 같은 입출력을 사용합니다.
- `encoderConfig.h`: 입력 크기, 모델 차원, 전처리·Template 형식을 정의합니다.
- `Algorithm.h`: native encoder 인터페이스와 입력·metadata 자료형을 정의합니다.

512차원 embedding을 cosine classifier ×16과 Softmax에 통과시킨 30차원 확률 벡터를 사용합니다. 모델별 점수는 `(cosine + 1) / 2`입니다.

`BCrypt`는 Windows 기본 API이며 `bcrypt`를 링크합니다. 별도 설치는 필요하지 않습니다. 파일 전체의 hash를 직접 계산하므로 manifest 값으로 대체하지 않습니다. 가중치·전처리·점수 계산은 유지하며, 첫 호출의 실제 시간은 Windows에서 측정해야 합니다.

<details>
<summary>DeepPrint Inception·Reduction 블록 상세</summary>

<img src="../../docs/diagrams/deepprint_blocks.png" alt="DeepPrint의 Inception A·B·C와 Reduction A·B 내부 구조 및 블록 반복 방식" width="100%">

[블록 그림 SVG 원본](../../docs/diagrams/deepprint_blocks.svg)

</details>

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg) · [전처리·학습 설정](../../docs/baseline/training_configuration.md) · [DLL 구성](../Kisa/readme.md)
