#pragma once

#include <string>
#include <vector>

// Shared model paths and comparison defaults.
namespace engine {
inline constexpr unsigned int Seed = 42; // Shuffle seed.
inline constexpr int RandomCount = 10; // [pairs/enrollment]
inline constexpr int TimeoutMs = 0; // [ms] Disabled; hard timeouts need a worker process.
inline constexpr int Concurrency = 1; // [workers] Current runner is synchronous.
// Uncalibrated fallback only; a matching minimum-score calibration supplies defaults.
inline constexpr double GenuineThreshold = 0.5954;
inline constexpr double ImpostorThreshold = 0.5954;
inline constexpr int ThresholdPrecision = 4; // Calibration rounds upward to this GUI precision.
inline constexpr double TargetFar = 0.01; // Validation operating-point target, not a test guarantee.
inline constexpr double MetricTolerance = 1e-12; // [ratio] Floating-point comparison tolerance.
inline constexpr int CsvPrecision = 17; // Significant digits in raw results.
inline constexpr const char* ImagesFolder = "data/original";
inline constexpr const char* WeightExtension = ".bin";
inline constexpr const char* TemplateExtension = ".fpt";
inline constexpr const char* ScoreType = "probability30_cosine_01";
inline constexpr const char* CalibrationFile = "calibration.json";
inline constexpr const char* CalibrationFormat = "fingerprint-calibration-v3";
inline constexpr const char* ScoreAggregation = "minimum-successful-matching-db-or-combined";
inline const std::vector<std::string> DatabaseIds = {"FVC2000_DB3_B", "FVC2002_DB3_B", "FVC2004_DB1_B"};
inline const std::vector<std::string> ModelIds = {"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"};
} // namespace engine
