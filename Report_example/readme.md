# 이전 버전의 실제 리포트 예시

2026-11-18에 기존 **512차원 특징 비교 방식**으로 생성한 GUI 리포트입니다. 현재의 30차원 확률 템플릿과 파일 형식·임계값·성능을 구분해 참고합니다. 원시 CSV·HTML·그래프·미리보기는 당시 기록을 그대로 보존합니다.

| 항목 | 당시 실행 조건·결과 |
|---|---|
| DB / 등록 이미지 | FVC2000_DB3_B / `102_6.tif` |
| 모델 | `fvc2000_db3`, 기존 `models/retrain_20261117_235258` 실행 |
| 모델 SHA-256 | `26901a10a1b172874b36656b859d4fc1c6be38100c290a20301a1b9d577227e5` |
| 비교 쌍 | Genuine 7쌍, Impostor 72쌍. 등록 이미지 자신은 제외 |
| Genuine / Impostor 임계값 | 0.90 / 0.80 |
| 판정 | PASS 7, MISS 0, REJ 66, FA 6, ERR 0 |
| FMR / FNMR / 관측 EER | 8.33333% / 0% / 0% |
| 선택 지표 | FTE, FMR, FNMR, EER, Matching time |
| 템플릿 | `templates/*.bin`, 512개 Float32 값으로 구성된 2,048 bytes의 특징 벡터 |

이 `.bin` 템플릿은 모델 Weight가 아닙니다. 현재 버전은 식별 정보를 포함하는 30차원 `.fpt`를 사용하며, 이 예시의 구형 템플릿을 현재 모델에 입력할 수 없습니다.

[HTML 리포트](report.html) · [미리보기](report_preview.png) · [비교 결과 CSV](comparisons.csv)

미리보기는 실제 HTML을 Qt로 렌더링한 이미지입니다. HTML에는 선택한 지표를, CSV에는 전체 지표와 모델별 점수를 저장했습니다. 처리 시간은 이 실행 환경의 측정값입니다.

당시 상대 경로는 원시 파일에 그대로 남겼습니다. 해당 모델의 현재 로컬 위치는 `models/baselines/square_padding`이며 새 Clone에는 포함되지 않습니다. 폴더 정리로 원시 기록의 경로·수치·판정을 다시 쓰지는 않았습니다.

같은 손가락의 촬영본을 Train/Validation으로 나눈 평가이며 독립 Test 성능이 아닙니다. 현재 모델의 사용법과 평가 결과는 [Root README](../readme.md)와 [Algorithm README](../src/Algorithm/readme.md#final-production-run--calibration)를 참고합니다.
