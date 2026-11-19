#include "KisaApi.h"
#include "kisaConfig.h"
#include "encoder.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
namespace fs = std::filesystem;
using KisaConfig::Error;
using algorithm::AlgorithmImage;
using algorithm::FingerprintEncoder;
using algorithm::PreparedImage;

// One encoder per iType, loaded on first use. After std::call_once the slot is read-only,
// so concurrent Enroll/Attempt/Compare calls do not race on model state.
struct Slot {
    std::once_flag once;
    std::unique_ptr<FingerprintEncoder> encoder;
    std::string model_id;
    std::string error;
};
std::array<Slot, std::size(KisaConfig::Sensors)> slots;

const KisaConfig::Sensor* FindSensor(int type) {
    for (const auto& sensor : KisaConfig::Sensors)
        if (sensor.type == type) return &sensor;
    return nullptr;
}

std::size_t SlotIndex(const KisaConfig::Sensor& sensor) {
    return std::size_t(&sensor - KisaConfig::Sensors);
}

fs::path ModuleFolder() {
#ifdef _WIN32
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleFolder), &module);
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
        if (length == 0) return fs::current_path();
        if (length < path.size()) { path.resize(length); break; }
        path.resize(path.size() * 2);
    }
    return fs::path(path).parent_path();
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&ModuleFolder), &info) == 0 || !info.dli_fname) return fs::current_path();
    return fs::path(info.dli_fname).parent_path();
#endif
}

// UTF-8 path for LibTorch (u8string() returns std::u8string under C++20).
std::string Utf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

void LoadSlot(const KisaConfig::Sensor& sensor, Slot& slot) {
    const fs::path base = ModuleFolder();
    const std::string preferred[] = {sensor.model, KisaConfig::CombinedModel};
    std::string tried;
    auto encoder = std::make_unique<FingerprintEncoder>();
    for (const fs::path& folder : {base / KisaConfig::ModelFolder, base}) {
        for (const auto& id : preferred) {
            const fs::path file = folder / (id + KisaConfig::WeightExtension);
            if (!fs::exists(file)) continue;
            std::string error;
            if (encoder->LoadModel(id, Utf8(file), error)) {
                slot.encoder = std::move(encoder);
                slot.model_id = id;
                return;
            }
            tried += id + ": " + error + "; ";
        }
    }
    slot.error = tried.empty() ? "No trained model next to the DLL (models/" + std::string(sensor.model)
                                     + ".bin or models/combined.bin from FingerprintTrain)."
                               : "Model load failed: " + tried;
}

Slot* ReadySlot(const KisaConfig::Sensor& sensor, std::string& error) {
    Slot& slot = slots[SlotIndex(sensor)];
    std::call_once(slot.once, [&] { LoadSlot(sensor, slot); });
    if (!slot.encoder) { error = slot.error; return nullptr; }
    return &slot;
}

void Report(int* code, char* message, Error error, const std::string& text) {
    if (code) *code = error;
    if (message) {
        std::memset(message, 0, 256);
        std::strncpy(message, text.c_str(), 255);
    }
}

void PutUint32(unsigned char* out, std::uint32_t value) { std::memcpy(out, &value, sizeof(value)); }
std::uint32_t GetUint32(const unsigned char* in) { std::uint32_t value; std::memcpy(&value, in, sizeof(value)); return value; }

// Enroll and Attempt are identical; the role only differs in naming.
int Extract(int type, unsigned char* image, unsigned char* out, int* size, int* code, char* message) {
    try {
        if (size) *size = 0;
        if (!image || !out || !size) {
            Report(code, message, KisaConfig::InvalidArgument, "Null image, template buffer or size pointer.");
            return 0;
        }
        const auto* sensor = FindSensor(type);
        if (!sensor) {
            Report(code, message, KisaConfig::UnsupportedType, "Unsupported iType " + std::to_string(type) + " (expected 1, 2 or 3).");
            return 0;
        }
        std::string error;
        Slot* slot = ReadySlot(*sensor, error);
        if (!slot) { Report(code, message, KisaConfig::ModelUnavailable, error); return 0; }

        AlgorithmImage input;
        input.width = sensor->width;
        input.height = sensor->height;
        input.database_id = sensor->database;
        input.pixels.assign(image, image + std::size_t(sensor->width) * sensor->height);

        PreparedImage prepared;
        if (!slot->encoder->Preprocess(slot->model_id, input, prepared, error)) {
            Report(code, message, KisaConfig::PreprocessFailed, "Preprocessing failed: " + error);
            return 0;
        }
        std::vector<unsigned char> payload;
        if (!slot->encoder->CreateTemplate(slot->model_id, prepared, payload, error)) {
            Report(code, message, KisaConfig::EncodeFailed, "Feature extraction failed: " + error);
            return 0;
        }
        if (payload.empty() || payload.size() > KisaConfig::MaxPayloadSize) {
            Report(code, message, KisaConfig::Internal, "Encoder template has an unexpected size.");
            return 0;
        }
        const std::size_t total = KisaConfig::HeaderSize + payload.size();
        std::memcpy(out, KisaConfig::Magic, sizeof(KisaConfig::Magic));
        PutUint32(out + 4, std::uint32_t(type));
        PutUint32(out + 8, std::uint32_t(payload.size()));
        PutUint32(out + 12, 0);
        std::memcpy(out + KisaConfig::HeaderSize, payload.data(), payload.size());
        *size = int(total);
        Report(code, message, KisaConfig::Ok, "");
        return 1;
    } catch (const std::exception& exception) {
        Report(code, message, KisaConfig::Internal, std::string("Unexpected error: ") + exception.what());
    } catch (...) {
        Report(code, message, KisaConfig::Internal, "Unexpected error.");
    }
    return 0;
}

