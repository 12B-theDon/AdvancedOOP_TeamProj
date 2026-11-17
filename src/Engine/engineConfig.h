#pragma once

#include <string>
#include <vector>

// Shared model paths and comparison defaults.
namespace EngineConfig {
inline constexpr unsigned int Seed = 42; // Shuffle seed.
inline constexpr int RandomCount = 10; // [pairs/enrollment]
inline constexpr int TimeoutMs = 0; // [ms] Disabled; hard timeouts need a worker process.
inline constexpr int Concurrency = 1; // [workers] Current runner is synchronous.
inline constexpr double DecisionThreshold = 0.75; // [normalized score] Equivalent to cosine >= 0.5.
inline constexpr double MetricTolerance = 1e-12; // [ratio] Floating-point comparison tolerance.
inline constexpr int CsvPrecision = 17; // Significant digits in raw results.
inline constexpr const char* ImagesFolder = "data/original";
inline constexpr const char* WeightsFolder = "models";
inline constexpr const char* WeightExtension = ".bin";
inline constexpr const char* ScoreType = "normalized_cosine_01";
inline const std::vector<std::string> DatabaseIds = {"FVC2000_DB3_B", "FVC2002_DB3_B", "FVC2004_DB1_B"};
inline const std::vector<std::string> ModelIds = {"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"};
}
