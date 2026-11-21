#include "../src/Public/BioApi.h"
#include "../src/Public/FvcSpec.h"
#include <QCoreApplication>
#include <QLibrary>
#include <cstring>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QLibrary library(QString::fromUtf8(argv[1]));
    const kisa::ExtractFunction enroll = reinterpret_cast<kisa::ExtractFunction>(library.resolve("CoreEnroll"));
    const kisa::ExtractFunction attempt = reinterpret_cast<kisa::ExtractFunction>(library.resolve("CoreAttempt"));
    const kisa::CompareFunction compare = reinterpret_cast<kisa::CompareFunction>(library.resolve("CoreCompare"));
    if (!enroll || !attempt || !compare) return 1;
    unsigned char pixel = 0;
    std::vector<unsigned char> bytes(kisa::TemplateCapacity + 1, 0);
    bytes.back() = 0x71;
    int size = 99, code = 0; char message[256] = {}; double score = 5;
    if (enroll(0, &pixel, bytes.data(), &size, &code, message) != 0 || size != 0 || code != 2 || bytes.back() != 0x71) return 2;
    if (attempt(1, nullptr, bytes.data(), &size, &code, message) != 0 || code != 1) return 3;
    if (compare(bytes.data(), bytes.data(), &score, &code, message) != 0 || code != 5 || score != 0) return 4;
    if (compare(nullptr, bytes.data(), &score, &code, message) != 0 || code != 1) return 5;
    if (std::memchr(message, '\0', 256) == nullptr) return 6;
    std::cout << "Core exports, null/type/template validation and buffer canary passed without model loading.\n";
}
