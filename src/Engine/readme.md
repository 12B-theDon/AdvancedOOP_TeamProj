# Engine

[Root](../../readme.md) · [Algorithm](../Algorithm/readme.md) · [GUI](../GUI/readme.md)

Source diagram: [SVG](../../docs/diagrams/engine.svg) · [PNG](../../docs/diagrams/engine.png). 비교 계획, 모델 로드, Template 생성, 비교와 원시 결과 저장의 연결을 보여준다.

![Engine structure](../../docs/diagrams/engine.png)

## Responsibility / Files

비교할 이미지쌍을 구성하고 Algorithm을 호출하는 실행부다. 모델별 Template과 점수·시간·오류를 모아 원시 파일로 저장한다. 화면은 GUI가, 지표와 HTML은 Report가 담당한다.

| 파일 | 역할 |
|---|---|
| [Engine.h](Engine.h) | Public API, Image·Pair·Model·Template·Result·Run type을 선언한다. |
| [Engine.cpp](Engine.cpp) | Plan, Model routing, Enrollment, Comparison, Synchronous execution, Raw export를 구현한다. |
| [engineConfig.h](engineConfig.h) | DB/Model ID·경로·Seed·Threshold·지원 Execution 기본값을 둔다. |

## Includes

| Include | 사용 이유 |
|---|---|
| [Algorithm.h](../Algorithm/Algorithm.h) | Abstract API와 Pixel/Preprocessing buffer를 공유한다. |
| `string`, `vector` | GUI widget과 독립적인 입력·계획·결과를 저장한다. |
| `random` | `mt19937`, Uniform distribution으로 Fisher–Yates shuffle을 수행한다. |
| `chrono` | `steady_clock`으로 경과 시간 [ms]를 측정한다. |
| `QImage` | Image file을 Gray8 buffer로 읽는다. Qt TIFF plugin이 필요하다. |
| `QDir`, `QFile` | CSV·Template·Settings 파일을 저장한다. |

필요한 Qt Core/Gui는 [CMakeLists.txt](../../CMakeLists.txt)에서 연결한다. 추가 Image/CSV Library는 사용하지 않는다.

## Types / Data Exchange

| Type | 담는 정보·계약 |
|---|---|
| `ImageInfo` | 파일명·경로·Finger/Capture ID·`source_id`. ID 0은 Unknown이다. 같은 `source_id`는 동일 Pixel을 뜻하며 −1은 Unknown이다. |
| `ComparisonPair` | DB Image list의 0-based Enrollment/Attempt index다. |
| `ComparisonOptions` | Rule, Capture 번호, Count, Seed 또는 CSV pair 순서다. |
| `ModelState` | Model ID, Weight file, Load 성공 여부와 Error다. |
| `ModelTemplate` | 한 Image·한 Model의 Template [bytes], 처리 시간 [ms], 실패 Stage·Error다. |
| `EnrollmentTemplates` | 한 Enrollment의 모든 ModelTemplate과 전체 성공 여부·실행 여부·Model revision이다. 하나 이상의 Model이 성공하면 Enrollment 성공이다. |
| `ModelScore`, `SimilarityResult` | Model별 Score·Time·Error와 가장 높은 성공 Score·Model ID·전체 시간이다. |
| `EnrollmentRecord`, `ComparisonRecord` | 실제 실행 여부와 원본 이름·실행 결과를 묶는다. Plan만 있는 상태도 표현한다. |
| `EvaluationRun` | DB/Weight 경로·Settings·Models·Enrollment/Comparison 기록을 담는 Export snapshot이다. |

GUI → Engine: Image list, 선택한 Enrollment, Model ID, ComparisonOptions, File path를 보낸다. Engine → Algorithm: Gray8 image와 Model ID를 보낸다. Algorithm → Engine: Model별 Feature buffer·Normalized score·Error를 돌려준다. GUI의 `Snapshot()`은 `EvaluationRun`으로 Raw export와 Report에 같은 결과를 전달한다.

## API / Execution Path

