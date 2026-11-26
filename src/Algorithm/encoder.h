#pragma once

#include "Algorithm.h"
#include "archive.h"
#include <memory>
#include <cstdint>

class QString;
class QByteArray;
class QJsonArray;
namespace at { class Tensor; }
namespace torch { namespace jit { struct Module; } }

namespace algorithm {

struct EncoderOptions {
    bool shared_features = true;
    bool overlap_hash = false;
    bool parallel_loading = false;
    bool representative_initialization = false;
};
struct EncoderProfile {
    double archive_ms = 0, hash_ms = 0, validation_ms = 0, compatibility_ms = 0;
    double preprocess_ms = 0, features_ms = 0, heads_ms = 0;
    std::uint64_t shared_images = 0, legacy_model_calls = 0;
    std::string sharing_status;
};
struct TemplateResult {
    bool success = false;
    std::vector<unsigned char> data;
    std::string error;
};
// DeepPrint loading, shared preprocessing and normalized cosine matching.
class FingerprintEncoder final : public Algorithm {
public:
    explicit FingerprintEncoder(const EncoderOptions& options = EncoderOptions{});
    FingerprintEncoder(const FingerprintEncoder&) = delete;
    FingerprintEncoder& operator=(const FingerprintEncoder&) = delete;
    ~FingerprintEncoder() override;
    bool LoadModel(const std::string& model_id, const std::string& weight_file, std::string& error) override;
    // Model construction is independent; registration happens on the calling thread.
    std::vector<bool> LoadModels(const std::vector<std::string>& model_ids,
                                const std::vector<std::string>& weight_files,
                                std::vector<std::string>& errors,
                                const AlgorithmImage* initialization_image = nullptr);
    std::vector<TemplateResult> CreateTemplates(const std::vector<std::string>& model_ids,
                                               const AlgorithmImage& image);
    EncoderProfile Profile() const;
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
