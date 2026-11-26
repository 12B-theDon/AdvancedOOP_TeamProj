# Algorithm DLL 구성

- `FingerprintAlgorithm.dll` (`BioApi.cpp`): 공개 KISA 세 함수를 export하고 Core로 전달하는 static-CRT wrapper입니다.
- `FingerprintAlgorithmCore.dll` (`core.cpp`): 전처리·inference·Template 인코딩·점수 집계를 수행합니다. 요청의 `iType`에 해당하는 DB 모델과 `combined`만 DLL 위치에서 lazy loading하고, Process 안에서 재사용합니다. 다른 DB가 처음 요청되면 해당 DB 모델만 추가 로딩합니다.

<img src="../../docs/diagrams/algorithm.png" alt="공개 KISA 함수와 wrapper·Core DLL·encoder의 호출 구조" width="100%">

Core는 호환성을 확인한 matching-DB·combined 모델의 전처리·Backbone 추론을 공유하되 두 Template을 각각 생성하고, 비교 시 두 모델의 성공 점수 중 최솟값을 사용합니다. 기존과 같이 둘 중 하나만 성공하면 해당 모델을 사용하고, 둘 다 실패하면 오류를 반환합니다. Template에는 가중치·label·전처리 metadata가 포함됩니다. 기존 네 슬롯 형식을 유지하되 다른 DB 슬롯은 비워 두므로, 이전 버전에서 생성한 Template과도 비교할 수 있습니다.

모델 쌍의 공유 조건을 확인할 수 없으면 기존 개별 추론으로 처리합니다. 초기화의 SHA-256 중첩·병렬 로딩·실제 이미지 기반 검증은 기본값에서 비활성화합니다. 실험하려면 새 Worker 시작 전에 `FINGERPRINT_OVERLAP_HASH`·`FINGERPRINT_PARALLEL_LOAD`·`FINGERPRINT_REPRESENTATIVE_INIT` 중 필요한 환경 변수의 값을 `1`로 설정합니다.

배포 폴더에는 모델 4개를 유지합니다. 한 Worker에서 같은 DB를 반복 처리하면 모델 2개를 재사용하고, 세 DB를 모두 처리하면 최종적으로 4개가 로딩됩니다. 로딩 실패도 해당 Process 안에서 기록하므로, 누락·손상된 모델 파일을 교체했다면 Worker를 다시 시작해야 합니다. 첫 호출의 로딩·inference 준비 비용은 남아 있으며, 5초 이내 완료 여부는 Windows에서 별도로 측정해야 합니다.

raw cosine 점수의 최솟값을 결정한 뒤 공개 반환 점수를 소수 셋째 자리까지 반올림합니다. 예: `0.997061483 → 0.997`. 기존 가중치와 확률 Template 형식은 유지하며, Threshold는 Evaluator에서 조절합니다. 이 프로젝트의 기본값은 `0.30`입니다. 반올림·자릿수 검사 규칙은 `Public/Score.h`를 공유합니다.

오류 코드 1–7은 각각 null 인자, iType, 모델 로딩, 추출, 템플릿, 비교, Core 로딩 오류입니다. 공개 ABI와 버퍼 규칙은 [Public](../Public/readme.md), 배포 파일은 [패키징 안내](../../docs/build.md#package)에 정리했습니다.

[구조도 SVG 원본](../../docs/diagrams/algorithm.svg)
