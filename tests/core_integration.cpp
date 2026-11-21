// Synthetic, untrained TorchScript fixtures exercise deployment plumbing only.
#include <torch/script.h>
#include "encoder.h"
#include "encoderConfig.h"
#include "../src/Public/BioApi.h"
#include "../src/Public/FvcSpec.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
static void Fixture(const QString& folder, const std::string& id, int index) {
    torch::jit::Module module("SyntheticBoundaryFixture");
    torch::Tensor weight = torch::zeros({1, 30}); weight[0][index] = 3;
    module.register_buffer("weight", weight);
    module.define(R"JIT(
        def preprocess(self, image: Tensor) -> Tensor:
            return image
        def forward(self, image: Tensor) -> Tensor:
            value = torch.mean(image)
            return torch.softmax(self.weight * (1.0 + value), 1)
    )JIT");
    QJsonArray labels;
    for (int i = 0; i < 30; ++i) labels.append(QString("test:%1").arg(i));
    const QJsonObject metadata{{"format", algorithm::ProbabilityFormat}, {"architecture", algorithm::ProbabilityArchitecture},
        {"model_id", QString::fromStdString(id)}, {"embedding_size", 512}, {"output_dim", 30},
        {"preprocessing", algorithm::Preprocessing}, {"labels", labels}, {"label_order_sha256", algorithm::LabelHash(labels)}};
    torch::jit::ExtraFilesMap extras{{"metadata.json", QJsonDocument(metadata).toJson(QJsonDocument::Compact).toStdString()}};
    module.save(QDir(folder).filePath(QString::fromStdString(id) + ".bin").toStdString(), extras);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir deployment; QTemporaryDir elsewhere;
        Check(deployment.isValid() && elsewhere.isValid(), "Temporary folders");
        const QString libraryPath = deployment.filePath(QFileInfo(QString::fromUtf8(argv[1])).fileName());
        Check(QFile::copy(QString::fromUtf8(argv[1]), libraryPath), "Copy Core");
        // Load before creating weights: dlopen must not initialize the models.
        QLibrary library(libraryPath); Check(library.load(), "Load relocated Core");
        const kisa::ExtractFunction enroll = reinterpret_cast<kisa::ExtractFunction>(library.resolve("CoreEnroll"));
        const kisa::ExtractFunction attempt = reinterpret_cast<kisa::ExtractFunction>(library.resolve("CoreAttempt"));
        const kisa::CompareFunction compare = reinterpret_cast<kisa::CompareFunction>(library.resolve("CoreCompare"));
        Check(enroll && attempt && compare, "Core entry points");
        const std::vector<std::string> ids = {"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"};
        for (int i = 0; i < 4; ++i) Fixture(deployment.path(), ids[i], i);
        Check(QDir::setCurrent(elsewhere.path()), "Change current folder");
        algorithm::FingerprintEncoder reference; std::string error;
        for (const std::string& id : ids) Check(reference.LoadModel(id, QDir(deployment.path()).filePath(QString::fromStdString(id) + ".bin").toStdString(), error), "Reference fixture load");
        for (int type = 1; type <= 3; ++type) {
            const kisa::FvcSpec& spec = *kisa::Find(type);
            algorithm::AlgorithmImage first, second;
            first.width = spec.width; first.height = spec.height; first.database_id = spec.database;
            first.pixels.assign(std::size_t(spec.width) * spec.height, 17); second = first;
            second.pixels.assign(second.pixels.size(), 201);
            std::vector<unsigned char> a(kisa::TemplateCapacity + 1), b(kisa::TemplateCapacity + 1);
            a.back() = b.back() = 0xc7;
            int aSize = 0, bSize = 0, code = 0; char message[256] = {};
            Check(enroll(type, first.pixels.data(), a.data(), &aSize, &code, message) == 1, "Core enrollment");
            Check(attempt(type, second.pixels.data(), b.data(), &bSize, &code, message) == 1, "Core attempt");
            Check(aSize > 0 && aSize < kisa::TemplateCapacity && a.back() == 0xc7 && b.back() == 0xc7, "Core buffers");
            double actual = 0, expected = 1;
            Check(compare(a.data(), b.data(), &actual, &code, message) == 1, "Core comparison");
            for (int i : {type - 1, 3}) {
                algorithm::PreparedImage preparedA, preparedB; std::vector<unsigned char> nativeA, nativeB; double value = 0;
                Check(reference.Preprocess(ids[i], first, preparedA, error) && reference.Preprocess(ids[i], second, preparedB, error), "Reference preprocess");
                Check(reference.CreateTemplate(ids[i], preparedA, nativeA, error) && reference.CreateTemplate(ids[i], preparedB, nativeB, error)
                    && reference.Compare(ids[i], nativeA, nativeB, value, error), "Reference scoring");
                expected = std::min(expected, value);
            }
            Check(std::abs(actual - expected) < 1e-12, "Core score aggregation changed");
            a[0] = 0;
            Check(compare(a.data(), b.data(), &actual, &code, message) == 0 && code == 5, "Corrupt envelope accepted");
        }
        // Reusing a process must continue from its cached models after files leave.
        for (const std::string& id : ids) Check(QFile::remove(QDir(deployment.path()).filePath(QString::fromStdString(id) + ".bin")), "Remove synthetic fixture");
        std::vector<unsigned char> image(448 * 478, 0), bytes(kisa::TemplateCapacity);
        int size = 0, code = 0; char message[256] = {};
        Check(enroll(1, image.data(), bytes.data(), &size, &code, message) == 1, "Models reloaded instead of reused");
        std::cout << "Relocated Core, lazy/reused models, independent CWD, all FVC types, template buffers and native score aggregation passed using synthetic untrained fixtures.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
