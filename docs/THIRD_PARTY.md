# Third-party attribution

- DeepPrint reimplementation: tim-rohwedder/fixed-length-fingerprint-extractors, pinned submodule commit7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13, LGPL-3.0. Preserve `third_party/deepprint/LICENSE`, `third_party/deepprint.NOTICE.txt`, the registered submodule/history and `third_party/deepprint.patch`.
- InceptionV4: Remi Cadene, BSD-3-Clause notice in `third_party/deepprint/flx/models/InceptionV4.py`; packaging includes that complete notice separately.
- LibTorch/PyTorch: BSD-3-Clause plus bundled third-party notices. Package the LICENSE/NOTICE files from the exact runtime distribution used to build.
- Qt: LGPL/GPL/commercial according to the selected distribution/modules. Package the license files from that Qt distribution; evaluator Qt dependencies are dynamically linked.
- MSVC redistributable files are included through CMake InstallRequiredSystemLibraries under the installed toolchain's redistribution terms.
- Pretrained checkpoint provenance is in `models/pretrained/deepprint.json`; upstream did not supply a separate checkpoint license. This refactor does not change or assert additional rights to it.

Reference assignment/guide PDFs and IndustryAlgorithm examples remain in their original locations. They specify interoperability and are not algorithm source dependencies.
