# Engine

[Root](../../readme.md) · [Algorithm](../Algorithm/readme.md) · [GUI](../GUI/readme.md)

Engine은 비교할 이미지쌍을 구성하고 Algorithm을 실행한다. 모델별 템플릿, 점수, 시간과 오류를 모아 원시 파일로 저장한다. 화면은 GUI가, 지표와 HTML은 Report가 담당한다. GUI와 Engine은 `ImageInfo`와 실행 기록을 공유한다.

![Engine structure](../../docs/diagrams/engine.png)

[구조도 SVG](../../docs/diagrams/engine.svg)

현재 Encoder는 30차원 확률 벡터를 담은 `.fpt` 템플릿을 사용한다. Engine은 모델·DB 조합별 추천 판정 기준을 제공하고, GUI는 Genuine과 Impostor 기준을 각각 조절한다.

## Responsibility / Files

Engine의 자료형·함수·설정값은 `engine` namespace에 둔다. Engine은 모델 선택과 등록 버전을 가진 실행 객체이므로 복사 생성자·복사 대입을 금지한다. `backend_`는 빌린 포인터이며 해제 책임은 호출자에게 있다. 구현 내부에서는 직접 이름을 사용하고, 알고리즘 인터페이스를 참조할 때는 `algorithm::Algorithm`처럼 다른 모듈의 범위를 명시한다.

| 파일 | 역할 |
|---|---|
| [Engine.h](Engine.h) | 공개 API와 이미지·비교 쌍·모델·템플릿·실행 결과 자료형 |
| [Engine.cpp](Engine.cpp) | 비교 계획, 모델 선택과 로딩, 등록·비교 실행, 원시 결과 저장 |
| [engineConfig.h](engineConfig.h) | DB·모델 ID, 경로, Seed, 판정 기준과 실행 기본값 |

[Algorithm.h](../Algorithm/Algorithm.h)의 추상 인터페이스를 사용한다. 이미지 읽기에는 Qt Gui와 TIFF 플러그인이 필요하며, 파일 저장은 Qt Core를 사용한다. 별도의 이미지·CSV 라이브러리는 연결하지 않는다. 경과 시간은 `steady_clock`으로 측정한다.

## Types / Data Exchange

| 자료형 | 내용 |
|---|---|
| `ImageInfo` | 파일명과 경로, 손가락·촬영 번호, 입력 DB, 동일 픽셀을 묶는 `source_id` |
| `ComparisonPair` | 이미지 목록에서 등록·비교 이미지의 0부터 시작하는 인덱스 |
| `ComparisonOptions` | 선택 규칙, 촬영 번호, 개수, Seed 또는 CSV 순서 |
| `ModelState` | 모델 ID와 파일 경로, 로딩 결과, 출력 차원과 실제 모델 식별 정보 |
| `ModelTemplate` | 한 이미지·한 모델의 직렬화된 템플릿, 처리 시간, 실패 단계와 오류 |
| `EnrollmentTemplates` | 한 등록 이미지의 모델별 템플릿과 모델 변경 버전 번호 |
| `ModelScore`, `SimilarityResult` | 모델별 결과와 가장 높은 성공 점수, 선택된 모델, 전체 시간 |
| `EnrollmentRecord`, `ComparisonRecord` | 계획과 실제 실행 여부, 이미지 이름과 결과 |
| `EvaluationRun` | 저장 시점의 DB·모델·설정·등록·비교 기록 |
| `ThresholdRecommendation` | 추천 임계값, 목표 충족 여부, 외부 입력용 여부와 근거. 상세 검증 비율은 보정 파일에 보관한다. |

GUI는 이미지 목록·등록 선택·모델 ID·비교 규칙을 전달한다. Engine은 Gray8 이미지와 모델 ID로 Algorithm을 호출하고 템플릿·점수·오류를 받는다. GUI의 `Snapshot()`은 같은 `EvaluationRun`을 원시 결과 저장과 Report에 전달한다. Algorithm 객체의 수명은 호출자가 관리하며, [main.cpp](../main.cpp)의 Encoder는 Window보다 오래 유지된다.

## API / Execution Path

