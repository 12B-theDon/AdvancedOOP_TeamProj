#pragma once
#include <string>
#include <vector>

// Original validation/calibration settings; evaluator defaults are independent.
namespace training {
inline constexpr int ThresholdPrecision = 4; // Calibration rounds upward to this GUI precision.
inline constexpr double TargetFar = 0.01; // Validation operating-point target, not a test guarantee.
inline constexpr const char* WeightExtension = ".bin";
inline constexpr const char* ScoreType = "probability30_cosine_01";
inline constexpr const char* CalibrationFile = "calibration.json";
inline constexpr const char* CalibrationFormat = "fingerprint-calibration-v3";
inline constexpr const char* ScoreAggregation = "minimum-successful-matching-db-or-combined";
inline const std::vector<std::string> DatabaseIds = {"FVC2000_DB3_B", "FVC2002_DB3_B", "FVC2004_DB1_B"};
inline const std::vector<std::string> ModelIds = {"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"};
}
