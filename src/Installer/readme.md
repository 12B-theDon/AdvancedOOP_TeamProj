# Installer 모듈

`bootstrap.cpp`는 Windows 기본 API와 static C++ runtime으로 실행하는 `install.exe`입니다. 초기 PowerShell 논리를 resource에 포함하며 Qt·LibTorch를 링크하지 않습니다. 원래 EXE를 점유하지 않도록 작업용 복사본에서 설치를 계속합니다.

`CMakeLists.txt`는 애플리케이션과 독립적으로 `FingerprintInstaller`를 빌드합니다. `bootstrap.rc.in`·`install.manifest`는 초기 script와 `asInvoker` 설정을 포함합니다. 실제 의존성·모델 준비는 기존 `tools/setup_windows.ps1`을 재사용합니다.

[실행·로그·독립 빌드·검증 범위](../../docs/installer.md)