// Validates the header and returns the encoder template; the encoder checks the rest.
bool ReadTemplate(const unsigned char* data, int& type, std::vector<unsigned char>& payload) {
    if (std::memcmp(data, KisaConfig::Magic, sizeof(KisaConfig::Magic)) != 0) return false;
    type = int(GetUint32(data + 4));
    const std::uint32_t size = GetUint32(data + 8);
    if (size == 0 || size > KisaConfig::MaxPayloadSize) return false;
    const unsigned char* begin = data + KisaConfig::HeaderSize;
    payload.assign(begin, begin + size);
    return true;
}
}

KISADLL int KISA_Enroll(int iType, unsigned char* pImage, unsigned char* pEnroll, int* piEnrollSize,
                        int* piErrorCode, char arErrorMessage[256]) {
    return Extract(iType, pImage, pEnroll, piEnrollSize, piErrorCode, arErrorMessage);
}

KISADLL int KISA_Attempt(int iType, unsigned char* pImage, unsigned char* pAttempt, int* piAttemptSize,
                         int* piErrorCode, char arErrorMessage[256]) {
    return Extract(iType, pImage, pAttempt, piAttemptSize, piErrorCode, arErrorMessage);
}

KISADLL int KISA_Compare(unsigned char* pEnroll, unsigned char* pAttempt, double* pdScore,
                         int* piErrorCode, char arErrorMessage[256]) {
    try {
        if (pdScore) *pdScore = 0;
        if (!pEnroll || !pAttempt || !pdScore) {
            Report(piErrorCode, arErrorMessage, KisaConfig::InvalidArgument, "Null template or score pointer.");
            return 0;
        }
        int enrollType = 0, attemptType = 0;
        std::vector<unsigned char> enrollment, attempt;
        if (!ReadTemplate(pEnroll, enrollType, enrollment) || !ReadTemplate(pAttempt, attemptType, attempt)) {
            Report(piErrorCode, arErrorMessage, KisaConfig::InvalidTemplate, "Template was not created by this DLL.");
            return 0;
        }
        if (enrollType != attemptType) {
            Report(piErrorCode, arErrorMessage, KisaConfig::TypeMismatch, "Enrollment and attempt templates use different iType values.");
            return 0;
        }
        const auto* sensor = FindSensor(enrollType);
        if (!sensor) {
            Report(piErrorCode, arErrorMessage, KisaConfig::InvalidTemplate, "Template has an unsupported iType.");
            return 0;
        }
        std::string error;
        Slot* slot = ReadySlot(*sensor, error);
        if (!slot) { Report(piErrorCode, arErrorMessage, KisaConfig::ModelUnavailable, error); return 0; }
        double score = 0;
        if (!slot->encoder->Compare(slot->model_id, enrollment, attempt, score, error)) {
            Report(piErrorCode, arErrorMessage, KisaConfig::CompareFailed, "Comparison failed: " + error);
            return 0;
        }
        *pdScore = score;
        Report(piErrorCode, arErrorMessage, KisaConfig::Ok, "");
        return 1;
    } catch (const std::exception& exception) {
        Report(piErrorCode, arErrorMessage, KisaConfig::Internal, std::string("Unexpected error: ") + exception.what());
    } catch (...) {
        Report(piErrorCode, arErrorMessage, KisaConfig::Internal, "Unexpected error.");
    }
    return 0;
}
