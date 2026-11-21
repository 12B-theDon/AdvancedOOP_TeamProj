#pragma once

#include <string>
#include <vector>

// Independent fine-tuning starts from the same pretrained checkpoint.
namespace algorithm {
inline constexpr unsigned int TrainSeed = 42; // Model initialization and batch shuffle seed.
inline constexpr int Epochs = 40; // [epochs] Training passes.
inline constexpr int BatchSize = 32; // [images/batch] Projection training.
inline constexpr int FeatureBatchSize = 4; // [images/batch] Frozen feature extraction.
inline constexpr double LearningRate = 0.0001; // Optimizer step size.
inline constexpr double WeightDecay = 0.0001; // L2 penalty coefficient.
inline constexpr double CosineLogitScale = 16.0; // Multiplier before cross entropy.
inline constexpr int LogEveryFeatureBatches = 10; // [batches] Console progress interval.
inline constexpr double ReloadRtol = 1e-5; // Relative reload comparison tolerance.
inline constexpr double ReloadAtol = 1e-6; // Absolute reload comparison tolerance.
inline constexpr double SelfScoreTolerance = 1e-5; // [normalized score] Self-match check.
inline constexpr const char* Optimizer = "Adam"; // Production Partial FT optimizer.
// Split original captures first; every augmentation inherits this assignment.
inline constexpr int ValidationFirstCapture = 7; // First validation capture ID.
// Prefixes of exported encoder parameters. BN and dropout remain in eval mode.
inline const std::vector<std::string> TrainablePrefixes = {
    "texture_projection.", "minutia_projection."
};
} // namespace algorithm
