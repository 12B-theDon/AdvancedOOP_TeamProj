#include "kisaBackend.h"
#define _KISA_IMPORT
#include "../KISA/KisaApi.h"
#include "../KISA/kisaConfig.h"
#include <QCryptographicHash>
#include <QFile>
#include <QLibrary>
#include <QString>
#include <cmath>
#include <cstring>
#ifdef _MSC_VER
#include <excpt.h>
#endif

namespace engine {
using algorithm::AlgorithmImage;
using algorithm::AlgorithmModelInfo;
using algorithm::PreparedImage;

struct KisaDllAlgorithm::State {
    std::unique_ptr<QLibrary> library;
    AlgorithmModelInfo info;
    KISA_ENROLL_FN enroll = nullptr;
    KISA_ATTEMPT_FN attempt = nullptr;
    KISA_COMPARE_FN compare = nullptr;
};

namespace {
// Structured-exception guards: no C++ objects with destructors may live in these frames.
int GuardedExtract(KISA_ENROLL_FN function, int type, unsigned char* image, unsigned char* out, int* size,
                   int* code, char* message, bool* crashed) {
#ifdef _MSC_VER
    __try { return function(type, image, out, size, code, message); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *crashed = true; return 0; }
#else
    (void)crashed;
    return function(type, image, out, size, code, message);
#endif
}

int GuardedCompare(KISA_COMPARE_FN function, unsigned char* enrollment, unsigned char* attempt, double* score,
                   int* code, char* message, bool* crashed) {
#ifdef _MSC_VER
    __try { return function(enrollment, attempt, score, code, message); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *crashed = true; return 0; }
#else
    (void)crashed;
    return function(enrollment, attempt, score, code, message);
#endif
}

const KisaConfig::Sensor* SensorBySize(int width, int height) {
    for (const auto& sensor : KisaConfig::Sensors)
        if (sensor.width == width && sensor.height == height) return &sensor;
    return nullptr;
}

std::string Failure(const char* function, int result, int code, const char* message) {
    std::string text = std::string(function) + " returned " + std::to_string(result) + " (error code " + std::to_string(code) + ")";
    const std::string detail(message, strnlen(message, 256));
    return detail.empty() ? text + "." : text + ": " + QString::fromLocal8Bit(detail.c_str()).toStdString();
}
}

KisaDllAlgorithm::KisaDllAlgorithm() : state_(new State) {}
KisaDllAlgorithm::~KisaDllAlgorithm() = default;

bool KisaDllAlgorithm::LoadModel(const std::string&, const std::string& file, std::string& error) {
    error.clear();
    state_->enroll = nullptr;
    state_->info = {};
    state_->attempt = nullptr;
    state_->compare = nullptr;
    // A previously loaded DLL stays mapped: its templates may still be cached by the caller.
    auto library = std::make_unique<QLibrary>(QString::fromStdString(file));
    if (!library->load()) {
        error = "Cannot load DLL: " + library->errorString().toStdString();
        return false;
    }
    auto enroll = reinterpret_cast<KISA_ENROLL_FN>(library->resolve("KISA_Enroll"));
    auto attempt = reinterpret_cast<KISA_ATTEMPT_FN>(library->resolve("KISA_Attempt"));
    auto compare = reinterpret_cast<KISA_COMPARE_FN>(library->resolve("KISA_Compare"));
    if (!enroll || !attempt || !compare) {
        error = "DLL does not export KISA_Enroll, KISA_Attempt and KISA_Compare.";
        return false;
    }
    // Record which DLL produced the results (raw export and report list the model info).
    AlgorithmModelInfo info;
    info.format = "kisa-fingerprint-dll";
    info.preprocessing = "dll-internal";
    QFile binary(QString::fromStdString(file));
    if (binary.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&binary)) info.weight_sha256 = hash.result().toHex().toStdString();
    }
    state_->library = std::move(library);
    state_->info = info;
    state_->enroll = enroll;
    state_->attempt = attempt;
    state_->compare = compare;
    return true;
}

