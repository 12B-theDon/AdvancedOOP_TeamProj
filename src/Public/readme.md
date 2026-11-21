# 공개 KISA 계약

`BioApi.h`는 Windows용 `KISA_Enroll`, `KISA_Attempt`, `KISA_Compare` 세 함수를 선언합니다. C linkage·cdecl을 사용하며 성공은 1, 실패는 0입니다.

- 입력: header·행 padding이 없는 원본 gray8 픽셀입니다.
- 출력: Template buffer는 1,000,000 bytes이며 실제 길이는 `0 < size < 1,000,000`입니다. 오류 코드와 256-byte 메시지를 함께 반환합니다.
- Compare: 길이 인자가 없으므로 같은 DLL이 생성한 완전한 Template을 전달합니다.

`FvcSpec.h`의 매핑은 다음과 같습니다. 크기는 가로×세로입니다.

| iType | DB | 크기 |
|---|---|---|
| 1 | FVC2000_DB3_B | 448×478 |
| 2 | FVC2002_DB3_B | 300×300 |
| 3 | FVC2004_DB1_B | 640×480 |

과제 PDF는 FVC2004에도 2를 중복 표기하지만, 이 프로젝트는 합의한 3을 사용합니다. 안내서의 물리 센서 코드는 적용하지 않습니다.

`Image.h`는 gray8 이미지 자료형입니다. 규격 원문은 루트 과제 PDF와 K-NBTC 안내서 pp.26–31, 제공된 fingerprint DLL 예제를 참고합니다.
