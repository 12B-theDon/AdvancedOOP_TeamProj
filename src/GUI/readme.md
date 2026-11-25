# GUI·보고서

`startup.cpp`가 Windows 실행 시 이 프로젝트의 배포 DLL·모델 4개·Worker를 확인합니다. 소스 저장소에서 누락이 발견되면 준비 프로그램이 LFS 다운로드를 우선 시도하고, 필요한 경우 학습·패키징을 마친 뒤 GUI를 다시 엽니다. 완성된 배포본과 이 프로젝트의 알고리즘을 포함하지 않은 외부 DLL 전용 평가기는 바로 실행됩니다.

`Window`는 7단계 QWizard와 DLL adapter를 소유합니다. Step 3에서 DLL 선택 → Worker·timeout 설정 → **Load selected DLL with these settings** → Template 생성 순서로 진행합니다. background 실행 중 진행률과 Stop을 제공합니다.

Step 2의 **Single enrollment**는 기존 단일 선택을 유지합니다. **Multiple enrollment**에서만 Batch selection이 나타납니다. 규칙과 설정을 고른 뒤 **Apply selection**을 누르면 목록에 자동 반영되며, 이후 Ctrl / Shift로 수동 조정할 수 있습니다. 기본 규칙은 **지문별 최대 2장·seed 42**입니다.

| 선택 규칙 | 동작 |
|---|---|
| All images | 읽기 실패 TIFF를 포함한 목록 전체 선택 |
| Random N images | 전체에서 중복 없이 최대 N장 선택 |
| Random images per finger | 지문별 중복 없이 최대 K장 선택 |
| Selected capture numbers | 입력한 촬영 번호 선택. 예: `1, 3` |
| Image list from CSV | `image` 열에 지정한 현재 DB의 파일명 선택 |

동일한 DB 목록과 seed는 같은 무작위 선택을 재현합니다. 이미지 수와 포함된 지문 수를 표시하며, **Save selected list CSV...**로 실제 선택 목록을 저장해 다른 DLL에도 재사용할 수 있습니다. 지문·촬영 번호를 모르는 외부 DB에서는 전체·무작위 N장·CSV를 사용합니다. 잘못된 규칙이나 CSV는 기존 선택을 유지하고 이유를 표시합니다. 선택 이미지가 바뀌면 기존 Template·비교 결과가 초기화됩니다. 적용 규칙과 최종 등록 이미지 목록은 결과 저장 시 함께 기록합니다.

`enrollmentSelection.*`는 선택 규칙·seed·CSV 입출력을 처리하며 DLL 호출과 독립적입니다.

읽기 실패 TIFF도 목록에서 제외하지 않습니다. 빨간색 항목·tooltip·DB 경고로 파일과 원인을 표시하고, 실패한 파일은 서로 다른 이미지로 유지합니다. 해당 파일을 등록 대상으로 선택하면 읽기를 다시 시도하며, 계속 읽을 수 없으면 등록 실패로 FTE에 반영합니다. 이미지가 모두 손상된 DB도 실패 건수를 기록할 수 있습니다.

선택 목록 CSV 형식은 다음과 같습니다. Step 5의 비교 쌍 CSV와는 별도의 형식입니다.

```csv
image
101_1.tif
102_3.tif
```

DLL 선택창은 `*.dll`을 보여주며 파일명을 제한하지 않습니다. 다른 팀의 K-NBTC DLL도 그대로 선택합니다. 이 프로젝트의 DLL은 `dist/FingerprintAlgorithm/FingerprintAlgorithm.dll`입니다. 함께 배포된 Core·LibTorch·Qt·Windows runtime DLL은 실행 의존성이므로 폴더에 그대로 둡니다.

