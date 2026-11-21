# Tests

```sh
ctest --test-dir build -C Release --output-on-failure
```

| 검사 | 범위 |
|---|---|
| `preservation` | 원본 파일·설정·가중치와 학습·비교 계획 코드 보존 |
| `inference_regression` | 세 DB의 전처리·pretrained embedding 기준 hash |
| `evaluation` | 비교 규칙·순서, opaque 템플릿, cache, Stop·교체, 병렬 점수, 지표·보고서 |
| `startup` | 배포 위치 탐색, 모델 누락·LFS 포인터·Worker 감지, 외부 DLL 평가기 호환 |
| `gui_smoke` | Qt offscreen에서 7페이지·DB·옵션·초기 임계값 |
| `core_boundary` / `core_integration` | ABI 경계, 경로 독립성, 지연 로딩·재사용, 버퍼와 점수 집계 |
| `packaging` | 누락 파일 거부, SHA 검증, 출력 경로 보호 |
| `windows_workers` / `windows_public_exports` | Windows 장애 주입·재시작·Stop·IPC·1/4-worker 점수·PE export/import |

Core 통합 검사는 임시 합성 모델을 사용합니다. `preservation`은 기존 `.DS_Store`를 포함한 전체 원본 snapshot을 요구합니다. Python 검사는 3.11+에서 구성되며 Windows 검사는 해당 OS에서만 등록됩니다.

[실행한 결과와 미검증 항목](../docs/verification.md)
