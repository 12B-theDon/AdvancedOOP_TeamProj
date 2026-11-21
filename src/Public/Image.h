#pragma once
#include <string>
#include <vector>
namespace fingerprint {
// Tightly packed grayscale image before preprocessing.
struct Image {
    int width = 0; // [px]
    int height = 0; // [px]
    std::vector<unsigned char> pixels; // [gray8] Row-major pixels.
    std::string database_id; // Source DB, independent of the selected model; empty if unknown.
};

}
