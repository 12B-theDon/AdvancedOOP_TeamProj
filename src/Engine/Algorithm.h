#pragma once
#include "../Public/Image.h"
#include <string>
#include <vector>

namespace engine {
// Raw image and opaque template boundary. Implementations support concurrent calls.
class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual bool Extract(const fingerprint::Image& image, bool enrollment,
                         std::vector<unsigned char>& bytes, std::string& error) = 0;
    // Optional duration reports DLL execution time separately from IPC.
    virtual bool Compare(const std::vector<unsigned char>& enrollment,
                         const std::vector<unsigned char>& attempt, double& score, std::string& error, double* time_ms = nullptr) = 0;
    virtual void Cancel() = 0;
    // Call after outstanding operations have joined.
    virtual void ResetCancel() = 0;
};
}
