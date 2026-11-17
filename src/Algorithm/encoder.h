#pragma once

#include "Algorithm.h"
#include <memory>

// DeepPrint loading, shared preprocessing and normalized cosine matching.
class FingerprintEncoder final : public Algorithm {
public:
    FingerprintEncoder();
    ~FingerprintEncoder() override;
    bool LoadModel(const std::string& model_id, const std::string& weight_file, std::string& error) override;
    bool Preprocess(const std::string& model_id, const AlgorithmImage& image,
                    PreparedImage& prepared, std::string& error) override;
    bool CreateTemplate(const std::string& model_id, const PreparedImage& prepared,
                        std::vector<unsigned char>& data, std::string& error) override;
    bool Compare(const std::string& model_id, const std::vector<unsigned char>& enrollment,
                 const std::vector<unsigned char>& attempt, double& score, std::string& error) override;

private:
    struct State; // Loaded modules, hidden from Engine and GUI headers.
    std::unique_ptr<State> state_;
};
