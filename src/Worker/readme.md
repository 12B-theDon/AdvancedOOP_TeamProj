# Windows worker

`FingerprintWorker.exe`가 선택 DLL을 로드하고 KISA 함수를 호출합니다. `Protocol.h`는 원본 픽셀·실제 길이의 템플릿과 응답을 전달하는 IPC 규약입니다. 전용 handle로 DLL의 표준 출력을 분리합니다.

![Host와 Windows worker의 IPC·DLL 호출·오류 및 timeout 처리 구조](../../docs/diagrams/worker.png)

Host는 pipe·process·job handle을 소유합니다. 비동기 쓰기·읽기 polling으로 timeout과 Stop을 처리하고, SEH·버퍼 guard·반환값 검사를 수행합니다. 비정상 worker는 종료하고 다음 호출에서 새로 시작합니다.

기본값은 worker 1개(최대 16개), 호출 30초, 새 worker의 첫 호출 최소 120초, 시작 30초입니다. 실패는 `FAIL`(DLL 코드·메시지), `TIMEOUT`, `CRASH`, `MISMATCH`, `CANCELLED`로 구분합니다.

[구조도 SVG 원본](../../docs/diagrams/worker.svg) · [검증 상태](../../docs/verification.md)
