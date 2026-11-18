#pragma once

#include "Algorithm.h"
#include <memory>

class QString;
class QByteArray;
class QJsonArray;
namespace at { class Tensor; }
namespace torch { namespace jit { struct Module; } }

namespace algorithm {

// DeepPrint loading, shared preprocessing and normalized cosine matching.
class FingerprintEncoder final : public Algorithm {
public:
    FingerprintEncoder();
    FingerprintEncoder(const FingerprintEncoder&) = delete;
    FingerprintEncoder& operator=(const FingerprintEncoder&) = delete;
    ~FingerprintEncoder() override;
    bool LoadModel(const std::string& model_id, const std::string& weight_file, std::string& error) override;
    bool GetModelInfo(const std::string& model_id, AlgorithmModelInfo& info, std::string& error) const override;
    bool Preprocess(const std::string& model_id, const AlgorithmImage& image,
                    PreparedImage& prepared, std::string& error) override;
    bool CreateTemplate(const std::string& model_id, const PreparedImage& prepared,
                        std::vector<unsigned char>& data, std::string& error) override;
    bool Compare(const std::string& model_id, const std::vector<unsigned char>& enrollment,
                 const std::vector<unsigned char>& attempt, double& score, std::string& error) override;

private:
    struct State; // Loaded modules, hidden from Engine and GUI headers.
    std::unique_ptr<State> state_; // Sole owner; destroyed with this encoder.
};

// Archive operations shared by the encoder and trainer. Implemented in encoder.cpp.
void Require(bool ok, const QString& message);
QString Hash(const QString& path);
QString LabelHash(const QJsonArray& labels);
void Write(const QString& path, const QByteArray& bytes);
bool Probabilities(const at::Tensor& value, int batch);
// The wrapper shares encoder tensors and switches them to inference mode.
torch::jit::Module RetainClassifier(torch::jit::Module& encoder, const at::Tensor& weight, double scale);
} // namespace algorithm
