#include "../Public/BioApi.h"
#include "../Public/FvcSpec.h"
#include "../Public/Score.h"
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QLibrary>
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); }
using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        const QStringList arguments = app.arguments();
        Check(arguments.size() == 3, "DeploymentCheck <installed root> <result.json>");
        const QDir root(arguments[1]);
        Check(QImageReader::supportedImageFormats().contains("tiff") || QImageReader::supportedImageFormats().contains("tif"), "TIFF plugin missing");
        QLibrary dll(root.filePath("FingerprintAlgorithm/FingerprintAlgorithm.dll"));
        Check(dll.load(), "Public algorithm DLL could not be loaded");
        const kisa::ExtractFunction enroll = reinterpret_cast<kisa::ExtractFunction>(dll.resolve("KISA_Enroll"));
        const kisa::ExtractFunction attempt = reinterpret_cast<kisa::ExtractFunction>(dll.resolve("KISA_Attempt"));
        const kisa::CompareFunction compare = reinterpret_cast<kisa::CompareFunction>(dll.resolve("KISA_Compare"));
        Check(enroll && attempt && compare, "Public KISA exports missing");
        QJsonArray rows;
        for (int type = 1; type <= 3; ++type) {
            const kisa::FvcSpec* spec = kisa::Find(type);
            QImage image(root.filePath("data/original/" + QString::fromUtf8(spec->database) + "/101_1.tif"));
            Check(!image.isNull() && image.width() == spec->width && image.height() == spec->height, "Original TIFF could not be decoded");
            image = image.convertToFormat(QImage::Format_Grayscale8);
            std::vector<unsigned char> pixels(std::size_t(spec->width) * spec->height);
            for (int y = 0; y < spec->height; ++y) std::memcpy(pixels.data() + std::size_t(y) * spec->width, image.constScanLine(y), spec->width);
            std::vector<unsigned char> a(kisa::TemplateCapacity), b(kisa::TemplateCapacity);
            int aSize = 0, bSize = 0, error = 0; char message[kisa::ErrorCapacity] = {};
            const Clock::time_point start = Clock::now();
            if (enroll(type, pixels.data(), a.data(), &aSize, &error, message) != 1) throw std::runtime_error(message);
            const double enrollMs = Ms(start);
            if (attempt(type, pixels.data(), b.data(), &bSize, &error, message) != 1) throw std::runtime_error(message);
            Check(aSize > 0 && aSize < kisa::TemplateCapacity && aSize == bSize && std::memcmp(a.data(), b.data(), aSize) == 0, "Enroll/Attempt bytes differ");
            double score = 0;
            if (compare(a.data(), b.data(), &score, &error, message) != 1) throw std::runtime_error(message);
            Check(kisa::ValidScore(score) && score == 1.0, "Public self-compare score failed");
            rows.append(QJsonObject{{"iType", type}, {"template_bytes", aSize}, {"score", score}, {"enroll_ms", enrollMs}});
        }
        QJsonArray modules; HMODULE handles[1024]; DWORD bytes = 0;
        Check(EnumProcessModules(GetCurrentProcess(), handles, sizeof(handles), &bytes) != 0 && bytes <= sizeof(handles), "Cannot inspect loaded runtime modules");
        for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
            wchar_t path[32768] = {}; DWORD size = GetModuleFileNameW(handles[i], path, 32768);
            if (size) modules.append(QString::fromWCharArray(path, int(size)));
        }
        QSaveFile output(arguments[2]); const QByteArray json = QJsonDocument(QJsonObject{{"all_three_public_abis_passed", true}, {"tiff_plugin", true}, {"databases", rows}, {"loaded_modules", modules}}).toJson();
        Check(output.open(QIODevice::WriteOnly) && output.write(json) == json.size() && output.commit(), "Cannot write deployment results");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
