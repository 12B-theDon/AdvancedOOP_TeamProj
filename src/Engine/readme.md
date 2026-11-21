# Engine 모듈

비교 계획, Template cache, 병렬 실행과 원시 결과 저장을 담당합니다. `Algorithm` 인터페이스로 원본 gray8과 opaque Template bytes를 주고받으며, GUI가 소유한 backend를 빌려 사용합니다.

<img src="../../docs/diagrams/engine.png" alt="평가 Engine의 인터페이스·템플릿 캐시·DLL adapter와 worker 연결" width="100%">

- `PlanComparisons()`: 전체·첫 capture·capture 번호·난수·CSV로 비교 쌍을 선택합니다. 동일한 픽셀의 이미지는 제외하고 seed 재현성과 CSV 순서를 유지합니다.
- `EnrollImages()`·`Execute()`: 등록·비교를 실행합니다. 실행별 attempt cache와 원래 결과 순서를 유지하며 Stop 시 결과를 버립니다.
- `SaveRawResults()`: 실제 길이의 Template과 점수·오류·시간을 저장합니다. `ExportControl`로 행·파일 단위 진행률과 취소를 GUI 보고서 저장에 연결합니다. DLL을 교체하면 이전 Template과 결과를 무효화합니다.

`kisaDll.h/.cpp`의 `KisaDllAlgorithm`이 [Windows worker](../Worker/readme.md)를 관리합니다. Compare 시간은 worker의 DLL 호출 구간이며, 유효한 응답이 없으면 경과 시간을 사용합니다. `model_scores.csv`의 attempt 시간은 캐시를 공유하는 행에 반복 기록됩니다.

`Load()`는 설정한 모든 Worker를 시작하고 DLL·KISA exports의 handshake를 확인합니다. inference는 호출하지 않으며, 한 Worker라도 실패하면 앞서 시작한 Worker까지 종료합니다. GUI는 사용자가 Load를 누른 뒤에만 이 함수를 호출합니다. 실행 중 장애가 난 Worker는 다음 호출에서 재시작합니다.

Template의 `time_ms`는 `Extract()` 요청 전체의 경과 시간입니다. DLL 내부 모델 초기화와 요청 중 필요한 Worker 재시작을 포함하며 GUI의 `Load time (ms)`와 `enrollments.csv`에 같은 값을 표시합니다.

`Cancel()`은 새 작업을 막고 전체 Worker의 종료를 요청합니다. `CancellationFinished()`로 handle 정리까지 끝났는지 확인합니다. `ResetCancel()`은 이전 호출을 모두 join한 뒤 사용하며, 취소된 Worker 정리가 완료될 때까지 기다립니다. 정상 실행 사이에는 준비된 Worker를 재사용합니다.

[구조도 SVG 원본](../../docs/diagrams/engine.svg) · [GUI·지표](../GUI/readme.md)
