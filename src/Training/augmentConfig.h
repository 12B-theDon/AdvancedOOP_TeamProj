#pragma once

// Offline image generation; inference never uses these settings.
namespace algorithm {
inline constexpr const char* FolderPattern = "crop_first_%1_per_image_seed%2"; // Copies and seed; numbered on setting changes.
inline constexpr int Version = 2; // Cache format revision.
inline constexpr const char* SourceGeometry = "fvc2004-center-square-before-augmentation";
inline constexpr unsigned int AugmentSeed = 42; // Reproducible augmentation seed.
inline constexpr int CopiesPerOriginal = 12; // [images/original]
// Independent probabilities; enabled changes are composed in the listed order.
inline constexpr double RotationProbability = 0.9; // [probability]
inline constexpr double RotationDegrees = 15.0; // [deg] Symmetric rotation limit.
inline constexpr double ScaleProbability = 0.9; // [probability]
inline constexpr double ScaleMin = 0.90; // [ratio] Minimum magnification.
inline constexpr double ScaleMax = 1.10; // [ratio] Maximum magnification.
inline constexpr double Fvc2004ScaleMin = 1.00; // [ratio] FVC2004 lower limit.
inline constexpr double Fvc2004ScaleMax = 1.30; // [ratio] FVC2004 upper limit.
inline constexpr double NoiseProbability = 0.7; // [probability]
inline constexpr double NoiseStdMin = 1.0; // [gray8] Minimum noise sigma.
inline constexpr double NoiseStdMax = 80.0; // [gray8] Maximum noise sigma.
inline constexpr double BrightnessProbability = 0.8; // [probability]
inline constexpr double BrightnessMin = 0.90; // [ratio] Minimum pixel gain.
inline constexpr double BrightnessMax = 1.10; // [ratio] Maximum pixel gain.
inline constexpr int ForegroundThreshold = 235; // [gray8] Dark foreground cutoff.
inline constexpr double MaxClippedForeground = 0.05; // [ratio] Maximum lost foreground.
inline constexpr int GeometryAttempts = 32; // Retries before identity geometry.
} // namespace algorithm
