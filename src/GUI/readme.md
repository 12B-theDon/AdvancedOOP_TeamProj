# GUI·보고서

`startup.cpp`가 Windows 실행 시 이 프로젝트의 배포 DLL·모델 4개·Worker를 확인합니다. 소스 저장소에서 누락이 발견되면 준비 프로그램이 LFS 다운로드를 우선 시도하고, 필요한 경우 학습·패키징을 마친 뒤 GUI를 다시 엽니다. 완성된 배포본과 이 프로젝트의 알고리즘을 포함하지 않은 외부 DLL 전용 평가기는 바로 실행됩니다.

`Window`는 7단계 QWizard와 DLL adapter를 소유합니다. Step 3에서 DLL·Worker·timeout을 설정하고 Template을 생성합니다. background 실행 중 진행률과 Stop을 제공하며, DLL을 교체하면 cache를 초기화합니다.

Stop은 전체 Worker를 종료하고 생성 중인 Template·비교 결과를 버립니다. GUI는 정리 상태를 표시하며 background 작업과 Worker의 handle 정리가 모두 끝난 뒤 다시 조작할 수 있습니다. 취소된 실행은 CSV·보고서로 저장하지 않습니다. 실행 중 메인 창을 닫으면 종료 여부를 확인하고, 승인 시 같은 취소·정리 절차가 끝난 뒤 창을 닫습니다. 종료 확인 중 작업이 먼저 끝나도 사용자의 선택을 기다립니다.

<img src="../../docs/diagrams/gui.png" alt="GUI의 화면 구성·백그라운드 평가·결과 및 보고서 생성 관계" width="100%">

Results에서 임계값(초기 0.5)을 바꾸면 저장된 점수로 다시 판정합니다. Save는 원시 CSV, 선택 지표의 HTML·Word 호환 RTF, 별도 SVG 그래프를 저장합니다.

| 지표 | 계산 기준 |
|---|---|
| FTE | 전체 등록 시도 중 실패 비율. 실패 유형별 횟수 포함 |
| REJ | 등록 실패로 막힌 쌍을 제외한 matching 시도 중 실패 비율 |
| FMR / FNMR | 사전 정의된 쌍이 분모. FNMR에는 등록·matching 실패 포함 |
| EER / EER* | 과제의 작은 midpoint 추정값과 구간. EER*는 성공 점수만 사용 |
| ZeroFMR / ZeroFNMR | FMR=0에서 최소 FNMR / FNMR=0에서 최소 FMR |
| FMR100 / FMR1000 | FMR≤1% / 0.1%에서 최소 FNMR |
| 시간 / 크기 | 실제 Compare 호출 평균, 성공 템플릿의 평균·최대 byte 수. attempt 시간은 CSV에 별도 기록 |

미실행 그룹의 지표는 N/A입니다. `report.cpp`가 지표와 보고서를 생성합니다.

[구조도 SVG 원본](../../docs/diagrams/gui.svg) · [사용 순서](../../readme.md#2-gui) · [검증 상태](../../docs/verification.md)
