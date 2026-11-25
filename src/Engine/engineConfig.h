#pragma once
#include "../Public/FvcSpec.h"
#include <string>
#include <vector>
namespace engine {
inline constexpr unsigned int Seed = 42;
inline constexpr int RandomCount = 10;
inline constexpr int DefaultTimeoutMs = 30000;
inline constexpr int FirstCallTimeoutMs = 120000;
inline constexpr int StartupTimeoutMs = 30000;
inline constexpr int DefaultConcurrency = 1;
inline constexpr int MaxConcurrency = 16;
inline constexpr double GenuineThreshold = 0.65;
inline constexpr double ImpostorThreshold = 0.65;
inline constexpr double MetricTolerance = 1e-12;
inline constexpr int CsvPrecision = 17;
inline constexpr const char* ImagesFolder = "data/original";
inline constexpr const char* TemplateExtension = ".fpt";
inline constexpr const char* DllModelId = "kisa_dll";
inline const std::vector<std::string> DatabaseIds = {kisa::Databases[0].database, kisa::Databases[1].database, kisa::Databases[2].database};
}
