# KISA DLL

[Root](../../readme.md) · [Algorithm](../Algorithm/readme.md)

K-NBTC 지문인식 성능시험의 DLL 함수 규격(「생체인식시스템 시험·인증 안내서」 25.11월, pp.26-29)에 맞춘 `FingerprintKISA.dll`이다. 내부에서는 [Algorithm](../Algorithm/readme.md)의 `FingerprintEncoder`를 그대로 사용한다.

| 파일 | 역할 |
|---|---|
| [KisaApi.h](KisaApi.h) | Export 함수 3개와 `GetProcAddress`용 함수 포인터 형식 |
| [kisa.cpp](kisa.cpp) | iType별 모델 로드, 특징 추출, Template 형식, 비교 |
| [kisaConfig.h](kisaConfig.h) | iType별 이미지 크기·DB·모델, Template header, Error code |
| [kisaTest.cpp](kisaTest.cpp) | DLL을 동적으로 불러 Enroll → Attempt → Compare를 실행하는 확인용 프로그램 |

## 함수

모든 함수는 성공하면 `1`, 실패하면 `0`을 반환하고 `piErrorCode`, `arErrorMessage`에 이유를 쓴다.

| 함수 | 입력 | 출력 |
|---|---|---|
| `KISA_Enroll` / `KISA_Attempt` | `iType`, RAW gray8 이미지(왼쪽 위부터, Header 없음) | Template, 크기 [bytes] |
| `KISA_Compare` | Enroll Template, Attempt Template | 0~1 점수 (높을수록 같은 손가락) |

| iType | DB | 크기 (W×H) | 우선 모델 |
|---|---|---|---|
| 1 | FVC2000_DB3_B | 448×478 | `fvc2000_db3.bin` |
| 2 | FVC2002_DB3_B | 300×300 | `fvc2002_db3.bin` |
| 3 | FVC2004_DB1_B | 640×480 | `fvc2004_db1.bin` |

## 모델 위치

DLL과 같은 폴더의 `models/`(없으면 DLL 폴더)에서 iType마다 `<우선 모델>.bin` → `combined.bin` 순으로 찾는다. `FingerprintTrain`이 만든 Cosine30 v2 모델만 쓸 수 있고, `deepprint.bin`(사전학습)은 템플릿을 만들 수 없어 사용하지 않는다. 모델은 iType별로 처음 호출될 때 한 번만 로드하므로 첫 Enroll은 1~2초 더 걸린다.

DLL은 iType의 DB 모델 하나로 점수를 낸다. GUI의 내장 모델 방식에서 해당 DB 모델과 통합 모델을 함께 고르면 둘 중 최솟값으로 판정하므로, 그 설정과는 점수가 다를 수 있다.

## Template

16 bytes Header(`DPK2`, iType, 본문 크기, 예약) + Encoder의 버전 있는 템플릿(모델 ID·Weight 해시·Label 순서·확률 30개)이다. `KISA_Compare`는 크기를 받지 않으므로 Header로 형식과 iType을 확인하고, 서로 다른 iType의 템플릿 비교는 거절한다. 본문은 Encoder가 다시 검사하므로 다른 Weight로 만든 템플릿도 거절된다.

## Error code

| Code | 의미 |
|---|---|
| 1 | Null 포인터 |
| 2 | 지원하지 않는 iType |
| 3 | 모델 파일 없음 또는 로드 실패 |
| 4 / 5 | 전처리 / 특징 추출 실패 |
| 6 / 7 | 이 DLL의 Template이 아님 / iType 불일치 |
| 8 / 9 | 비교 실패 / 예상하지 못한 예외 |

## 빌드와 확인 (Windows)

```bat
cmake --build build-vs --config Release --target FingerprintKISA FingerprintKISATest
copy /Y "C:\deps\libtorch\lib\*.dll" "build-vs\Release\"
"C:\deps\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --release build-vs\Release\FingerprintKISATest.exe
mkdir build-vs\Release\models & copy models\run_01\*.bin build-vs\Release\models\
build-vs\Release\FingerprintKISATest.exe build-vs\Release\FingerprintKISA.dll 2 data\original\FVC2002_DB3_B\101_1.tif data\original\FVC2002_DB3_B\101_2.tif data\original\FVC2002_DB3_B\102_1.tif
```

배포할 때는 DLL 옆에 LibTorch DLL, `Qt6Core.dll`, `models/`가 함께 있어야 한다. LibTorch가 64-bit만 지원하므로 **x64 DLL**이다.
