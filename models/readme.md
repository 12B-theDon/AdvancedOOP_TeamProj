# 모델 파일

- `pretrained/deepprint.bin`: fine-tuning 초기화에 사용하는 TorchScript encoder입니다. 출처는 같은 폴더의 `deepprint.json`에 기록됩니다.
- `trained_result/`: Git LFS로 배포하는 기본 학습 출력 폴더입니다. `fvc2000_db3.bin`, `fvc2002_db3.bin`, `fvc2004_db1.bin`, `combined.bin`을 포함합니다.

`trained_result/weights.json`은 네 모델의 크기·SHA-256, LibTorch 버전과 학습 noise 설정을 기록합니다. 현재 배포본은 Gaussian noise 확률 70%·σ=[1,80]으로 학습했습니다.

Git clone 후 설치 스크립트가 LFS 파일을 가져오므로 보통 로컬 재학습은 필요하지 않습니다. 학습 로그·feature cache는 Git에 포함하지 않습니다.

패키징 시 `FINGERPRINT_PACKAGE_MODELS`로 학습 출력 폴더를 지정합니다. 배포 폴더에는 네 모델을 알고리즘 DLL 옆에 배치합니다.

[학습](../src/Training/readme.md) · [패키징](../docs/build.md#package) · [검증 상태](../docs/verification.md)
