// Loads a KISA fingerprint DLL the way an evaluator does (dynamic loading) and runs
// Enroll -> Attempt -> Compare for one enrollment image against attempt images.
// Usage: FingerprintKISATest <dll> <iType> <enroll image> <attempt image> [more attempt images...]
#include "KisaApi.h"
#include <QCoreApplication>
#include <QImage>
#include <QLibrary>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static bool ReadRaw(const QString& path, std::vector<unsigned char>& pixels, int& width, int& height) {
    QImage image(path);
    if (image.isNull()) return false;
    image = image.convertToFormat(QImage::Format_Grayscale8);
    width = image.width();
    height = image.height();
    pixels.resize(std::size_t(width) * height);
    for (int row = 0; row < height; ++row) // QImage rows are 4-byte aligned; the DLL expects packed rows.
        std::memcpy(pixels.data() + std::size_t(row) * width, image.constScanLine(row), std::size_t(width));
    return true;
}

static double Milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr, "Usage: %s <dll> <iType> <enroll image> <attempt image> [...]\n", argv[0]);
        return 2;
    }
    QLibrary library(QString::fromLocal8Bit(argv[1]));
    auto enroll = reinterpret_cast<KISA_ENROLL_FN>(library.resolve("KISA_Enroll"));
    auto attempt = reinterpret_cast<KISA_ATTEMPT_FN>(library.resolve("KISA_Attempt"));
    auto compare = reinterpret_cast<KISA_COMPARE_FN>(library.resolve("KISA_Compare"));
    if (!enroll || !attempt || !compare) {
        std::fprintf(stderr, "Cannot load KISA functions: %s\n", library.errorString().toLocal8Bit().constData());
        return 1;
    }
    const int type = std::atoi(argv[2]);
    std::vector<unsigned char> enrollTemplate(1000000), attemptTemplate(1000000), pixels;
    int width = 0, height = 0, size = 0, code = 0;
    char message[256] = {};

    if (!ReadRaw(QString::fromLocal8Bit(argv[3]), pixels, width, height)) {
        std::fprintf(stderr, "Cannot read %s\n", argv[3]);
        return 1;
    }
    auto start = std::chrono::steady_clock::now();
    int ok = enroll(type, pixels.data(), enrollTemplate.data(), &size, &code, message);
    std::printf("Enroll  %s (%dx%d): return=%d size=%d code=%d %.1f ms %s\n", argv[3], width, height, ok, size, code,
                Milliseconds(start), message);
    if (ok != 1) return 1;

    int failures = 0;
    for (int index = 4; index < argc; ++index) {
        if (!ReadRaw(QString::fromLocal8Bit(argv[index]), pixels, width, height)) {
            std::fprintf(stderr, "Cannot read %s\n", argv[index]);
            ++failures;
            continue;
        }
        start = std::chrono::steady_clock::now();
        ok = attempt(type, pixels.data(), attemptTemplate.data(), &size, &code, message);
        const double attemptTime = Milliseconds(start);
        if (ok != 1) {
            std::printf("Attempt %s: return=%d code=%d %s\n", argv[index], ok, code, message);
            ++failures;
            continue;
        }
        double score = -1;
        start = std::chrono::steady_clock::now();
        ok = compare(enrollTemplate.data(), attemptTemplate.data(), &score, &code, message);
        std::printf("Compare %s: return=%d score=%.6f code=%d attempt %.1f ms, compare %.3f ms %s\n", argv[index], ok,
                    score, code, attemptTime, Milliseconds(start), message);
        if (ok != 1) ++failures;
    }
    return failures == 0 ? 0 : 1;
}
