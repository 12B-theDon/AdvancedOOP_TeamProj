# 검증 결과 — 2026-11-21

Windows·MSVC 전용 프로젝트입니다. Windows PowerShell 5.1 로그에서 Qt·LibTorch 설치, MSVC Release 빌드, 모델 4개 재사용을 확인했습니다. 최신 Windows 로그에서는 LibTorch 고지 검사를 통과한 뒤 SDK에서 Qt 고지를 찾지 못해 `[8/9]`에서 중단됐습니다. Qt 고지 fallback 수정 후 파일 패키징 검사는 macOS 검증 환경에서 수행했으며, 수정본의 실제 Windows 패키징·GUI/DLL 실행은 아직 재검증하지 않았습니다.

| 현재 검사 | 결과 |
|---|---|
| 플랫폼 제한 | CMake가 Windows 외 OS와 MSVC 외 toolchain을 거부하는지 확인 |
| Windows 빌드 로그 | Windows PowerShell 5.1·MSVC 19.44·Qt 6.8.3·LibTorch 2.15.0 CPU에서 GUI·Train·Worker·공개 DLL·Core DLL 생성 완료. 모델 4개 확인 후 학습 생략. 사용자가 제공한 로그 기준 |
| Windows 전용 전환 | 전환 당시 전처리 결과 9개 조합이 기존 Windows/MSVC 코드·include 선택과 일치. 이번 Stop 수정 이전의 검사 |
| 공개 KISA ABI | Clang의 Windows x64·x86 target에서 함수 포인터 자료형·export 선언의 구문 검사 통과. MSVC 링크·실행 검사는 아님 |
| 원본·학습 보존 | 원본 241개·디렉터리·가중치·설정·학습/검증 함수 본문 보존 검사 통과 |
| 설치 스크립트 | PowerShell 7.7에서 구문·helper·격리 모의 실행 통과: 한글 프로젝트 경로·ASCII 의존성 경로, Qt 설치 실패·완료 기록·경로·버전 검사, 부분 설치 보관·재설치, 다운로드 우선·학습 생략·fallback, 패키징·GUI 실행, 실패 시 중단. Windows PowerShell 5.1 실제 실행은 위 로그의 `[8/9]` 진입까지 확인 |
| 전체 Worker 취소 | 현재 `kisaDll.cpp`를 WinAPI mock으로 컴파일·실행: 실행·대기 slot 전체 종료, job 자식 종료 대기, handle 해제, 비차단 Cancel, Reset 대기, 반복 Stop, destructor 정리, 시작 도중 취소·취소 후 대기 Worker 생성 방지 통과. 실제 Windows job·pipe 검사는 아님 |
| GUI 종료 | 현재 GUI·Engine을 Qt 5.15 offscreen·검증용 backend로 컴파일·실행: Stop 버튼, 정리 대기·결과 폐기·저장 차단·재실행, 실행 중 창 닫기의 Yes/No, 확인 중 작업 완료, 정리 후 지연 종료, 예외 시 정리 통과 |
| 패키징 검사 | manifest 누락·손상·경로 보호·가중치 누락 거부 통과. 실제 Algorithm·Evaluator CMake 스크립트에서 runtime 탐색·외부 windeployqt만 mock으로 대체하고 Qt/LibTorch 고지 fallback·파일 복사·manifest·폴더 교체 통과. 버전·build·SHA·고지 누락, windeployqt 실패·TIFF 누락 거부와 기존 배포본 보존 통과. SDK의 LICENSES 및 doc/global/licenses 경로도 확인 |
| LibTorch 고지 출처 | 공식 Windows CPU ZIP 파일 목록 9,573개에 라이선스 고지가 없음을 확인. 동일 버전·build의 공식 Windows CPU wheel SHA-256 확인 후 고지 61개 전문 보존. URL·해시를 provenance에 기록 |
| Qt 고지 출처 | 공식 Qt Base·Qt SVG·Qt Image Formats 6.8.3 source archive 3개의 SHA-256을 Qt 서버의 공개 checksum과 대조. 라이선스·third-party 고지·attribution 156개 전문과 원문별 SHA-256 보존. attribution의 LicenseFile 참조도 모두 포함 |
| 그림 | 전체 Worker 종료·cleanup·GUI 종료 대기를 구조도에 반영. Arial·SVG/PNG의 크기·aspect ratio 유지 |

이번 검증용 mock은 Windows 이외의 실행 지원 코드에 포함하지 않습니다. 배포 대상은 Windows·MSVC로 유지합니다. 실제 Windows의 수정 후 패키징·DLL/IPC·Stop·종료 범위는 추가 실행 검증이 필요합니다.

<details>
<summary>Windows 전용 전환 이전 검증 기록</summary>

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

당시 미검증 항목은 Windows 설치 스크립트의 실제 도구 설치·Windows 빌드·PE export/import·실제 worker IPC/장애/timeout/Stop/재시작·Unicode DLL 교체·1/4-process 점수 일치·배포 런타임/TIFF 구성과 Microsoft Word에서의 RTF 열기입니다.

2026-11-21 기존 설정(각 40 epochs·seed 42)으로 모델 4개를 새로 학습했습니다. `models/trained_result`의 네 `.bin`은 총 1,066,301,763 bytes이며 `weights.json`에 SHA-256을 기록했습니다. 네 모델 모두 batch 1·2·4 재로딩, native self-score=1, 고정 Backbone·버퍼 보존 검사를 통과했습니다. native 비교 24개를 검증했고 cache/native 점수의 최대 차이는 약 6.18×10⁻⁷입니다.

리팩터링 전 production 모델 4개는 없었으므로 과거 모델과의 점수 회귀는 확인할 수 없습니다. 이번 파일은 사용자가 요청한 신규 학습 결과이며, 리팩터링 전 보존 기준을 대체하지 않습니다. 최종 Windows 패키지 생성과 GUI 자동 준비의 실제 Windows 실행은 미검증 상태입니다.

</details>