DLL 선택·설정 변경만으로는 로드하지 않습니다. **Load selected DLL with these settings**를 누르면 배포 파일·manifest를 검사하고, 설정한 수의 Worker에서 DLL과 KISA exports를 로드합니다. 성공하면 Algorithm / DLL file / Initialization 표에 `Loaded`가 표시되고 **Create enrollment templates**가 활성화됩니다. 실패하면 `Error`와 원인이 표시됩니다. Load에도 진행률·Stop을 제공하며 실패·중단 시 이미 시작한 Worker도 모두 정리합니다. 파일명과 별개로 Windows bitness와 KISA ABI가 호환되어야 합니다.

DLL·Worker 수·timeout을 바꾸거나 DLL 파일 변경을 감지하면 기존 등록·Template·비교 결과를 무효화합니다. 새 설정은 Load를 다시 눌러 적용합니다. DB만 바꾸면 로드된 DLL은 유지하고 Template·비교 결과를 초기화합니다.

**Create enrollment templates**를 누르면 각 이미지의 `Status / Size (bytes) / Error / Load time (ms)`가 표시됩니다. 시간은 Template 생성 요청의 전체 경과 시간이며, DLL 내부의 첫 모델 초기화와 요청 중 필요한 Worker 재시작을 포함합니다. DLL 내부 모델은 구현에 따라 첫 inference에서 초기화될 수 있습니다. 실패한 요청의 시간도 표시합니다. 잘린 메시지는 셀에 마우스를 올려 확인합니다.

Stop은 전체 Worker를 종료하고 생성 중인 Template·비교 결과를 버립니다. GUI는 정리 상태를 표시하며 background 작업과 Worker의 handle 정리가 모두 끝난 뒤 다시 조작할 수 있습니다. 취소된 실행은 CSV·보고서로 저장하지 않습니다. 실행 중 메인 창을 닫으면 종료 여부를 확인하고, 승인 시 같은 취소·정리 절차가 끝난 뒤 창을 닫습니다. 종료 확인 중 작업이 먼저 끝나도 사용자의 선택을 기다립니다.

<img src="../../docs/diagrams/gui.png" alt="GUI의 화면 구성·백그라운드 평가·결과 및 보고서 생성 관계" width="100%">

Results에서 Genuine·Impostor 임계값(초기 모두 `0.30`)을 바꾸면 저장된 점수로 다시 판정합니다. DLL을 새로 Load하면 두 임계값을 초기값으로 되돌립니다. 우리 알고리즘의 raw cosine 점수에 맞춘 시작값입니다. 다른 DLL·노이즈 데이터에는 점수 분포를 확인해 조절하며, 외부 DLL 점수에는 변환을 적용하지 않습니다. EER는 공통 Threshold를 별도로 탐색하므로 초기값 변경으로 달라지지 않습니다. Save는 원시 CSV, 선택 지표의 HTML·Word 호환 RTF, 별도 SVG 그래프를 background에서 생성하며 진행 건수·비율과 Stop을 표시합니다. CSV 행·지표·그래프 계산과 파일 쓰기 중 취소를 확인합니다. 임시 폴더에 작성한 파일은 완료 후 `run_N`으로 저장합니다. 보고서 Stop·창 닫기는 미완성 파일만 폐기하고, 완료된 Template·비교 결과와 기존 보고서는 보존합니다. Stop 후 다시 저장할 수 있습니다.

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

`error_tradeoff.svg`는 과제 예시와 같은 FMR·FNMR 로그축의 DET 그래프입니다. 성공 점수의 모든 고유 Threshold와 최댓값 직후의 reject-all 지점을 사용합니다. 0%는 원시 값 그대로 유지하고, 그림에서만 명시된 로그축 하한에 표시합니다. 표본 수가 크면 더 작은 오류율도 보이도록 하한을 확장합니다. 선택한 FMR100·FMR1000 운영점과 EER 추정값을 함께 표시합니다. EER 표시는 표와 같은 midpoint 추정치를 양축에 사용하므로 실제 단일 Threshold의 점과 다를 수 있습니다.

[구조도 SVG 원본](../../docs/diagrams/gui.svg) · [사용 순서](../../readme.md#2-gui)
