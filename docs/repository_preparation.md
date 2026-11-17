# Repository Setup

Repository: https://github.com/12B-theDon/AdvancedOOP_TeamProj
Branch: `main`.

## Clone / Dependencies

Use `git clone --recurse-submodules` to include the external encoder source.

| Item | Value |
|---|---|
| Submodule | `third_party/deepprint` |
| URL | `https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors.git` |
| Pinned commit | `7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13` |
| Local changes | [deepprint.patch](../third_party/deepprint.patch) |
| Attribution | [deepprint.NOTICE.txt](../third_party/deepprint.NOTICE.txt) |

The submodule is a mode-160000 Git entry, not copied source files.
Before one-time checkpoint conversion, apply the tracked patch with
`git -C third_party/deepprint apply ../deepprint.patch` once. This preserves the
previous import cleanup and constant-image normalization. Keep the patch in the
main repository; do not commit it to the upstream project.

Qt and LibTorch are installed separately. The whole `models` directory, including
pretrained checkpoints and fine-tuned weights, is excluded. Prepare checkpoints
using the [Algorithm instructions](../src/Algorithm/readme.md#conversion--offline-run).

## Included / Excluded

- Included: source, CMake, original TIFF DBs, diagrams, GUI_flows, and Report_example.
- Excluded: `build*`, `out`, `data/augmented`, `model`, `models`, IDE and Python caches.
- Report template binaries are small output feature vectors, not encoder weights.
- Local models and augmented data remain on the working machine.

## Examples / Validation

GUI_flows contains current seven-step captures made on 2026-11-18 with real
retrained models. See [capture settings](../GUI_flows/readme.md).

Report_example contains the real single-enrollment run, selected-metric HTML,
all-metric CSV, raw pair/model scores, vector graphs, and an updated preview.
Paths are relative. See [report settings](../Report_example/readme.md).

Examples demonstrate program behavior and are not independent Test-set performance.
Train/Validation use captures 1-6 / 7-8 of the same fingers; no separate Test split.

Before publication, inspect the staged tree for excluded paths, check the pinned
submodule, confirm all relative links in a fresh recursive clone, and build the
C++ targets. Windows execution remains unverified.
