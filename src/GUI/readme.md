# GUI / Report

[Root](../../readme.md) · [Algorithm](../Algorithm/readme.md) · [Engine](../Engine/readme.md)

Source diagram: [SVG](../../docs/diagrams/gui.svg) · [PNG](../../docs/diagrams/gui.png). 화면 입력이 Engine 호출과 결과·지표·리포트로 이어지는 흐름을 보여준다.

![GUI and report structure](../../docs/diagrams/gui.png)

## Responsibility / Files

사용자가 DB와 등록 이미지를 고르고, 비교 규칙을 설정해 결과를 확인하는 화면이다. 추론과 원시 결과 저장은 Engine에 요청한다. Report는 화면 Widget 대신 `EvaluationRun`을 받아 지표·그래프·HTML을 만든다.

| 파일 | 역할 |
|---|---|
| [GUI.h](GUI.h) | `Window : QWizard`와 Image·ComparisonControls·Widget·Cache를 선언한다. |
| [GUI.cpp](GUI.cpp) | 7개 Page, DB/모델/이미지 선택, Event, Comparison plan 표시, Snapshot, Save 흐름을 구현한다. |
| [guiConfig.h](guiConfig.h) | 기본 Label·화면 크기·Metric 선택·표시 정밀도·Graph 설정이다. |
| [report.h](report.h) | `ReportData`와 Metric·Graph·HTML 함수 선언이다. |
| [report.cpp](report.cpp) | Metric 계산, SVG 문자열 생성, 전체 Metric CSV와 선택 Metric HTML을 저장한다. |
| [main.cpp](../main.cpp) | QApplication·FingerprintEncoder·Window를 만들고 Backend를 연결한다. |

## Includes

| Include | 사용 이유 |
|---|---|
| `QWizard`, `QLabel`, `QPushButton`, `QCheckBox`, `QComboBox`, `QSpinBox`, `QDoubleSpinBox` | Page 전환, 선택, Rule 입력, 실행 버튼을 구성한다. |
| `QListWidget`, `QTableWidget`, `QHeaderView`, `Q*Layout`, `QStackedWidget`, `QGroupBox`, `QTabWidget` | Enrollment list·결과 표·Rule별 입력 화면을 배치한다. |
| `QBrush` | 판정별 Row 배경색과 검정 글씨를 지정한다. |
| `QImageReader`, `QImage` | TIFF resolution·Pixel을 읽고 Preview와 동일 이미지 검사를 수행한다. |
| `QDir`, `QFileInfo`, `QFile`, `QFileDialog` | 폴더·CSV 탐색과 Pair CSV 읽기, 새 Run folder를 처리한다. |
| `QEvent`, `QVariant`, `QMessageBox`, Qt Signal/Slot | Hover 정보, Widget property, 입력 변화와 Exit 확인을 처리한다. |
| [Engine.h](../Engine/Engine.h) | Plan·Inference·Raw export API와 Result type을 사용한다. |
| report.cpp의 `QDir`, `QFile`, `cmath`, `limits` | Report 파일과 Rate·Finite·관측 Threshold 계산을 처리한다. |
| `QStringList`, `vector` | Metric name/value와 Selection flag를 전달한다. |

