#include "encoder.h"
#include "encoderConfig.h"
#include <torch/script.h>
#include <torch/torch.h>
#include <ATen/ops/_upsample_bilinear2d_aa.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>

struct FingerprintEncoder::State {
    struct Model { std::string id; torch::jit::Module module; };
    std::vector<Model> models;
    torch::jit::Module& Find(const std::string& id) {
        for (auto& model : models) if (model.id == id) return model.module;
        throw std::runtime_error("Model is not loaded: " + id);
    }
};

FingerprintEncoder::FingerprintEncoder() : state_(new State) {
    torch::set_num_threads(EncoderConfig::Threads);
}
FingerprintEncoder::~FingerprintEncoder() = default;

bool FingerprintEncoder::LoadModel(const std::string& id, const std::string& file, std::string& error) {
    error.clear();
    try {
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        auto module = torch::jit::load(file, torch::kCPU, extras);
        const auto metadata = QJsonDocument::fromJson(QByteArray::fromStdString(extras["metadata.json"])).object();
        const auto preprocessing = metadata.value("preprocessing").toString();
        if (metadata.value("format").toString() != "deepprint-torchscript-v1"
            || metadata.value("architecture").toString() != EncoderConfig::Architecture
            || (preprocessing != EncoderConfig::Preprocessing && preprocessing != EncoderConfig::CompatibleArchivePreprocessing)
            || metadata.value("embedding_size").toInt() != EncoderConfig::EmbeddingSize
            || metadata.value("model_id").toString().toStdString() != id)
            throw std::runtime_error("Incompatible model metadata or model ID.");
        module.eval();
        torch::NoGradGuard guard;
        auto sample = module.forward({torch::zeros({1, 1, EncoderConfig::InputSize, EncoderConfig::InputSize})}).toTensor();
        if (sample.sizes() != torch::IntArrayRef({1, EncoderConfig::EmbeddingSize}) || !torch::isfinite(sample).all().item<bool>())
            throw std::runtime_error("Invalid encoder output.");
        for (auto& model : state_->models) {
            if (model.id == id) { model.module = module; return true; }
        }
        state_->models.push_back({id, module});
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::Preprocess(const std::string& id, const AlgorithmImage& image,
                                    PreparedImage& prepared, std::string& error) {
    prepared = PreparedImage();
    error.clear();
    try {
        if (image.width <= 0 || image.height <= 0
            || image.pixels.size() != std::size_t(image.width) * image.height)
            throw std::runtime_error("Expected tightly packed gray8 image.");
        torch::NoGradGuard guard;
        auto& model = state_->Find(id);
        auto tensor = torch::from_blob(const_cast<unsigned char*>(image.pixels.data()), {1, 1, image.height, image.width}, torch::kUInt8)
                          .to(torch::kFloat32).div(double(EncoderConfig::GrayMaximum));
        if (image.database_id == EncoderConfig::CenterCropDatabase) {
            const int side = image.width < image.height ? image.width : image.height;
            const int left = (image.width - side) / 2, top = (image.height - side) / 2;
            tensor = tensor.slice(2, top, top + side).slice(3, left, left + side);
        } else {
            const int side = image.width > image.height ? image.width : image.height;
            const int left = (side - image.width) / 2, top = (side - image.height) / 2;
            tensor = torch::constant_pad_nd(tensor, {left, side - image.width - left, top, side - image.height - top}, 1.0);
        }
        tensor = at::_upsample_bilinear2d_aa(tensor, {EncoderConfig::InputSize, EncoderConfig::InputSize}, false, {}, {});
        tensor = model.run_method("preprocess", tensor).toTensor().contiguous();
        if (tensor.numel() != EncoderConfig::InputSize * EncoderConfig::InputSize || !torch::isfinite(tensor).all().item<bool>())
            throw std::runtime_error("Invalid preprocessed input.");
        prepared.width = prepared.height = EncoderConfig::InputSize;
        prepared.values.assign(tensor.data_ptr<float>(), tensor.data_ptr<float>() + tensor.numel());
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::CreateTemplate(const std::string& id, const PreparedImage& prepared,
                                        std::vector<unsigned char>& data, std::string& error) {
    data.clear();
    error.clear();
    try {
        if (prepared.width != EncoderConfig::InputSize || prepared.height != EncoderConfig::InputSize
            || prepared.values.size() != EncoderConfig::InputSize * EncoderConfig::InputSize)
            throw std::runtime_error("Expected a preprocessed 299x299 image.");
        torch::NoGradGuard guard;
        auto input = torch::from_blob(const_cast<float*>(prepared.values.data()), {1, 1, prepared.height, prepared.width}, torch::kFloat32);
        auto embedding = state_->Find(id).forward({input}).toTensor().contiguous();
        if (embedding.numel() != EncoderConfig::EmbeddingSize || !torch::isfinite(embedding).all().item<bool>()
            || embedding.norm().item<double>() <= EncoderConfig::NormEpsilon)
            throw std::runtime_error("Invalid fingerprint embedding.");
        data.resize(EncoderConfig::EmbeddingSize * sizeof(float));
        std::memcpy(data.data(), embedding.data_ptr<float>(), data.size());
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::Compare(const std::string& id, const std::vector<unsigned char>& enrollment,
                                 const std::vector<unsigned char>& attempt, double& score, std::string& error) {
    score = 0;
    error.clear();
    try {
        state_->Find(id);
        const std::size_t bytes = EncoderConfig::EmbeddingSize * sizeof(float);
        if (enrollment.size() != bytes || attempt.size() != bytes) throw std::runtime_error("Invalid template size.");
        double dot = 0, normA = 0, normB = 0;
        for (int index = 0; index < EncoderConfig::EmbeddingSize; ++index) {
            float a, b;
            std::memcpy(&a, enrollment.data() + index * sizeof(float), sizeof(float));
            std::memcpy(&b, attempt.data() + index * sizeof(float), sizeof(float));
            if (!std::isfinite(a) || !std::isfinite(b)) throw std::runtime_error("Non-finite template.");
            dot += double(a) * b; normA += double(a) * a; normB += double(b) * b;
        }
        if (normA <= EncoderConfig::NormEpsilon || normB <= EncoderConfig::NormEpsilon) throw std::runtime_error("Zero-norm template.");
        score = dot / std::sqrt(normA * normB);
        if (score < -1) score = -1;
        if (score > 1) score = 1;
        score = (score + 1.0) / 2.0;
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
