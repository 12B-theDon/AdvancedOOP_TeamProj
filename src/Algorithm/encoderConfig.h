#pragma once

// DeepPrint reimplementation: https://github.com/tim-rohwedder/fixed-length-fingerprint-extractors
// Upstream architecture/preprocessing: LGPL-3.0; InceptionV4: BSD-3-Clause.
// LibTorch: https://github.com/pytorch/pytorch (BSD-3-Clause).
namespace algorithm {
inline constexpr int InputSize = 299; // [px] Square encoder input.
inline constexpr int EmbeddingSize = 512; // [features] Combined texture + minutia vector.
inline constexpr int TemplateSize = 30; // [probabilities] Retained cosine classifier output.
inline constexpr int TemplateVersion = 2;
inline constexpr double ProbabilityTolerance = 1e-5;
inline constexpr const char* ProbabilityFormat = "deepprint-cosine-probabilities-v2";
inline constexpr const char* ProbabilityArchitecture = "DeepPrint_TexMinu_512_Cosine30";
inline constexpr const char* TemplateFormat = "fingerprint-probability-template-v2";
inline constexpr int FeatureSize = 2560; // [features] Frozen backbone output.
inline constexpr double GaborRidgeWidth = 1.8; // [px] Requires re-export when changed.
inline constexpr int GrayMaximum = 255; // [gray8] Pixel normalization divisor.
inline constexpr int TextureFeatureSize = 1536; // [features] Texture backbone output.
inline constexpr int BranchEmbeddingSize = 256; // [features] Each embedding branch.
inline constexpr double ExportAtol = 1e-5; // Absolute export comparison tolerance.
inline constexpr double ExportRtol = 1e-4; // Relative export comparison tolerance.
inline constexpr int Threads = 4; // [threads] CPU execution.
inline constexpr const char* Architecture = "DeepPrint_TexMinu_512";
// Older archives contain the same encoder/Gabor method; geometry is selected by DB.
inline constexpr const char* CompatibleArchivePreprocessing = "gray01-white-square-aa299-gabor1.8-v1";
inline constexpr const char* Preprocessing = "gray01-fvc2004-center-square-aa299-gabor1.8-v2";
inline constexpr const char* CenterCropDatabase = "FVC2004_DB1_B";
inline constexpr const char* UpstreamCommit = "7accfca1f33b9b42bfd220e43cd5bc13b4a7fa13";
} // namespace algorithm
