#include "../Algorithm/encoder.h"
#include "../Public/BioApi.h"
#include "../Public/FvcSpec.h"
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <array>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#define CORE_API extern "C" __declspec(dllexport)
#else
#include <dlfcn.h>
#define CORE_API extern "C" __attribute__((visibility("default")))
#endif

namespace {
constexpr quint32 Magic = 0x4b465031;
constexpr int HeaderSize = 12;
const std::array<std::string, 4> ModelIds = {"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"};
struct Bundle {
    int type = 0;
    std::array<std::vector<unsigned char>, 4> templates;
};
int Failure(kisa::Error kind, const std::string& text, int* code, char* message) {
    if (code) *code = static_cast<int>(kind);
    if (message) {
        const std::size_t length = std::min(text.size(), std::size_t(kisa::ErrorCapacity - 1));
        std::memcpy(message, text.data(), length); message[length] = '\0';
    }
    return 0;
}
QString ModuleFolder() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ModuleFolder), &module)) throw std::runtime_error("Cannot locate Core DLL.");
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (length == 0 || length >= 32768) throw std::runtime_error("Invalid Core DLL path.");
    return QFileInfo(QString::fromWCharArray(path, static_cast<int>(length))).absolutePath();
#else
    Dl_info info = {};
    if (!dladdr(reinterpret_cast<void*>(&ModuleFolder), &info)) throw std::runtime_error("Cannot locate algorithm library.");
    return QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
