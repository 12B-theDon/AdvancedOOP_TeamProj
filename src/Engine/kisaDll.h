#pragma once
#include "Algorithm.h"
#include "engineConfig.h"
#include <memory>
#include <functional>

namespace engine {
struct DllOptions {
    int workers = DefaultConcurrency;
    int timeout_ms = DefaultTimeoutMs;
    int first_call_timeout_ms = FirstCallTimeoutMs;
    int startup_timeout_ms = StartupTimeoutMs;
};
// Owns one restartable process per slot. No encoder, template decoder or Torch.
class KisaDllAlgorithm final : public Algorithm {
public:
    explicit KisaDllAlgorithm(const std::string& dll_path, const std::string& worker_path, const DllOptions& options = {});
    ~KisaDllAlgorithm() override;
    KisaDllAlgorithm(const KisaDllAlgorithm&) = delete;
    KisaDllAlgorithm& operator=(const KisaDllAlgorithm&) = delete;
    // Load and validate KISA exports in every worker without making an inference call.
    bool Load(std::string& error, const std::function<void()>& worker_ready = {});
    bool Extract(const fingerprint::Image& image, bool enrollment, std::vector<unsigned char>& bytes, std::string& error) override;
    bool Compare(const std::vector<unsigned char>& enrollment, const std::vector<unsigned char>& attempt, double& score, std::string& error, double* time_ms = nullptr) override;
    void Cancel() override;
    bool CancellationFinished() const override;
    void ResetCancel() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
