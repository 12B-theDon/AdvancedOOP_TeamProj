FingerprintAlgorithm — Windows

FingerprintAlgorithm.dll, FingerprintAlgorithmCore.dll, runtime DLL과 다음 모델을
같은 폴더에 둡니다: fvc2000_db3.bin, fvc2002_db3.bin, fvc2004_db1.bin, combined.bin.
모델은 DLL 위치에서 지연 로딩하며 프로세스 안에서 재사용합니다.

공개 함수: KISA_Enroll, KISA_Attempt, KISA_Compare. cdecl, 성공 1 / 실패 0.
iType: 1=FVC2000_DB3_B 448×478, 2=FVC2002_DB3_B 300×300,
3=FVC2004_DB1_B 640×480. 과제 PDF의 FVC2004 중복 2 대신 합의한 3을 사용합니다.
입력은 tightly packed 원본 gray8, 출력 버퍼는 1,000,000 bytes입니다.
성공 템플릿 길이는 0보다 크고 1,000,000보다 작으며, 오류 메시지 버퍼는 256 bytes입니다.
Compare에는 같은 배포본이 반환한 완전한 템플릿을 전달합니다. DLL·모델 교체 시 재생성합니다.

평가는 FingerprintGUI·FingerprintWorker로 실행합니다. 임계값은 Results에서 조절합니다.
파일 검사: python verify_manifest.py .
출처·라이선스: licenses/. LGPL source·patch는 소스 저장소에 보존합니다.
검증 상태: 소스 저장소의 docs/verification.md.
