#pragma once

#include <cstddef>
#include <string>

namespace kisa {
inline constexpr int TemplateCapacity = 1000000;
inline constexpr int ErrorCapacity = 256;
struct FvcSpec { int type; const char* database; int width; int height; };
inline constexpr FvcSpec Databases[] = {
    {1, "FVC2000_DB3_B", 448, 478},
    {2, "FVC2002_DB3_B", 300, 300},
    {3, "FVC2004_DB1_B", 640, 480}
};
inline const FvcSpec* Find(int type) {
    for (const FvcSpec& spec : Databases) if (spec.type == type) return &spec;
    return nullptr;
}
inline int ImageType(const std::string& database, int width, int height) {
    for (const FvcSpec& spec : Databases) {
        if (!database.empty() && database != spec.database && database + "_B" != spec.database) continue;
        if (width == spec.width && height == spec.height) return spec.type;
    }
    return 0;
}
enum class Error { None, InvalidPointer, InvalidType, ModelLoad, Extraction, InvalidTemplate, Comparison, CoreLoad };
} // namespace kisa
