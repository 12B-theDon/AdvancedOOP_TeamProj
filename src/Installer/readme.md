# Installer 모듈

`FingerprintSetup.iss`는 설치 위치·바로가기·진행률·완료 후 실행·Windows 앱 제거 기능을 제공하는 Inno Setup 마법사입니다. 완성된 프로그램·runtime DLL·학습 모델을 포함하므로 사용자 PC에서 개발 도구를 설치하거나 빌드하지 않습니다.

`CMakeLists.txt`의 `FingerprintInstaller` target과 `PackageInstaller.cmake`가 깨끗한 배포 payload로 `install.exe`를 만듭니다. `deployment_check.cpp`·`verify_install.ps1`은 설치된 파일만으로 세 DB의 공개 KISA 함수와 실제 GUI·복구·제거 동작을 검사하는 개발용 도구입니다. 일반 배포본에 검사 프로그램을 넣지 않습니다.

[설치·로그·개발자 빌드](../../docs/installer.md)
