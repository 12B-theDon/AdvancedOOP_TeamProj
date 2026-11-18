# GUI / Report

[Root](../../readme.md) · [Algorithm](../Algorithm/readme.md) · [Engine](../Engine/readme.md)

GUI는 DB와 등록 이미지를 선택하고 비교를 실행하는 화면이다. 추론과 원시 결과 저장은 Engine에 요청한다. 리포트는 화면 위젯 대신 `EvaluationRun`을 받아 지표·그래프·HTML을 만든다.

![GUI and report structure](../../docs/diagrams/gui.png)

[구조도 SVG](../../docs/diagrams/gui.svg)

등록 이미지마다 선택한 모델의 30차원 템플릿을 생성하고, 비교 점수와 판정을 표시한다. 리포트는 같은 실행 기록에서 계산하므로 화면과 저장 결과에 같은 기준을 적용한다.

## Responsibility / Files

화면과 리포트의 자료형·함수·설정값은 `gui` namespace에 둔다. `Window`와 지표·리포트 함수가 같은 범위를 사용하므로 별도 Report namespace는 없다. 리포트에는 실행 기록과 선택 지표를 직접 전달하며 별도 포장 구조는 사용하지 않는다. Engine 결과는 `engine::EvaluationRun`처럼 모듈을 명시해 참조한다.

| 파일 | 역할 |
|---|---|
| [GUI.h](GUI.h) | `Window : QWizard`, 비교 설정과 화면 상태 선언 |
| [GUI.cpp](GUI.cpp) | 7단계 화면, 이미지·모델 선택, 비교 실행, 결과 갱신과 저장 |
| [guiConfig.h](guiConfig.h) | 안내 문구, 화면 크기, 지표 선택과 표시 정밀도 |
| [report.h](report.h), [report.cpp](report.cpp) | 지표 계산, SVG 생성, CSV·HTML 저장 |
| [main.cpp](../main.cpp) | Encoder와 Window를 생성하고 연결하는 진입점 |

