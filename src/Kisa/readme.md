# Algorithm DLL 구성

- `FingerprintAlgorithm.dll` (`BioApi.cpp`): 공개 KISA 세 함수를 export하고 Core로 전달하는 static-CRT wrapper입니다.
- `FingerprintAlgorithmCore.dll` (`core.cpp`): 전처리·inference·Template 인코딩·점수 집계를 수행합니다. DLL 위치에서 모델을 lazy loading하고 Process 안에서 재사용합니다.

<img src="../../docs/diagrams/algorithm.png" alt="공개 KISA 함수와 wrapper·Core DLL·encoder의 호출 구조" width="100%">

Core는 성공한 모델별 Template을 묶어 반환하고, 비교 시 matching-DB·combined 모델의 성공 점수 중 최솟값을 사용합니다. Template에는 가중치·label·전처리 metadata가 포함됩니다.

최솟값을 결정한 뒤 공개 반환 점수를 소수 셋째 자리까지 반올림합니다. 예: `0.997061483 → 0.997`. 모델 내부 점수 계산과 가중치는 유지하며, Threshold는 Evaluator에서 조절합니다. 반올림·자릿수 검사 규칙은 `Public/Score.h`를 공유합니다.

오류 코드 1–7은 각각 null 인자, iType, 모델 로딩, 추출, 템플릿, 비교, Core 로딩 오류입니다. 공개 ABI와 버퍼 규칙은 [Public](../Public/readme.md), 배포 파일은 [패키징 안내](../../docs/build.md#package)에 정리했습니다.

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg) · [검증 결과](../../docs/verification.md)
