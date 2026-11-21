Fingerprint Evaluator — Windows

FingerprintGUI.exe, FingerprintWorker.exe와 Qt/runtime DLL·plugins를 함께 둡니다.
소스 저장소에서 자사 DLL·모델이 누락되면 LFS 다운로드를 우선 시도하고,
필요한 경우 학습·패키징 후 GUI를 다시 실행합니다. 독립 배포 시에는
FingerprintAlgorithm/과 FingerprintEvaluator/를 함께 복사합니다.
DB → 등록 이미지 → DLL·옵션·Load·템플릿 생성 → 비교 종류 → 규칙 → 실행 → 결과 순서입니다.
DB 폴더는 제공된 FVC 크기의 TIFF를 사용합니다. Step 3에서 완전한 배포 폴더의
알고리즘 DLL(*.dll)은 파일명에 관계없이 선택하고 worker 수·timeout을 설정합니다.
Load selected DLL with these settings를 눌러 Initialization에 Loaded가 표시되면
Create enrollment templates를 누릅니다. DLL과 worker의 bit 수·KISA ABI가 호환되어야 합니다.
선택·설정 변경만으로는 로드하지 않으며, 변경 후에는 Load를 다시 눌러 적용합니다.
Load 실패 시 Error와 원인을 표시하고 이미 시작한 Worker도 정리합니다.

Load·템플릿 생성·비교·보고서 저장은 진행률과 Stop을 제공합니다.
Stop은 생성·비교 중인 결과를 버리며, 보고서 저장 중에는 미완성 파일만 삭제합니다.
완료된 평가·기존 보고서는 보존합니다. 템플릿 표에는 생성 시간(ms)도 표시합니다.
Results에서 임계값(초기 0.5)을 조절하고,
원시 CSV·선택 지표의 HTML·Word 호환 RTF·별도 SVG 그래프를 저장합니다.
DLL·설정 변경 후에는 Load와 템플릿 생성을 다시 수행합니다.

파일 검사: python verify_manifest.py .
라이선스: licenses/. 검증 상태: 소스 저장소의 docs/verification.md.