Qt Widgets로 화면을 구성한다. 그래프는 SVG를 직접 생성하므로 별도의 Chart/PDF Library를 연결하지 않는다. 사용하는 Library와 License는 [Root README](../../readme.md#libraries--headers)를 참고한다.

## Pages / Actual Flow

| Page | 선택·동작 |
|---|---|
| 1. DB | 세 Built-in tile 또는 External folder를 선택한다. Hover는 기본 Resolution을 보여준다. 선택 후 TIFF 총수와 Resolution별 수를 표시한다. |
| 2. Enrollment | Single 또는 Ctrl/Shift 기반 Multiple selection이다. 각 Image가 별도 Template을 만든다. |
| 3. Templates | 학습을 완료했거나 공유받은 Fine-tuned Weight 폴더와 Model을 선택한다. Built-in은 해당 모델 하나, External은 네 개가 기본 선택된다. `Create enrollment templates`로 Template을 생성한다. |
| 4. Comparison type | Genuine, Impostor, Both 중 하나를 선택한다. 선택 전에는 Next가 비활성화된다. |
| 5. Comparison rules | 선택한 Type의 Rule만 표시한다. Both는 Genuine/Impostor를 각각 설정한다. Pair 수만 표시하며 결과 표는 아직 보여주지 않는다. |
| 6. Run | 선택 Type·Rule·Pair 수를 확인하고 `Run selected comparisons`를 누른다. 선택한 Type만 한 번에 실행한다. 실행 전에는 Results로 넘어갈 수 없다. |
| 7. Results | 선택 Type의 결과 Tab과 Metrics and report Tab을 표시한다. 선택 Metric만 화면·HTML에 표시한다. 저장은 전체 Metric CSV·선택 Comparison의 Raw CSV·필요한 SVG를 포함한다. |

GUI는 `models/pretrained/deepprint.bin`을 비교용 모델로 사용하지 않는다. Clone 직후에는 Fine-tuning을 먼저 실행하거나 학습된 모델을 별도로 받아야 한다. Root의 `models/run_01`은 학습 명령의 출력 예시이며, GUI에서는 실제 네 `.bin`이 생성된 폴더를 선택한다.

DB의 바로 아래 `.tif`·`.tiff`를 읽으며 Subfolder는 재귀 탐색하지 않는다. 총수 표시는 TIFF 파일 기준이고 선택 List에는 Decode 가능한 이미지만 넣는다. 이름이 `finger_capture`이면 Known ID로 해석한다. 그 외 파일명도 Enrollment는 가능하지만 자동 Genuine/Impostor 후보에서는 제외한다. 이 경우 해당 Type의 Rule에서 CSV로 Label을 지정한다.

모델을 하나 선택하면 `Model / Score`, 두 개 이상 선택하면 `Best model / Best score / Worst model / Worst score`를 표시한다. Best/Worst는 성공한 모델의 최고/최저 유사도다. 성공 모델이 하나뿐이면 Worst는 비워 둔다. `#`는 각 비교 종류에서 1부터 Pair 수까지 증가한다. 화면의 값은 중앙 정렬하며, 모든 모델별 점수·오류는 Result Tooltip과 원시 CSV에 남긴다. Metric과 Report는 Best score를 사용한다.

각 결과 탭의 Threshold 입력 범위는 `[0,1]`, 기본값은 `0.75`다. Genuine·Impostor 기준은 각각 조절할 수 있으며, [판정과 색상](#result-labels--thresholds)은 실제 손가락 ID와 Best score로 결정한다. Unknown ID의 CSV 비교는 사용자가 지정한 Group label을 Ground truth로 사용한다. Save all 버튼은 Metrics and report 탭에서만 활성화된다. 동일 이미지의 비교 제외와 점수 집계는 Engine 규칙을 따른다.

Comparison selection은 비교 대상을 고르는 기준이다. All eligible images는 조건에 맞는 전체 이미지, One image per finger는 손가락당 첫 Capture, Selected capture numbers는 지정 번호, Random images per enrollment는 지정 개수, Comparison pairs from CSV는 저장된 Pair 순서다. Self-image 제외 규칙은 모든 선택에 적용한다. 한 장 등록한 Built-in DB에서 Impostor의 All eligible images는 72쌍, One image per finger는 9쌍이다.

Template 생성 전에는 Type 선택으로 진행할 수 없다. 활성 Rule이 잘못됐거나 Pair가 없으면 실행할 수 없다. DB·Enrollment·Model·Type·Rule을 변경하면 이전 Comparison 결과를 초기화하며 다시 실행해야 저장할 수 있다. 미선택 Type의 Rule·Pair는 실행·저장하지 않는다. 한 Type만 실행하면 두 Group이 필요한 EER 등의 Metric은 N/A다.

Pair CSV는 `enrollment,attempt`, 선택적 `type`을 지원한다. 파일명은 현재 DB의 이름이어야 한다. Quote escape·BOM·CRLF를 처리하지만 한 Record는 한 Line이어야 한다. 저장한 `comparisons.csv`도 다시 입력할 수 있다. Self·동일 Pixel·잘못된 Label 검사는 [Engine 규칙](../Engine/readme.md#comparison-rules)을 따른다.

## Functions / Inputs / Outputs

| 함수 | 입력 | 출력·목적 |
|---|---|---|
| `DatabasePage / EnrollmentPage / TemplatePage / ComparisonTypePage / ComparisonRulesPage / ExecutionPage / ResultsPage` | 현재 설정 | 7개 `QWizardPage*`와 Event 연결을 만든다. |
| `ComparisonPanel / ComparisonSelected / UpdateWorkflow` | Genuine 여부·Type·Template/Plan/실행 상태 | Type별 Rule Widget을 만들고 표시·Next·Run 가능 여부를 갱신한다. |
| `LoadDatabase` | Folder, 선택적 Built-in DB ID | Image list·Source ID·Resolution 표시·기본 Model 선택을 갱신한다. 빈 DB ID는 External이다. |
| `SetAlgorithm` | Caller가 소유하는 Algorithm pointer | Engine 연결 후 Model 상태를 다시 읽는다. Window는 Backend를 삭제하지 않는다. |
| `ReloadModels / ClearTemplates` | 현재 Model/Weight 선택 | Model Load 상태를 표시하고 이전 Template·실행 가능 상태를 초기화한다. |
| `CreateEnrollmentTemplates` | 선택 Image list | Engine::Enroll 결과를 Image별 Cache와 표에 보관한다. |
| `ReadPairCsv` | CSV path, Genuine 여부 | Pair index vector·Error, `bool`을 반환한다. |
| `UpdateComparisons / UpdateEnrollment` | 현재 Rule·Enrollment 선택 | 선택 Type의 Engine plan을 만들고 Pair 수·가능 여부·Metric 상태를 갱신한다. 기존 Comparison 결과는 초기화한다. |
| `RunSelectedComparisons` | 선택 Type·Plan·Template | 선택한 Group만 실행한 뒤 Results로 이동한다. |
| `ApplyThreshold` | Cache된 Result, 해당 Tab의 Threshold | PASS/MISS/REJ/FA/ERR·Row color·요약·Metric을 갱신한다. |
| `RunComparisons` | Genuine/Impostor Controls | Engine::Execute의 Score·Time·최고 Model·Model별 Error를 표시한다. |
| `SelectedMetrics / Snapshot` | 현재 Widget/Cache 상태 | Metric flag vector와 UI 독립 `EvaluationRun`을 반환한다. |
| `UpdateMetrics` | 현재 Snapshot | Report::CalculateMetrics의 17개 값을 표에 넣는다. |
| `SaveResults` | Output parent folder | 새 `run_N`에 Engine Raw export → Report Save. 실패하면 이번에 만든 폴더만 지우고 `false`다. |
| `done / validateCurrentPage / eventFilter / resizeEvent` | Exit·전환·Hover·Resize event | Exit 확인, 필수 선택 검사, DB 설명, 우측 상단 Step counter 위치를 처리한다. |

| Report 함수 | 입력 | 출력·목적 |
|---|---|---|
| `CalculateMetrics` | EvaluationRun | Metric name 순서의 `QStringList` 17개. 미실행·불충분한 Group은 N/A다. |
| `GraphFiles / GraphTitle` | Metric selection, File name | 선택에 맞는 SVG 파일명과 Title을 반환한다. |
| `WriteGraphs` | ReportData, Folder | Score histogram·Error rate curve·Linear trade-off SVG를 저장한다. |
| `Save` | ReportData, Folder | `metrics.csv` 전체, `report.html` 선택 항목, 관련 SVG를 저장한다. |

## Result Labels / Thresholds

Genuine은 같은 손가락, Impostor는 다른 손가락이라는 실제 Group label을 뜻한다. 단순히 점수가 높다고 모든 비교에 PASS를 붙이지 않는다. GUI와 CSV는 `Engine::ClassifyComparison`을 함께 사용한다.

| 실제 Group | Best score | 표시 / 색상 |
|---|---|---|
| Genuine | Threshold 이상 | PASS / 연한 초록 — 올바르게 수락 |
| Genuine | Threshold 미만 | MISS / 연한 빨강 — 본인을 잘못 거절 |
| Impostor | Threshold 미만 | REJ / 연한 파랑 — 타인을 올바르게 거절 |
| Impostor | Threshold 이상 | FA / 연한 주황 — 타인을 잘못 수락 |
| 어느 Group이든 추론 실패 | 유효한 점수 없음 | ERR / 회색 |

Threshold를 바꾸면 저장된 점수로 다시 판정하고 색상·요약·FMR/FNMR을 갱신한다. Encoder를 다시 실행하지는 않는다. Metrics의 `REJ genuine/impostor`는 계산 실패율이며, 위 표의 올바른 타인 거절 `REJ`와 다른 항목이다.

## Metrics / Interpretation

현재 계산 방식은 [report.cpp](report.cpp)의 `Rates`, `Thresholds`, `EerBounds`, `CalculateMetrics`가 기준이다. Score는 해당 Group의 입력 Threshold 이상일 때 Accept다. FMR은 Impostor 기준, FNMR은 Genuine 기준을 사용한다. EER·파생 Metric·Curve는 별도로 공통 Threshold를 순회하므로 입력 기준 변경에 영향받지 않는다. Model을 여러 개 선택하면 Engine이 고른 최고 Score를 사용한다.

| Metric | 현재 계산 |
|---|---|
| FTE [%] | 선택 Enrollment가 모두 실행됐을 때 실패 Image 수 / 실행 Image 수. Model 하나 이상이 성공하면 해당 Image는 성공이다. |
| REJ genuine / impostor [%] | 해당 Group이 모두 실행됐을 때 실패 Comparison 수 / Planned pairs. |
| FMR [%] | Impostor false accept 수 / Planned impostor pairs. 실패 Impostor는 False accept로 세지 않는다. |
| FNMR [%] | (Genuine false reject 수 + 실패 Genuine 수) / Planned genuine pairs. |
| EER / EER* [%] | 관측 Score와 최고 Score 바로 위 Threshold를 순회한다. 두 Rate가 같으면 단일 값, 교차하면 Bounds를 표시한다. EER*는 성공 Pair만 분모로 사용한다. |
| ZeroFMR / ZeroFNMR [%] | 관측 Threshold에서 FMR=0일 때 최소 FNMR / FNMR=0일 때 최소 FMR이다. 불가능하면 N/A다. |
| FMR100 / FMR1000 [%] | FMR ≤ 0.01 / 0.001 조건에서 최소 FNMR이다. 표시는 [%]다. |
| Enrollment / Matching / Genuine / Impostor time [ms] | 실행한 항목의 평균 경과 시간. Matching은 두 Comparison group을 합친 평균이다. |
| Mean / Max template size [bytes] | 성공한 Image/Model Template들의 평균·최대 크기다. |

비교계획이 없는 Group, 아직 Pending인 Group, EER 교차가 없는 경우는 구분해 N/A를 반환한다. Mean time은 실패한 실행도 포함한다. 여기서 Matching time에는 Attempt Encoding이 포함되며 Image decoding·Model Load는 포함되지 않는다.

Accuracy Metric을 선택하면 Histogram·Threshold curve를 만든다. EER 계열 또는 FMR/FNMR 동시 선택이면 Trade-off graph도 만든다. FMR 또는 FNMR만 선택한 경우 해당 Curve만 그린다. Timing-only 또는 선택 없음이면 Graph를 만들지 않는다. CSV는 항상 17개 Metric을 보관한다.

Graph는 관측 범위를 확장한 41개 Threshold sample을 사용한다. Metric의 관측 Threshold sweep과는 다르다. Trade-off의 두 축은 Linear [%]이며 Normal-deviate DET graph는 아니다. 실패율·Bounds 처리의 구현은 확인했지만 과제의 모든 수식 선택 기능이나 외부 인증 적합성 검증을 구현한 것은 아니다.

## Hyperparameters / Display Defaults

설정은 [guiConfig.h](guiConfig.h), 비교 기준은 [engineConfig.h](../Engine/engineConfig.h)에 있다. 모든 기본 Label이 외부 번역 파일에 있는 구조는 아니다. 상태·Error 문구와 일부 Layout/CSS/SVG 좌표는 `.cpp`에도 남아 있다.

| 설정 | 현재 값 | 의미 |
|---|---|---|
| Window / Preview | 900×720 / 340×340 [px] | 시작 Window와 Image preview 크기다. |
| StepWidth/Height, StepRight/Top | 90×20, 20/14 [px] | 우측 상단 `Step N / 7` 표시 위치다. |
| ModelTableHeight / OptionsHeight | 165 / 36 [px] | Model 상태 표와 Rule 입력 영역 높이다. |
| MetricColumns / DefaultMetrics | 3 / FTE·EER·Matching time | Checkbox 배치와 초기 선택이다. |
| MaxRandomCount / DefaultCaptures | 1,000,000 / `1` | Random count 상한과 Capture 입력 초기값이다. |
| ThresholdMinimum/Maximum / ThresholdStep / ThresholdPrecision | 0~1 / 0.01 / 4 decimal | 기준 입력 범위·증감 폭·정밀도다. |
| PassColor / MissColor / RejectColor / FalseAcceptColor / ErrorColor | `#e5f3e8` / `#fbe5e5` / `#e5effa` / `#fff0d9` / `#f2f2f2` | PASS·MISS·REJ·FA·ERR Row 음영이다. |
| ScorePrecision / TimePrecision / MetricPrecision | 4 decimal / 3 decimal / 6 significant | 화면의 Score·Time·Metric 문자열 정밀도다. |
| GraphWidth/Height / HistogramBins | 840×320 [px] / 10 | SVG 표시 크기와 Histogram bin 수다. Logical viewBox 좌표는 현재 840×320이다. |
| CurveSteps / CurvePadding | 40 / 0.05 [ratio] | 41개 Threshold sample과 관측 범위의 여유 폭이다. |
| GraphPrecision / EmptyScoreLow/High | 5 significant / 0, 1 [normalized score] | SVG 숫자 정밀도와 빈 Plot의 기본 범위다. |
| Engine default | Seed 42, Count 10, Threshold 0.75 | [Engine 설명](../Engine/readme.md#hyperparameters--paths)을 따른다. |

## Status / Evidence

macOS Offscreen GUI에서 Built-in 기본 Model, Combined 선택, External 네 Model, 실제 Template·Genuine/Impostor·Raw CSV 저장을 확인했다. 합성 사례로 실패 분모·Time·Template size·EER Bounds/EER*·선택 Metric·SVG 저장을 확인했다. 이전 학습/Inference 기록은 `models/implementation_check.json`에 있다. Source 재구성 이후 검증 Log는 당시 임시 Build 환경에 있으며 영구 Test target은 CMake에 없다.

7단계 순서, 세 Comparison type, 비활성 Type 제외, 입력 변경 후 결과 초기화, Rule 오류 차단, 실제 Model의 Template·Score·선택 Pair 저장을 Offscreen GUI에서 확인했다.

현재 Progress는 Page counter이며 실행 Pair별 Live progress가 아니다. Inference가 GUI thread에서 순차 실행되므로 실행 중 UI 응답·Cancel의 즉시 강제 종료는 지원하지 않는다. Exit 확인창과 Yes 후 종료는 구현돼 있다. Word/PDF Report, Normal-deviate DET, Metric 수식 선택 UI, KISA DLL 선택·호출, Windows 배포는 미구현 또는 실행 미확인이다. [GUI_flows](../../GUI_flows/readme.md)와 [Report_example](../../Report_example/readme.md)은 2026-11-18의 실제 재학습 Weight로 갱신한 예시다. 촬영 조건과 Threshold는 각 폴더 문서에 남겼으며, 독립 Test-set 성능을 의미하지 않는다.
