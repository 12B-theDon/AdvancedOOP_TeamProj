#pragma once

#include <string>
#include <vector>

// Tightly packed grayscale image before preprocessing.
struct AlgorithmImage {
    int width = 0; // [px]
    int height = 0; // [px]
    std::vector<unsigned char> pixels; // [gray8] Row-major pixels.
    std::string database_id; // Source DB, independent of the selected model; empty if unknown.
};

// Encoder input after deterministic preprocessing.
struct PreparedImage {
    int width = 0; // [px]
    int height = 0; // [px]
    std::vector<float> values; // Row-major normalized pixels.
};

// Engine-facing contract; false returns an error message.
class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual bool LoadModel(const std::string& model_id, const std::string& weight_file,
                           std::string& error) = 0;
    virtual bool Preprocess(const std::string& model_id, const AlgorithmImage& image,
                            PreparedImage& prepared, std::string& error) = 0;
    virtual bool CreateTemplate(const std::string& model_id, const PreparedImage& prepared,
                                std::vector<unsigned char>& data, std::string& error) = 0;
    // [0,1] Normalized similarity; higher is closer. Not a calibrated probability.
    virtual bool Compare(const std::string& model_id, const std::vector<unsigned char>& enrollment,
                         const std::vector<unsigned char>& attempt, double& score,
                         std::string& error) = 0;
};