| 함수 | 입력 | 출력·목적 |
|---|---|---|
| `PlanComparisons` | Images, Enrollment indices, Genuine 여부, Options | `bool`, Pair vector, Error. Self/동일 Pixel·Label 위반을 제외하고 순서를 만든다. |
| `SelectModels` | Built-in DB ID 또는 External flag, Weight folder | 기본 `ModelState` 목록. Built-in은 대응 Model 하나, External은 네 Model을 선택한다. |
| `LoadModels` | 위 선택 정보, 빌린 Algorithm pointer | Default 선택 후 Load한다. 하나 이상 Loaded이면 `true`다. |
| `LoadSelectedModels` | 선택 Model ID 목록, Weight folder, Algorithm pointer | Config에 있는 ID만 허용하고 중복 ID를 제거한다. Revision을 갱신하고 모든 Load 결과를 보관한다. |
| `Models` | 없음 | 현재 ModelState list의 Const reference다. |
| `ReadImage` | Image path | Tightly packed Gray8 `AlgorithmImage`. Decode 실패 시 빈 Image가 된다. |
| `Enroll` | 한 `AlgorithmImage` | 각 Model로 Preprocess → CreateTemplate을 수행한 `EnrollmentTemplates`다. |
| `Compare` | EnrollmentTemplates, Attempt image | 같은 Model끼리 Template을 비교한 `SimilarityResult`다. Model revision이 다르면 재등록을 요구한다. |
| `Execute` | Ordered pairs, Images, Enrollment cache | Pair 순서와 같은 Result vector. 이미지 파일을 읽고 `Compare`를 순차 호출한다. |
| `ClassifyComparison` | Genuine 여부, SimilarityResult, Threshold | Genuine은 PASS/MISS, Impostor는 REJ/FA, 추론 실패는 ERR를 반환한다. GUI·CSV가 같은 함수를 사용한다. |
| `SaveRawResults` | EvaluationRun, 이미 생성된 Run folder | 파일 저장 성공 여부다. 새 폴더 선택과 실패 Rollback은 GUI가 담당한다. |
| `MakeTemplate` / Private | 한 ModelState, Image | Input 검사·Preprocess·Encoding·Stage/Error·Time을 묶는다. |

Algorithm pointer는 Engine이 소유하지 않는다. [main.cpp](../main.cpp)의 Encoder가 Window보다 오래 살아 있도록 구성한다.

`Compare`는 성공한 Model 중 가장 높은 Normalized score `[0,1]`를 선택한다. 동점이면 먼저 선택된 Model이다. 실패한 모델과 0~1 범위를 벗어난 점수는 제외한다. 범위 비교를 통과하지 못하는 NaN·Infinity도 실패로 처리한다. 모두 실패하면 Score를 성공값으로 표시하지 않는다. Enrollment와 Attempt는 같은 Model의 같은 Preprocessing을 사용한다. Model 간 Score calibration은 없다.

Time은 Algorithm 호출 경로의 경과 시간이다. Enrollment는 Preprocessing+Encoding, Comparison은 Attempt Encoding+Cosine+Model 순회를 포함한다. `ReadImage`의 File decoding은 이 시간 밖에서 실행한다. Model Load 시간도 별도 Metric으로 수집하지 않는다.

## Comparison Rules

| Rule | 동작 |
|---|---|
| `All` | Known finger ID에서 조건에 맞는 모든 Pair를 만든다. |
| `FirstCapture` | Self를 제외한 후보에서 Finger별 가장 낮은 Capture 번호를 선택한다. 항상 Capture 1을 뜻하지는 않는다. |
| `CaptureNumbers` | 지정한 Capture 번호만 후보로 사용한다. |
| `RandomCount` | Enrollment별 최대 Count개를 뽑는다. 후보보다 많으면 가능한 수까지만 사용한다. |
| `FromCsv` | GUI가 읽은 Pair index를 그대로 사용한다. CSV 순서를 다시 Shuffle하지 않는다. |

자동 Rule은 Genuine에서 같은 Finger, Impostor에서 다른 Finger를 선택한다. Unknown ID는 자동 후보에서 제외한다. 자동 실행 순서는 Seed로 Shuffle한다. CSV는 선택되지 않은 Enrollment, 범위를 벗어난 Index, 중복 Pair, Self/동일 Pixel, Known label 모순을 거절한다. Genuine/Impostor 양쪽에 같은 Unordered pair가 들어가는 검사는 GUI가 추가 수행한다.

## Hyperparameters / Paths

값은 [engineConfig.h](engineConfig.h)에서 읽는다. Source 변경 후 다시 Build해야 한다.

| 설정 | 현재 값 | 의미·지원 범위 |
|---|---|---|
| `Seed` | 42 | 자동 Pair 선택·순서의 기본 Seed다. GUI는 RandomCount일 때만 Seed를 표시·Export한다. 실제 순서 재현은 저장 CSV가 기준이다. |
| `RandomCount` | 10 [pairs/enrollment] | RandomCount 초기값이다. GUI에서 바꿀 수 있다. |
| `DecisionThreshold` | 0.75 [normalized score] | Genuine/Impostor Threshold의 초기값이다. GUI가 Run별 실제 입력값을 Snapshot에 보관한다. |
| `MetricTolerance` | `1e-12` [ratio] | Report의 Rate 비교 허용 오차다. |
| `CsvPrecision` | 17 significant digits | 원시 Score·Time 저장 정밀도다. |
| `TimeoutMs`, `Concurrency` | 0 [ms], 1 [worker] | 현재 지원하는 값이다. 0은 Timeout 비활성화다. 다른 값이면 `Execute`가 Execution failure를 반환한다. |
| `ImagesFolder`, `WeightsFolder` | `data/original`, `models` | GUI/CLI가 공유하는 기본 경로다. |
| `WeightExtension` | `.bin` | Model weight와 Template 파일의 확장자다. 실제 Format은 서로 다르다. |
| `DatabaseIds` | FVC2000_DB3_B, FVC2002_DB3_B, FVC2004_DB1_B | Built-in 세 DB다. |
| `ModelIds` | fvc2000_db3, fvc2002_db3, fvc2004_db1, combined | 앞의 세 Model이 DB 순서에 대응한다. |

