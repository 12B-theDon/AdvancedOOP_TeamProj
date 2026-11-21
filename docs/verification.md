# 검증 결과 — 2026-11-21

macOS arm64, AppleClang 21, Qt 5.15.17, LibTorch 2.15.0 CPU에서 수행한 결과입니다.

| 검사 | 결과 |
|---|---|
| 전체 빌드·CTest | GUI·Train·Core 빌드, 8/8 통과 |
| Evaluator 전용 빌드 | Torch 의존성 없이 빌드, 5/5 통과 |
| 원본·학습 보존 | 원본 241개와 디렉터리, 설정·가중치·학습/비교 계획 코드 일치 |
| 추론 회귀 | 세 DB의 `101_1` 전처리·pretrained 512차원 embedding hash 일치 |
| 평가 엔진 | 픽셀·템플릿 길이, cache·순서·Stop·교체·병렬 점수·지표·보고서 검사 통과 |
| Core 통합 | 합성 모델로 경로 독립성, 지연 로딩·재사용, 버퍼·점수 집계 검사 통과 |
| GUI 시작 준비 | 배포 위치·누락 모델·LFS 포인터·Worker 감지, 독립 외부 DLL 평가기 호환 검사 통과 |
| GUI·출력 | Qt offscreen 확인, SVG 파싱·시각 확인, RTF textutil 변환 |
| 문서 그림 표시 | 로컬 GFM·WebKit에서 1000px·600px 화면 18개 조합, 그림 36개의 표시 폭·aspect ratio 검사 통과 |
| 패키징·소스 | 누락 파일·SHA·경로 보호 검사, `git diff --check` 통과 |
| Windows 설치 스크립트 | PowerShell 7.7 구문·격리 모의 실행 통과: 다운로드 우선·학습 생략, 부분 모델 보관·학습, 패키징·GUI 재실행, 공백·한글 경로, 실패 시 중단 |

위 결과는 이 작업 폴더에서 수행한 검증 기록입니다. 테스트 코드는 Git 배포 대상에서 제외하며, 로컬에 `tests/CMakeLists.txt`가 있을 때만 CMake에서 등록합니다. 보존 기준은 [baseline](baseline/training_configuration.md)에 정리했습니다. 합성 모델 검사는 실제 인식 성능을 검증하지 않습니다.

미검증 항목은 Windows 설치 스크립트의 실제 도구 설치·Windows 빌드·PE export/import·실제 worker IPC/장애/timeout/Stop/재시작·Unicode DLL 교체·1/4-process 점수 일치·배포 런타임/TIFF 구성과 Microsoft Word에서의 RTF 열기입니다.

2026-11-21 기존 설정(각 40 epochs·seed 42)으로 모델 4개를 새로 학습했습니다. `models/trained_result`의 네 `.bin`은 총 1,066,301,763 bytes이며 `weights.json`에 SHA-256을 기록했습니다. 네 모델 모두 batch 1·2·4 재로딩, native self-score=1, 고정 Backbone·버퍼 보존 검사를 통과했습니다. native 비교 24개를 검증했고 cache/native 점수의 최대 차이는 약 6.18×10⁻⁷입니다.

리팩터링 전 production 모델 4개는 없었으므로 과거 모델과의 점수 회귀는 확인할 수 없습니다. 이번 파일은 사용자가 요청한 신규 학습 결과이며, 리팩터링 전 보존 기준을 대체하지 않습니다. 최종 Windows 패키지 생성과 GUI 자동 준비의 실제 Windows 실행은 미검증 상태입니다.