#endif
}
struct State {
    algorithm::FingerprintEncoder encoder;
    bool initialized = false;
    std::array<bool, 4> loaded = {};
    std::array<std::string, 4> errors;
    std::mutex mutex;
    void Load() {
        if (initialized) return;
        const QDir folder(ModuleFolder());
        for (int i = 0; i < 4; ++i)
            loaded[i] = encoder.LoadModel(ModelIds[i], folder.filePath(QString::fromStdString(ModelIds[i]) + ".bin").toStdString(), errors[i]);
        initialized = true;
    }
};
State& SharedState() { static State state; return state; }
QByteArray Encode(const Bundle& bundle) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15); stream.setByteOrder(QDataStream::LittleEndian);
    stream << Magic << quint32(0) << quint32(bundle.type);
    for (const std::vector<unsigned char>& data : bundle.templates) {
        stream << quint32(data.size());
        if (!data.empty()) stream.writeRawData(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    }
    if (stream.status() != QDataStream::Ok || bytes.size() >= kisa::TemplateCapacity) throw std::runtime_error("Template exceeds ABI capacity.");
    stream.device()->seek(4); stream << quint32(bytes.size());
    return bytes;
}
bool Decode(const unsigned char* data, Bundle& bundle) {
    // ABI has no input length. Callers must supply the complete template previously
    // returned by this DLL. Validate all embedded lengths before reading payloads.
    quint32 magic = 0, size = 0, type = 0;
    QDataStream header(QByteArray::fromRawData(reinterpret_cast<const char*>(data), HeaderSize));
    header.setByteOrder(QDataStream::LittleEndian); header >> magic >> size >> type;
    if (magic != Magic || size < HeaderSize + 16 || size >= kisa::TemplateCapacity || !kisa::Find(static_cast<int>(type))) return false;
    const QByteArray bytes(reinterpret_cast<const char*>(data), static_cast<int>(size));
    int offset = HeaderSize;
    for (std::vector<unsigned char>& item : bundle.templates) {
        if (offset + 4 > bytes.size()) return false;
        QDataStream lengthStream(bytes.mid(offset, 4)); lengthStream.setByteOrder(QDataStream::LittleEndian);
        quint32 length = 0; lengthStream >> length; offset += 4;
        if (length > 16384 || length > static_cast<quint32>(bytes.size() - offset)) return false;
        item.assign(data + offset, data + offset + length); offset += static_cast<int>(length);
    }
    bundle.type = static_cast<int>(type);
    return offset == bytes.size();
}
int Extract(int type, unsigned char* image, unsigned char* output, int* size, int* code, char* message) {
    if (size) *size = 0;
    if (code) *code = 0;
    if (message) message[0] = '\0';
    if (!image || !output || !size || !code || !message) return Failure(kisa::Error::InvalidPointer, "Null argument.", code, message);
    const kisa::FvcSpec* spec = kisa::Find(type);
    if (!spec) return Failure(kisa::Error::InvalidType, "Expected FVC iType 1, 2 or 3.", code, message);
    try {
        State& state = SharedState(); std::lock_guard<std::mutex> lock(state.mutex); state.Load();
        if (!state.loaded[type - 1] && !state.loaded[3])
            return Failure(kisa::Error::ModelLoad, "No matching-DB/combined model: " + state.errors[type - 1] + "; " + state.errors[3], code, message);
        algorithm::AlgorithmImage raw;
        raw.width = spec->width; raw.height = spec->height; raw.database_id = spec->database;
        raw.pixels.assign(image, image + std::size_t(spec->width) * spec->height);
        Bundle bundle; bundle.type = type;
        bool decisionAvailable = false;
        std::string error;
        for (int i = 0; i < 4; ++i) {
            if (!state.loaded[i]) { error = state.errors[i]; continue; }
            algorithm::PreparedImage prepared;
            if (!state.encoder.Preprocess(ModelIds[i], raw, prepared, error)
                || !state.encoder.CreateTemplate(ModelIds[i], prepared, bundle.templates[i], error)) continue;
            if (i == type - 1 || i == 3) decisionAvailable = true;
        }
        if (!decisionAvailable) return Failure(kisa::Error::Extraction, "No matching-DB/combined template: " + error, code, message);
        const QByteArray bytes = Encode(bundle);
        std::memcpy(output, bytes.constData(), static_cast<std::size_t>(bytes.size())); *size = bytes.size();
        return 1;
    } catch (const std::exception& e) { return Failure(kisa::Error::ModelLoad, e.what(), code, message); }
      catch (...) { return Failure(kisa::Error::Extraction, "Unknown extraction exception.", code, message); }
}
}
CORE_API int KISA_CALL CoreEnroll(int type, unsigned char* image, unsigned char* output, int* size, int* code, char* message) {
    return Extract(type, image, output, size, code, message);
}
CORE_API int KISA_CALL CoreAttempt(int type, unsigned char* image, unsigned char* output, int* size, int* code, char* message) {
    return Extract(type, image, output, size, code, message);
}
CORE_API int KISA_CALL CoreCompare(unsigned char* enrollment, unsigned char* attempt, double* score, int* code, char* message) {
    if (score) *score = 0;
    if (code) *code = 0;
    if (message) message[0] = '\0';
    if (!enrollment || !attempt || !score || !code || !message) return Failure(kisa::Error::InvalidPointer, "Null argument.", code, message);
    try {
        Bundle a, b;
        if (!Decode(enrollment, a) || !Decode(attempt, b) || a.type != b.type)
            return Failure(kisa::Error::InvalidTemplate, "Invalid or incompatible algorithm template.", code, message);
        State& state = SharedState(); std::lock_guard<std::mutex> lock(state.mutex); state.Load();
        bool success = false;
        double minimum = 1;
        std::string error;
        for (int i : {a.type - 1, 3}) {
            if (!state.loaded[i] || a.templates[i].empty() || b.templates[i].empty()) continue;
            double value = 0;
            if (state.encoder.Compare(ModelIds[i], a.templates[i], b.templates[i], value, error)) {
                success = true; minimum = std::min(minimum, value);
            }
        }
        if (!success) return Failure(kisa::Error::InvalidTemplate, "No compatible matching-DB/combined templates: " + error, code, message);
        *score = minimum; return 1;
    } catch (const std::exception& e) { return Failure(kisa::Error::Comparison, e.what(), code, message); }
      catch (...) { return Failure(kisa::Error::Comparison, "Unknown comparison exception.", code, message); }
}
