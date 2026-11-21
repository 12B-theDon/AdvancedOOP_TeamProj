#include <torch/script.h>
#include "encoder.h"
#include "Engine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QDir root(QString::fromUtf8(argv[1]));
    algorithm::FingerprintEncoder encoder; std::string error;
    const std::string path = root.filePath("models/pretrained/deepprint.bin").toStdString();
    if (!encoder.LoadModel("pretrained", path, error)) { std::cerr << error; return 1; }
    torch::jit::Module module = torch::jit::load(path); module.eval(); torch::NoGradGuard guard;
    QFile file(root.filePath("docs/baseline/inference.json"));
    if (!file.open(QIODevice::ReadOnly)) return 2;
    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject expected = value.toObject();
        const QString db = expected.value("database").toString();
        const fingerprint::Image image = engine::Engine::ReadImage(root.filePath("data/original/" + db + "/101_1.tif").toStdString(), db.toStdString());
        algorithm::PreparedImage prepared;
        if (!encoder.Preprocess("pretrained", image, prepared, error)) { std::cerr << error; return 3; }
        torch::Tensor input = torch::from_blob(prepared.values.data(), {1, 1, 299, 299}, torch::kFloat32);
        torch::Tensor embedding = module.forward({input}).toTensor().contiguous();
        const QByteArray pixels(reinterpret_cast<const char*>(prepared.values.data()), static_cast<int>(prepared.values.size() * sizeof(float)));
        const QByteArray output(reinterpret_cast<const char*>(embedding.data_ptr<float>()), static_cast<int>(embedding.numel() * sizeof(float)));
        const QString pixelHash = QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex());
        const QString outputHash = QString::fromLatin1(QCryptographicHash::hash(output, QCryptographicHash::Sha256).toHex());
        if (pixelHash != expected.value("preprocessing_sha256").toString() || outputHash != expected.value("embedding_sha256").toString()) {
            std::cerr << "Inference differs for " << db.toStdString(); return 4;
        }
    }
    std::cout << "Preprocessing and pretrained embeddings match all three baseline hashes.\n";
}
