Fingerprint Evaluator — Windows

FingerprintGUI.exe, FingerprintWorker.exe와 Qt/runtime DLL·plugins를 함께 둡니다.
소스 저장소에서 자사 DLL·모델이 누락되면 LFS 다운로드를 우선 시도하고,
필요한 경우 학습·패키징 후 GUI를 다시 실행합니다. 독립 배포 시에는
FingerprintAlgorithm/과 FingerprintEvaluator/를 함께 복사합니다.
DB → 등록 이미지 → DLL·옵션·템플릿 생성 → 비교 종류 → 규칙 → 실행 → 결과 순서입니다.
DB 폴더는 제공된 FVC 크기의 TIFF를 사용합니다. Step 3에서 완전한 배포 폴더의
알고리즘 DLL을 선택하고 worker 수·timeout을 설정합니다. DLL과 worker의 bit 수를 맞춥니다.

Stop은 해당 실행 결과를 버립니다. Results에서 임계값(초기 0.5)을 조절하고,
원시 CSV·선택 지표의 HTML·Word 호환 RTF·별도 SVG 그래프를 저장합니다.
알고리즘 교체 후에는 템플릿을 다시 생성합니다.

파일 검사: python verify_manifest.py .
라이선스: licenses/. 검증 상태: 소스 저장소의 docs/verification.md.