bool KisaDllAlgorithm::GetModelInfo(const std::string&, AlgorithmModelInfo& info, std::string& error) const {
    info = {};
    error.clear();
    if (!state_->enroll) { error = "DLL is not loaded."; return false; }
    info = state_->info;
    return true;
}

bool KisaDllAlgorithm::Preprocess(const std::string&, const AlgorithmImage& image, PreparedImage& prepared, std::string& error) {
    prepared = PreparedImage();
    error.clear();
    const auto* sensor = SensorBySize(image.width, image.height);
    if (!sensor) {
        error = "Image size " + std::to_string(image.width) + "x" + std::to_string(image.height)
              + " matches no KISA iType (448x478, 300x300 or 640x480).";
        return false;
    }
    if (!image.database_id.empty() && image.database_id != sensor->database) {
        error = "Image size does not match its DB " + image.database_id + ".";
        return false;
    }
    if (image.pixels.size() != std::size_t(image.width) * image.height) {
        error = "Expected tightly packed gray8 pixels.";
        return false;
    }
    // The DLL does its own preprocessing; pass the raw pixels through unchanged.
    prepared.width = image.width;
    prepared.height = image.height;
    prepared.values.assign(image.pixels.begin(), image.pixels.end());
    return true;
}

bool KisaDllAlgorithm::Extract(bool attempt, const PreparedImage& prepared, std::vector<unsigned char>& data, std::string& error) {
    data.clear();
    error.clear();
    const auto* sensor = SensorBySize(prepared.width, prepared.height);
    auto function = attempt ? state_->attempt : state_->enroll;
    const char* name = attempt ? "KISA_Attempt" : "KISA_Enroll";
    if (!function) { error = "DLL is not loaded."; return false; }
    if (!sensor || prepared.values.size() != std::size_t(prepared.width) * prepared.height) {
        error = "Invalid raw image.";
        return false;
    }
    std::vector<unsigned char> pixels(prepared.values.size());
    for (std::size_t index = 0; index < pixels.size(); ++index) pixels[index] = (unsigned char)prepared.values[index];
    std::vector<unsigned char> buffer(KisaConfig::MaxTemplateSize);
    int size = -1, code = 0;
    char message[256] = {};
    bool crashed = false;
    const int result = GuardedExtract(function, sensor->type, pixels.data(), buffer.data(), &size, &code, message, &crashed);
    if (crashed) { error = std::string(name) + " crashed (access violation or unhandled exception)."; return false; }
    if (result != 1) { error = Failure(name, result, code, message); return false; }
    if (size <= 0 || size >= KisaConfig::MaxTemplateSize) {
        error = std::string(name) + " reported success with an invalid template size " + std::to_string(size) + ".";
        return false;
    }
    data.assign(buffer.begin(), buffer.begin() + size);
    return true;
}

bool KisaDllAlgorithm::CreateTemplate(const std::string&, const PreparedImage& prepared, std::vector<unsigned char>& data, std::string& error) {
    return Extract(false, prepared, data, error);
}

bool KisaDllAlgorithm::CreateAttemptTemplate(const std::string&, const PreparedImage& prepared, std::vector<unsigned char>& data, std::string& error) {
    return Extract(true, prepared, data, error);
}

bool KisaDllAlgorithm::Compare(const std::string&, const std::vector<unsigned char>& enrollment,
                               const std::vector<unsigned char>& attempt, double& score, std::string& error) {
    score = 0;
    error.clear();
    if (!state_->compare) { error = "DLL is not loaded."; return false; }
    // KISA_Compare takes non-const buffers; give it private copies.
    std::vector<unsigned char> enrolled(enrollment), probe(attempt);
    int code = 0;
    char message[256] = {};
    bool crashed = false;
    double value = std::nan("");
    const int result = GuardedCompare(state_->compare, enrolled.data(), probe.data(), &value, &code, message, &crashed);
    if (crashed) { error = "KISA_Compare crashed (access violation or unhandled exception)."; return false; }
    if (result != 1) { error = Failure("KISA_Compare", result, code, message); return false; }
    if (!(value >= 0.0 && value <= 1.0)) {
        error = "KISA_Compare reported success with a score outside [0, 1].";
        return false;
    }
    score = value;
    return true;
}

} // namespace engine