## Raw Export

| 파일 | 내용 |
|---|---|
| `models.csv` | 선택 Model·Weight path·Load status·Error |
| `enrollments.csv` | Image/Model별 Status·Template size/path·Stage/Error·Time |
| `comparisons.csv` | Type·Enrollment·Attempt·실행 Status·최고 Score·Time·선택 Model·Model별 요약·Group별 Index·Threshold·PASS/MISS/REJ/FA/ERR |
| `model_scores.csv` | Pair/Model별 Status·Score·Stage/Error·Time |
| `templates/<index>_<model>.bin` | 성공한 Enrollment Feature vector. Weight archive가 아니다. |
| `settings.txt` | Run의 Genuine/Impostor Threshold와 판정 규칙, Snapshot의 실제 Rule·선택 Metric·Random 설정 |

Metric CSV·HTML·SVG는 [Report](../GUI/report.cpp)가 저장한다. `Not run`과 실행 후 `Failed`는 구분한다. CSV의 문자열은 Quote escaping을 수행한다.

## DLL Backend / Extension

### 현재 지원 범위

현재 실행 파일은 외부 알고리즘 DLL을 동적으로 로드하지 않는다. [CMakeLists.txt](../../CMakeLists.txt)는 Algorithm·Engine을 STATIC으로 연결하고, [main.cpp](../main.cpp)는 `FingerprintEncoder`를 생성해 GUI에 전달한다. GUI의 `.bin` 선택은 이 내장 Encoder가 읽는 Weight를 바꾸는 기능이다.

| 교체 대상 | 현재 동작 |
|---|---|
| 호환되는 DeepPrint `.bin` | GUI에서 폴더와 모델을 선택하면 새 Weight를 로드한다. 기존 Template은 초기화한다. |
| 다른 구조의 모델 Weight | Metadata·입력 크기·출력 차원 등이 맞지 않으면 로드를 거절한다. |
| KISA 규격 알고리즘 DLL | 로딩·호출 Adapter와 GUI 선택 경로가 없어 현재 실행할 수 없다. |
| Qt·LibTorch DLL | 프로그램의 실행 의존성이다. 이를 교체한다고 지문 알고리즘이 바뀌지는 않는다. |

### DLL 연동 시 필요한 구현

아래는 확장 지점이며 아직 구현된 사용 절차가 아니다.

1. [Algorithm.h](../Algorithm/Algorithm.h)의 인터페이스를 구현하는 DLL Backend를 추가한다. Windows의 `LoadLibrary` 또는 Qt의 `QLibrary` 같은 로더와 KISA 함수 규격을 연결해야 한다.
2. 이미지 버퍼·전처리·Template 형식과 메모리 소유권을 DLL 규약에 맞춘다. 반환 상태·점수 범위는 현재 Engine이 사용하는 성공/실패와 0~1 유사도로 변환한다.
3. `.bin` 파일을 전제로 하는 현재 모델 선택 경로에 DLL 경로와 초기화 설정을 추가하고, GUI에서 Backend를 고를 수 있게 연결한다. 현재 `LoadModel` 호출을 DLL 함수에 그대로 전달할 수 있다고 가정하지 않는다.
4. 같은 이미지쌍으로 현재 Backend와 DLL Backend의 등록·비교·저장을 검증한다. 알고리즘 Crash·Hard timeout을 격리하려면 별도 Worker process도 필요하다.

DLL의 Export 함수·호출 규약·버퍼 규격이 없으면 파일만 바꿔 연결할 수 없다. Windows DLL을 macOS/Linux에서 그대로 로드하는 경로도 지원하지 않는다.

## Status

기본 Model routing·실제 Inference·Raw export는 macOS에서 확인했다. 현재는 Synchronous runner로 GUI thread를 사용한다. Hard timeout, Worker process, 동시 실행, 실행 중 강제 중단, Crash isolation은 미구현이다. GUI의 Exit 확인창은 이 기능들을 대신하지 않는다. Windows/KISA DLL 경로도 아직 미확인·미구현이다. License는 [Root](../../readme.md#license--submodule)를 따른다.
