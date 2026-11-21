# GUI / Report

`startup.cpp`가 Windows 실행 시 자사 배포 DLL·모델 4개·Worker를 확인합니다. 소스 저장소에서 누락이 발견되면 준비 프로그램이 LFS 다운로드를 우선 시도하고, 필요한 경우 학습·패키징을 마친 뒤 GUI를 다시 엽니다. 완성된 배포본과 자사 알고리즘이 없는 외부 DLL 전용 평가기는 바로 실행됩니다.

`Window`는 7단계 QWizard와 DLL adapter를 소유합니다. Step 3에서 DLL·worker·timeout을 설정하고 템플릿을 생성합니다. 백그라운드 실행 중 진행률과 Stop을 제공하며, DLL 교체 시 캐시를 초기화합니다.

![GUI의 화면 구성·백그라운드 평가·결과 및 보고서 생성 관계](../../docs/diagrams/gui.png)

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
