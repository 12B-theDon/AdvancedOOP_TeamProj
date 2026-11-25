#pragma once

#include "../Public/Image.h"
#include <string>
#include <vector>

namespace algorithm {

using AlgorithmImage = fingerprint::Image;

// Encoder input after deterministic preprocessing.
struct PreparedImage {
    int width = 0; // [px]
    int height = 0; // [px]
    std::vector<float> values; // Row-major normalized pixels.
};

// Metadata of the actually loaded archive, independent of its filename.
struct AlgorithmModelInfo {
    std::string format;
    std::string weight_sha256;
    std::string label_order_sha256;
    std::string preprocessing;
    int output_size = 0;
    std::vector<std::string> labels;
};

// Native encoder/training contract; false returns an error message.
class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual bool LoadModel(const std::string& model_id, const std::string& weight_file,
                           std::string& error) = 0;
    virtual bool GetModelInfo(const std::string& model_id, AlgorithmModelInfo& info,
                              std::string& error) const = 0;
    virtual bool Preprocess(const std::string& model_id, const AlgorithmImage& image,
                            PreparedImage& prepared, std::string& error) = 0;
    virtual bool CreateTemplate(const std::string& model_id, const PreparedImage& prepared,
                                std::vector<unsigned char>& data, std::string& error) = 0;
    // [0,1] Raw cosine of nonnegative probability templates; higher is closer.
    virtual bool Compare(const std::string& model_id, const std::vector<unsigned char>& enrollment,
                         const std::vector<unsigned char>& attempt, double& score,
                         std::string& error) = 0;
};

} // namespace algorithm