| 함수 | 동작 |
|---|---|
| `PlanComparisons` | 이미지와 규칙으로 비교 쌍을 만들고 잘못된 쌍을 검사한다. |
| `SelectModels` | 제공 DB에는 대응 개별 모델, 외부 DB에는 네 모델을 기본 선택한다. |
| `LoadSelectedModels` | 설정에 있는 모델 ID만 허용하고 중복을 제거해 로딩한다. 모델 변경 버전을 갱신하며 실패 결과도 보관한다. |
| `Models` | 실제 로딩 정보가 포함된 모델 목록을 반환한다. |
| `RecommendedThreshold` | 보정 파일과 현재 모델이 일치하는지 검사하고 DB·모델 조합별 추천값을 반환한다. |
| `ReadImage` | 파일을 연속된 Gray8 픽셀과 입력 DB 정보로 읽는다. 실패하면 빈 이미지를 반환한다. |
| `Enroll` | 모델별 전처리와 템플릿 생성을 실행한다. 하나 이상 성공하면 이미지 등록은 성공이다. |
| `Compare` | 등록 때와 같은 모델로 비교 이미지의 템플릿을 만들고 모델별 점수를 모은다. |
| `Execute` | 계획한 순서대로 이미지를 읽고 비교한다. |
| `ClassifyComparison` | 원본 정답 종류와 점수, 임계값으로 PASS/MISS/REJ/FA/ERR를 반환한다. |
| `SaveRawResults` | 이미 생성된 실행 폴더에 원시 결과를 저장한다. 폴더 생성과 실패 시 정리는 GUI가 담당한다. |

`Compare`는 성공한 모델 중 가장 높은 점수를 최종 점수로 선택한다. 동점이면 먼저 선택된 모델을 사용한다. 실패, [0, 1] 범위 밖의 점수, NaN과 무한대는 제외한다. 모든 모델이 실패하면 유효한 점수를 반환하지 않는다. 모델별 점수를 별도로 변환하거나 평균내는 방식은 사용하지 않는다.

현재 Encoder는 같은 모델의 30차원 확률 템플릿을 비교한다. 모델이 바뀐 뒤의 이전 등록 기록은 재등록을 요구한다. Backend도 템플릿 내부의 버전·모델 ID·실제 가중치 해시·ID 순서·전처리를 확인하므로 다른 모델이나 구형 512차원 템플릿을 섞어 비교할 수 없다.

입력 DB가 명시되면 그 정보를 우선하고, 생략되면 알려진 부모 폴더 이름으로 식별한다. FVC2004는 중앙을 정사각형으로 자르고, 다른 DB나 출처 불명 이미지는 여백을 채운다. 이 규칙은 선택한 모델이 아니라 이미지 출처에 따라 정하며 등록과 비교에 동일하게 적용한다.

등록 시간은 전처리와 인코딩을 포함한다. 비교 시간은 비교 이미지 인코딩, 템플릿 유사도와 모델 순회를 포함한다. 파일 읽기와 모델 로딩 시간은 이 지표에 포함하지 않는다.

## Comparison Rules

| 규칙 | 동작 |
|---|---|
| `All` | ID를 아는 이미지 중 종류에 맞는 모든 후보를 사용한다. |
| `FirstCapture` | 자기 자신을 제외한 후보에서 손가락마다 촬영 번호가 가장 낮은 한 장을 선택한다. 반드시 1번 촬영본은 아니다. |
| `CaptureNumbers` | 지정한 촬영 번호만 사용한다. |
| `RandomCount` | 등록 이미지당 최대 지정 개수를 뽑는다. 후보가 적으면 가능한 수만 사용한다. |
| `FromCsv` | CSV의 비교 쌍과 순서를 그대로 사용한다. |

자동 규칙은 Genuine에서 같은 손가락, Impostor에서 다른 손가락을 선택한다. ID 0은 알 수 없는 ID로 취급해 자동 후보에서 제외한다. 같은 이미지와 동일 픽셀의 복제본도 제외한다. `source_id=-1`은 동일 픽셀 여부를 알 수 없다는 뜻이다.

자동 비교 순서는 Seed에 따라 섞는다. 무작위 후보 선택에는 `mt19937`과 Fisher–Yates 방식의 섞기를 사용한다. CSV는 다시 섞지 않으며, 미선택 등록 이미지·범위 밖 인덱스·중복 쌍·동일 이미지·알려진 ID와 모순된 종류를 거절한다. Genuine과 Impostor에 같은 순서 없는 쌍이 들어가는지는 GUI에서 추가 검사한다.

## Hyperparameters / Paths

값은 [engineConfig.h](engineConfig.h)에 둔다. 소스를 수정하면 다시 빌드해야 하며, 판정 기준은 GUI 실행 중에도 조절할 수 있다.

| 설정 | 기본값과 의미 |
|---|---|
| `Seed` | 42. 자동 선택·순서에 사용하며 실제 순서는 저장한 CSV로 재현한다. |
| `RandomCount` | 등록 이미지당 최대 10쌍. GUI에서 변경할 수 있다. |
| `GenuineThreshold`, `ImpostorThreshold` | 초기값 0.5954. 정상 모델 로딩 후에는 DB·선택 모델별 추천값을 우선한다. |
| `TargetFar`, `ThresholdPrecision` | 검증 목표 FAR 0.01, 소수점 네 자리 올림 |
| `MetricTolerance`, `CsvPrecision` | 비율 비교 허용 오차 1e-12, 원시 숫자 유효숫자 17자리 |
| `TimeoutMs`, `Concurrency` | 0ms, 1. 시간 제한 없이 순차 실행하며 다른 값은 실행 오류로 처리한다. |
| `ImagesFolder` | `data/original`. Weight 폴더는 GUI에서 선택한 학습 출력 경로를 사용한다. |
| `WeightExtension`, `TemplateExtension` | 전체 모델 `.bin`, 템플릿 `.fpt` |
| `DatabaseIds` | FVC2000_DB3_B, FVC2002_DB3_B, FVC2004_DB1_B |
| `ModelIds` | fvc2000_db3, fvc2002_db3, fvc2004_db1, combined |

