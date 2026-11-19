#pragma once

#include "../Algorithm/Algorithm.h"
#include <memory>

namespace engine {

// Adapter: a K-NBTC fingerprint DLL (KISA_Enroll / KISA_Attempt / KISA_Compare) behind the
// Algorithm interface, so Engine, GUI and Report treat it like the built-in encoder.
// LoadModel's weight_file is the DLL path. iType is chosen from the image size
// (FVC2000 448x478 = 1, FVC2002 300x300 = 2, FVC2004 640x480 = 3).
// Calls run in this process: access violations and escaping exceptions inside the DLL
// are reported as failures (MSVC), but hangs and heap corruption are not isolated.
class KisaDllAlgorithm final : public algorithm::Algorithm {
public:
    KisaDllAlgorithm();
    KisaDllAlgorithm(const KisaDllAlgorithm&) = delete;
    KisaDllAlgorithm& operator=(const KisaDllAlgorithm&) = delete;
    ~KisaDllAlgorithm() override;
    bool LoadModel(const std::string& model_id, const std::string& weight_file, std::string& error) override;
    bool GetModelInfo(const std::string& model_id, algorithm::AlgorithmModelInfo& info, std::string& error) const override;
    bool Preprocess(const std::string& model_id, const algorithm::AlgorithmImage& image,
                    algorithm::PreparedImage& prepared, std::string& error) override;
    bool CreateTemplate(const std::string& model_id, const algorithm::PreparedImage& prepared,
                        std::vector<unsigned char>& data, std::string& error) override;
    bool CreateAttemptTemplate(const std::string& model_id, const algorithm::PreparedImage& prepared,
                               std::vector<unsigned char>& data, std::string& error) override;
    bool Compare(const std::string& model_id, const std::vector<unsigned char>& enrollment,
                 const std::vector<unsigned char>& attempt, double& score, std::string& error) override;

private:
    struct State;
    std::unique_ptr<State> state_; // Sole owner of the loaded library.
    bool Extract(bool attempt, const algorithm::PreparedImage& prepared, std::vector<unsigned char>& data, std::string& error);
};

} // namespace engine