이미지 정보는 `engine::ImageInfo`로 한 번만 저장하고 등록·비교·CSV·리포트에서 공유한다. 화면 위젯은 명시적 타입으로 선언하며 이벤트 연결에 필요한 짧은 람다를 사용한다. Qt Widgets를 사용하며 별도의 차트 라이브러리 없이 SVG를 생성한다. 페이지는 Window를 부모로 생성하고, 레이아웃에 넣은 위젯은 Qt의 부모·자식 소유권으로 해제한다. 멤버 위젯 포인터는 소유하지 않는 참조이며 nullptr로 초기화한다. Window 복사는 금지한다. Qt 이벤트 연결의 짧은 람다는 Window를 연결 문맥으로 사용하므로 Window가 소멸하면 연결도 해제된다. 이미지 읽기에는 Qt TIFF 플러그인이 필요하다. 사용하는 라이브러리와 라이선스는 [Root README](../../readme.md#libraries--headers)에 정리했다.

## Pages / Actual Flow

| 단계 | 동작 |
|---|---|
| 1. DB | 제공 DB 세 개 중 하나를 선택하거나 외부 TIFF 폴더를 연다. 이미지 수와 해상도를 표시한다. |
| 2. Enrollment | 한 장 또는 여러 장을 선택한다. 여러 이미지는 Ctrl/Shift로 선택하며, 이미지마다 별도 템플릿을 만든다. |
| 3. Templates | 학습된 모델 폴더와 사용할 모델을 선택하고 `Create enrollment templates`를 누른다. 제공 DB는 대응 개별 모델, 외부 DB는 네 모델이 기본 선택된다. |
| 4. Comparison type | 같은 손가락인 Genuine, 다른 손가락인 Impostor, 또는 Both를 고른다. |
| 5. Comparison rules | 비교 대상을 고르는 규칙을 설정한다. Both는 두 종류를 각각 설정한다. |
| 6. Run | 비교 개수를 확인하고 `Run selected comparisons`로 실행한다. |
| 7. Results | 점수와 판정 기준을 확인한다. `Metrics and report` 탭에서 지표를 선택한 뒤 `Save all`로 저장한다. |

학습된 네 `.bin`과 `calibration.json`을 같은 폴더에 둔다. GUI의 초기 경로는 `models/current`이며, 다른 위치에 학습한 모델은 폴더 선택으로 불러온다. `models/run_01`은 학습 명령이 생성하는 출력 폴더의 예시다. Clone 직후에는 직접 학습하거나 별도로 받은 모델이 필요하며, `models/pretrained`는 GUI 비교용 폴더가 아니다. 구형 512차원 모델을 선택하면 재학습 안내를 표시한다.

사용자 안내에는 템플릿의 역할, 점수의 의미와 조절 방법만 간단히 설명한다. HTML의 자세한 실행 설정은 `Run settings`를 펼쳐 볼 수 있다. 모델 형식과 평가 근거는 [Algorithm README](../Algorithm/readme.md)에 정리하고, 실제 모델 정보는 원시 결과에도 보존한다.

### 이미지와 비교 대상

DB 폴더 바로 아래의 `.tif`·`.tiff`를 읽고 하위 폴더는 재귀 탐색하지 않는다. 파일 수는 TIFF 전체를 기준으로 표시하되, 선택 목록에는 읽을 수 있는 이미지만 넣는다. 파일명이 `finger_capture` 형식이면 손가락과 촬영 번호를 읽는다. 다른 파일명도 등록할 수 있지만 자동 Genuine/Impostor 후보에서는 제외하며, 이때는 CSV로 비교 쌍과 종류를 지정한다.

GUI는 입력 DB 정보를 Engine에 전달한다. FVC2004는 중앙을 자른 뒤 299×299로 줄이고, 다른 DB는 여백을 채운다. 외부 폴더도 알려진 DB 이름이면 해당 규칙을 적용하며, 출처가 불명확하면 여백을 채운다. 등록과 비교에 같은 함수를 사용하므로 사용자가 별도로 자를 필요는 없다. 모델이나 전처리가 바뀌면 템플릿을 다시 만든다.

비교 규칙은 전체 후보, 손가락당 한 장, 지정 촬영 번호, 등록 이미지당 무작위 개수, CSV 순서 중에서 선택한다. 같은 이미지와 동일 픽셀의 복제본은 제외한다. 제공 DB에서 한 장을 등록했을 때 Impostor 전체 후보는 72쌍, 손가락당 한 장은 9쌍이다.

CSV는 `enrollment,attempt`와 선택적인 `type` 열을 받으며, 파일명은 현재 DB에 있어야 한다. 따옴표 이스케이프·BOM·CRLF를 지원하지만 한 레코드는 한 줄이어야 한다. 저장한 `comparisons.csv`도 다시 입력할 수 있다. 잘못된 쌍의 처리 기준은 [Engine 비교 규칙](../Engine/readme.md#comparison-rules)을 따른다.

템플릿을 만들기 전에는 다음 단계로 진행할 수 없다. 규칙이 잘못되었거나 비교 쌍이 없으면 실행을 막는다. DB·등록 이미지·모델·비교 종류·규칙을 바꾸면 이전 비교 결과를 초기화한다. 실행하지 않은 종류는 저장하지 않으며, 두 종류가 필요한 지표는 결과가 부족하면 N/A로 표시한다.

## Result Labels / Thresholds

점수는 높을수록 두 지문이 비슷하다는 뜻이며 동일 지문일 확률은 아니다. 현재 모델은 30차원 Softmax 출력의 코사인 유사도를 `(cos+1)/2`로 바꿔 [0.5, 1] 점수를 만든다. 정답은 원본 손가락 ID 또는 CSV 종류로 결정한다.

모델 하나를 선택하면 `Model / Score`를, 여러 개를 선택하면 `Best model / Best score / Worst model / Worst score`를 표시한다. Best/Worst는 성공한 모델의 최고·최저 점수이며 판정과 지표에는 최고 점수를 사용한다. 성공한 모델이 하나면 Worst는 비워 둔다. 모델별 점수와 오류는 툴팁과 CSV에 남긴다.

| 종류 | 조건 | 표시 |
|---|---|---|
| Genuine | 점수 ≥ Genuine 기준 | PASS |
| Genuine | 점수 < Genuine 기준 | MISS: 본인 지문 오거절 |
| Impostor | 점수 < Impostor 기준 | REJ: 타인 지문 거절 |
| Impostor | 점수 ≥ Impostor 기준 | FA: 타인 지문 오수락 |
| 공통 | 추론 실패 | ERR |

모델을 불러오면 `calibration.json`의 실제 가중치 해시·ID 순서·FAR 정책·정밀도를 검사하고, 입력 DB와 선택 모델 조합에 맞는 추천값을 적용한다. 해당 개별 모델의 초기값은 FVC2000 0.5603, FVC2002 0.6648, FVC2004 0.7235다. 모델 선택이 바뀌면 새 조합의 추천값을 불러온다.

두 기준은 같은 값으로 시작하지만 각각 독립적으로 조절할 수 있다. 입력 범위는 [0, 1]이며, 기준 변경은 재추론 없이 판정·색상·지표만 갱신한다. 서로 다른 기준에서 얻은 두 정확도를 하나의 공통 경계에서 측정한 정확도로 합치지 않는다.

추천값을 읽을 수 없으면 안내와 함께 초기값 0.5954를 사용한다. 외부 DB는 제공 DB에서 얻은 보수적인 값을 적용하고 외부 이미지에서 미검증임을 안내한다. 추천값은 검증에 사용한 이미지쌍에 대한 결과이며, 임의의 비교 규칙에서 같은 FAR을 보장하지 않는다. [임계값 산정 근거](../Algorithm/readme.md#판정-임계값-보정-2026-11-18)

## Metrics / Report

화면과 HTML에는 선택한 지표를, CSV에는 전체 17개 지표를 저장한다. 판정 기준을 바꾸면 FMR·FNMR 등을 다시 계산한다. EER과 고정 오류율 지표는 현재 입력한 기준과 별개다. 점수를 한 번 정렬하고 중복 임계값을 제거한 뒤, 이진 탐색으로 각 기준의 오류 수를 구해 반복 계산을 줄인다. 그래프도 같은 점수 집계와 오류율 함수를 재사용하며, 비정상 점수를 만났을 때의 기존 표시 규칙을 유지한다.

| 지표 | 계산 기준 |
|---|---|
| FTE | 등록 실행이 모두 끝났을 때 실패 이미지 수 / 실행 이미지 수. 모델 하나 이상이 성공하면 해당 이미지는 성공이다. |
| REJ genuine / impostor | 해당 종류의 비교가 모두 끝났을 때 실패 비교 수 / 계획한 쌍 수 |
| FMR | Impostor 오수락 수 / 계획한 Impostor 쌍 수. 추론 실패는 오수락으로 세지 않는다. |
| FNMR | Genuine 오거절과 추론 실패 수 / 계획한 Genuine 쌍 수 |
| EER / EER* | 관측 점수와 최고 점수 바로 위의 기준을 순회한다. 일치하면 단일 값, 교차하면 범위를 표시한다. EER*는 성공한 쌍만 분모로 사용한다. |
| ZeroFMR / ZeroFNMR | FMR=0일 때 최소 FNMR / FNMR=0일 때 최소 FMR. 불가능하면 N/A다. |
| FMR100 / FMR1000 | FMR ≤ 0.01 / 0.001일 때 최소 FNMR. 표시는 %다. |
| 처리 시간 | 실행한 등록·비교의 평균 ms. 실패한 실행도 포함하며, 비교 시간에는 비교 이미지의 인코딩이 포함된다. |
| 템플릿 크기 | 성공한 템플릿의 실제 평균·최대 bytes. 120bytes의 확률 값과 식별 정보를 모두 포함한다. |

비교 계획이 없거나 아직 실행 중인 경우, 필요한 종류가 부족한 경우와 EER 교차가 없는 경우를 구분해 N/A로 표시한다. 이미지 읽기와 모델 로딩 시간은 위 처리 시간에 포함하지 않는다.

정확도 지표를 고르면 점수 분포와 임계값 곡선을 만든다. EER 계열 또는 FMR·FNMR을 함께 선택하면 오류율 상관 그래프도 만든다. 시간 지표만 선택하거나 아무 지표도 고르지 않으면 그래프를 저장하지 않는다. 그래프는 관측 범위를 확장한 41개 임계값을 사용하며, 지표 계산의 관측값 순회와는 다르다. 두 축은 선형 %이며 정규편차 DET 그래프는 아니다.

`Save all`은 Metrics 탭에서 활성화된다. 새 `run_N` 폴더에 원시 결과를 저장한 뒤 지표·HTML·SVG를 저장하며, 실패하면 이번에 만든 폴더만 지운다. 상세 실행 설정은 HTML에서 펼쳐 볼 수 있고 `settings.txt`에도 남긴다.

## Functions / Inputs / Outputs

| 함수 | 역할 |
|---|---|
| `*Page`, `ComparisonPanel` | 화면과 이벤트 연결 생성 |
| `LoadDatabase`, `ReloadModels`, `CreateEnrollmentTemplates` | 입력과 모델 로딩, 템플릿 생성 |
| `UpdateWorkflow`, `UpdateComparisons`, `UpdateEnrollment` | 진행 가능 여부와 비교 계획 갱신 |
| `ReadPairCsv`, `RunSelectedComparisons`, `RunComparisons` | CSV 읽기와 선택한 비교 실행 |
| `ApplyThreshold`, `UpdateMetrics` | 저장된 점수로 판정과 지표 갱신 |
| `SelectedMetrics`, `Snapshot`, `SaveResults` | 선택 지표와 실행 기록을 모아 저장 |
| `CalculateMetrics`, `GraphFiles`, `GraphTitle`, `WriteGraphs`, `Save` | Report의 지표·그래프·파일 생성 |

`SetAlgorithm`으로 연결한 알고리즘 객체는 호출자가 소유하며 Window보다 오래 유지해야 한다. Window는 Backend를 삭제하지 않는다. `validateCurrentPage`는 필수 선택을 검사하고, `eventFilter`와 `resizeEvent`는 안내와 단계 표시를 처리한다. `done`은 종료 확인을 처리한다.

## Hyperparameters / Display Defaults

설정은 [guiConfig.h](guiConfig.h)에, 비교 기준은 [engineConfig.h](../Engine/engineConfig.h)에 둔다. 일부 상태·오류 문구와 배치/CSS/SVG 좌표는 `.cpp`에도 있다.

| 설정 | 기본값 |
|---|---|
| Window / Preview | 900×720 / 340×340px |
| MetricColumns / DefaultMetrics | 3열 / FTE·EER·Matching time |
| ThresholdMinimum/Maximum / Step / Precision | 0~1 / 0.01 / 소수점 4자리 |
| Score / Time / Metric 정밀도 | 소수점 4자리 / 3자리 / 유효숫자 6자리 |
| GraphWidth/Height / HistogramBins | 840×320px / 10개 |
| CurveSteps / CurvePadding | 40 / 0.05 |

## Status / Evidence

운영 전환과 파일 통합 후 `QT_QPA_PLATFORM=offscreen`에서 실제 Window와 위젯으로 제공 DB 7개 흐름·140쌍과 외부 파일명 ID 1개 흐름·2쌍을 확인했다. 등록, 비교, 독립 기준 변경, `.fpt`·CSV·HTML 저장이 정상 동작했다. 검증 전용 `tests/`와 별도 CMake 대상은 이후 정리했으며, 이 검증 기록은 `models/current` 아래에 보존한다. 이후 코드 정리·폴더 이동 검증은 `models/validation`에, 화면 예시는 `GUI_flows`에 보관한다. 외부 검사는 기능 확인이며 미학습 손가락의 정확도 측정이 아니다. 학습에 포함된 검증과 재평가 명령은 [Algorithm README](../Algorithm/readme.md#verification--limits)에 정리했다.

당시 검증은 Qt offscreen으로 수행했으며, 실제 데스크톱 창 조작은 확인하지 못했다. 추론은 GUI 스레드에서 순차 실행하며, 실행 중 강제 취소와 오류 격리는 미구현이다. 진행 표시는 단계 번호이며 이미지쌍별 실시간 진행률이 아니다. Word/PDF 리포트, 정규편차 DET, 지표 수식 선택, KISA DLL은 미구현이고 Windows 배포도 미검증이다.

[GUI 화면 예시](../../GUI_flows/00_overview.png)와 [캡처 기록 안내](../../GUI_flows/captures/readme.md)를 참고한다. [Report_example](../../Report_example/readme.md)은 이전 리포트 예시로 보존한다.