`calibration.json`은 실제 가중치와 ID 순서의 해시, 파일 형식, FAR 정책과 정밀도를 확인한 뒤 사용한다. 가능한 15개 모델 조합마다 세 DB의 최고 점수 분포를 평가하고, 외부 입력용 값까지 총 60개 항목을 제공한다. 외부 입력에는 알려진 세 DB의 임계값 중 최댓값을 사용한다. 추천값은 학습에 등장한 ID의 다른 촬영본에서 선정한 결과로, 외부 DB나 임의의 GUI 비교 쌍에서 같은 FAR을 보장하지 않는다. [평가와 보정 근거](../Algorithm/readme.md#final-production-run--calibration)

Genuine은 기준 이상일 때 PASS, 미만일 때 MISS다. Impostor는 기준 미만일 때 REJ, 이상일 때 FA다. 두 조절기는 같은 추천값에서 시작해 독립적으로 수정되며, 실제 값은 실행 기록에 남긴다.

## Raw Export

| 파일 | 내용 |
|---|---|
| `models.csv` | 모델·파일 경로·로딩 결과·형식·차원·가중치 및 ID 순서 해시·전처리 |
| `enrollments.csv` | 이미지·모델별 상태, 템플릿 크기와 경로, 시간과 오류 |
| `comparisons.csv` | 정답 종류, 등록·비교 이미지, 최고 점수·모델·시간, 임계값과 판정 |
| `model_scores.csv` | 이미지쌍·모델별 점수, 시간과 오류 |
| `templates/<index>_<model>.fpt` | 성공한 등록 이미지의 확률 벡터와 식별 정보 |
| `settings.txt` | 두 판정 기준과 규칙, 선택 지표와 무작위 설정, 모델별 실행 정보 |

아직 실행하지 않은 상태와 실행 실패는 구분한다. CSV 문자열은 따옴표를 이스케이프한다. 지표 CSV와 HTML·SVG는 [Report](../GUI/report.cpp)에서 저장한다.

## DLL Backend / Extension

현재 Algorithm과 Engine은 실행 파일에 정적으로 연결한다. GUI의 `.bin` 선택은 내장 Encoder가 읽는 가중치를 바꾸는 기능이며 외부 DLL을 불러오는 기능은 아니다. 호환되는 Cosine30 v2 파일만 사용할 수 있고, 다른 구조는 입력·출력·메타데이터 검사에서 거절한다.

외부 알고리즘 DLL을 연결하려면 다음 구현이 필요하다.

1. `Algorithm` 인터페이스를 구현하고 `LoadLibrary` 또는 `QLibrary`로 DLL을 로드한다.
2. DLL 함수 규약에 맞춰 이미지·템플릿 형식과 메모리 소유권을 정하고, 결과를 Engine의 상태·점수 규칙으로 변환한다.
3. 모델 선택 화면에 DLL 경로와 초기화 설정을 연결한다.
4. 같은 이미지쌍으로 등록·비교·저장을 검증한다. 충돌과 강제 시간 제한을 격리하려면 별도 작업 프로세스가 필요하다.

DLL 파일만 교체해 연결할 수 없으며 함수와 버퍼 규약이 필요하다. Windows DLL을 macOS/Linux에서 그대로 실행하는 경로도 지원하지 않는다. Qt·LibTorch DLL은 실행 의존성이므로 지문 알고리즘 선택과는 별개다.

## Status

운영 전환과 파일 통합 과정에서 모델 선택, 버전 있는 템플릿, 실제 추론과 원시 결과 저장, 보정값 호환성을 확인했다. GUI 연동은 Qt 오프스크린으로 검증했다. 별도 검증 전용 소스와 실행 대상은 정리했으며, 모델 학습·평가에 포함된 검사는 `FingerprintTrain`에서 계속 수행한다. [평가 명령과 한계](../Algorithm/readme.md#verification--limits)를 참고한다. 실행은 GUI 스레드에서 동기 처리하며 강제 시간 제한·동시 실행·실행 중 강제 취소·프로세스 오류 격리는 미구현이다. 실제 데스크톱 화면과 Windows/KISA DLL 경로도 미검증 또는 미구현이다. 라이선스는 [Root](../../readme.md#4-license--submodule)를 따른다.
