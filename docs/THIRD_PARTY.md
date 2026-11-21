# 외부 라이브러리·모델 출처

- **DeepPrint**: `tim-rohwedder/fixed-length-fingerprint-extractors`의 재구현을 사용합니다. submodule은 commit `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13`에 고정하며 라이선스는 LGPL-3.0입니다. `third_party/deepprint/LICENSE`, `third_party/deepprint.NOTICE.txt`, 등록된 submodule과 history, `third_party/deepprint.patch`를 보존합니다.
- **InceptionV4**: Remi Cadene의 구현을 사용합니다. BSD-3-Clause 고지는 `third_party/deepprint/flx/models/InceptionV4.py`에 있으며, 패키징할 때 고지 전문도 별도 파일로 포함합니다.
- **LibTorch/PyTorch**: BSD-3-Clause와 함께 제공되는 third-party 고지가 적용됩니다. runtime 배포본에 LICENSE·NOTICE가 있으면 그대로 패키징합니다. 고지가 없는 Windows CPU 2.15.0 ZIP은 같은 버전·build hash의 [공식 Windows CPU wheel](https://download.pytorch.org/whl/cpu/torch/)에서 확보한 고지 61개 전문을 사용합니다. 원문은 [LICENSES.txt](https://github.com/12B-theDon/AdvancedOOP_TeamProj/blob/main/third_party/libtorch-notices/windows-cpu-2.15.0/LICENSES.txt), 배포본 URL·SHA-256·원문별 SHA-256은 [provenance.json](https://github.com/12B-theDon/AdvancedOOP_TeamProj/blob/main/third_party/libtorch-notices/windows-cpu-2.15.0/provenance.json)에 보존합니다. 패키징 시 LibTorch 버전·build hash와 고지 SHA-256을 확인합니다.
- **Qt**: 선택한 배포본과 module에 따라 LGPL·GPL·commercial 라이선스가 적용됩니다. 해당 Qt 배포본의 라이선스 파일을 포함하며, Evaluator의 Qt 의존성은 dynamic linking으로 연결합니다.
- **MSVC**: 설치된 toolchain의 재배포 조건에 따라 CMake의 `InstallRequiredSystemLibraries`로 redistributable 파일을 포함합니다.
- **Pretrained checkpoint**: 출처는 `models/pretrained/deepprint.json`에 기록합니다. upstream은 별도의 checkpoint 라이선스를 제공하지 않았습니다. 이 리팩터링으로 해당 파일의 권리를 변경하거나 추가 권리를 주장하지 않습니다.

과제·안내서 PDF와 IndustryAlgorithm 예제는 원래 위치에 로컬 참고 자료로 보관합니다. 상호운용성 규격을 설명하는 자료이며 알고리즘 소스의 빌드 의존성에는 포함되지 않습니다.
