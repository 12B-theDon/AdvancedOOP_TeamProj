# 저장소 준비와 배포 범위

저장소: [AdvancedOOP_TeamProj](https://github.com/12B-theDon/AdvancedOOP_TeamProj), 기본 브랜치: `main`.

## Clone과 의존성

Git LFS를 설치하고 `git lfs install`을 실행한 뒤 `git clone --recurse-submodules`로 받습니다. 사전학습 파일이 내려오지 않았다면 프로젝트 루트에서 `git lfs pull`을 실행합니다. OS별 빌드·학습·GUI 실행 명령은 [Root README](../readme.md)에 있습니다.

| 항목 | 내용 |
|---|---|
| Submodule | `third_party/deepprint` |
| 원본 저장소 | `https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors.git` |
| 고정 Commit | `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13` |
| 로컬 수정 Patch | [deepprint.patch](../third_party/deepprint.patch) |
| 출처·수정 설명 | [deepprint.NOTICE.txt](../third_party/deepprint.NOTICE.txt) |

외부 소스는 복사본 대신 Submodule의 Git 항목으로 관리합니다. Patch는 원본 체크포인트를 직접 변환할 때 한 번 적용하며, 이미 배포된 사전학습 `.bin`으로 C++ 학습을 실행할 때는 필요하지 않습니다. 변환 방법과 Patch 적용 명령은 [Algorithm 안내](../src/Algorithm/readme.md#conversion--offline-run)에 있습니다. 이 로컬 수정을 외부 저장소에 커밋하지 않습니다.

Qt와 LibTorch는 별도로 설치합니다. 학습 초기 파일 `models/pretrained/deepprint.bin`은 Git LFS로, 정보 파일 `deepprint.json`은 일반 파일로 포함합니다. GUI용으로 Fine-tuning한 모델은 배포하지 않으므로 새 Clone에서는 먼저 학습하거나 별도로 받은 호환 모델을 선택해야 합니다.

## 포함·제외 대상

| 저장소에 포함 | 로컬에 보존하고 제외 |
|---|---|
| C++ 소스·CMake·원본 TIFF DB | 빌드·IDE·Python 캐시, 증강 이미지 |
| 학습 초기 `.bin`과 정보 JSON, `models/readme.md` | `models/current`, 이전 모델·실험·검증 결과, 원본 체크포인트 |
| 구조도 SVG·PNG, GUI 화면 모음 PNG | `GUI_flows/captures`의 원본 화면·실행 결과·샘플 이미지·요약 JSON |
| `GUI_flows/captures/readme.md`, 이전 `Report_example` | `tools/readme.md`의 로컬 스타일 기록 |

`Report_example/templates/*.bin`은 작은 출력 특징 벡터이며 학습 Weight가 아닙니다. 현재 모델 폴더의 역할은 [모델 안내](../models/readme.md)를 참고합니다.

## 예시와 검증 기록

`GUI_flows`의 화면 모음은 2026-11-18의 30차원 모델 실행을 보여줍니다. 원본 캡처와 원시 결과는 로컬 자료이며 [캡처 기록 안내](../GUI_flows/captures/readme.md)에서 범위를 설명합니다. 문서용 그림·캡처 생성 도구는 정리했고 결과 이미지는 유지합니다.

`Report_example`은 이전 512차원 방식으로 저장한 HTML·CSV·SVG와 미리보기입니다. 당시 경로·수치·판정을 보존하며 현재 모델 결과로 해석하지 않습니다. [리포트 설명](../Report_example/readme.md)을 참고합니다.

현재 최종 모델의 학습과 코드를 정리한 뒤 수행한 검증은 [Algorithm의 검증 범위](../src/Algorithm/readme.md#verification--limits)에 구분해 기록했습니다. 예시는 독립 Test 성능을 뜻하지 않습니다. Train/Validation은 같은 손가락의 촬영본 1–6 / 7–8로 나뉘며, 별도 Test 분할은 없습니다. Windows/Linux 실행과 실제 데스크톱 창 조작은 아직 검증하지 않았습니다.

배포 전에는 staged 파일에 로컬 산출물이 들어가지 않았는지, 문서의 상대 링크가 커밋될 파일을 가리키는지, Submodule Commit과 Git LFS 설정이 유지되는지 확인합니다. 저장소를 새로 Clone할 때 필요한 준비 과정은 Root README를 기준으로 따릅니다.
