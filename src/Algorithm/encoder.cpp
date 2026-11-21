#include "encoder.h"
#include "encoderConfig.h"
#include <torch/script.h>
#include <torch/utils.h>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <ATen/ops/_upsample_bilinear2d_aa.h>
#include <QDataStream>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace algorithm {
void Require(bool ok, const QString& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
QString Hash(const QString& path) {
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Cannot open " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Require(hash.addData(&file), "Cannot hash " + path);
    return QString::fromLatin1(hash.result().toHex());
}
QString LabelHash(const QJsonArray& labels) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(labels).toJson(QJsonDocument::Compact),
                                                     QCryptographicHash::Sha256).toHex());
}
void Write(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    Require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(), "Cannot write " + path);
}
bool Probabilities(const torch::Tensor& value, int batch) {
    return value.sizes() == torch::IntArrayRef({batch, TemplateSize})
        && value.scalar_type() == torch::kFloat32 && torch::isfinite(value).all().item<bool>()
        && (value >= 0).all().item<bool>() && (value <= 1).all().item<bool>()
        && ((value.sum(1) - 1).abs() <= ProbabilityTolerance).all().item<bool>();
}
// LibTorch compiles this wrapper directly. No Python exporter at runtime.
torch::jit::Module RetainClassifier(torch::jit::Module& encoder, const torch::Tensor& weight, double scale) {
    torch::jit::Module result("FingerprintProbabilities");
    result.register_module("encoder", encoder);
    result.register_parameter("classifier_weight", weight.detach().clone(), false);
    result.register_attribute("logit_scale", c10::FloatType::get(), scale);
    result.define(R"JIT(
        def preprocess(self, image: Tensor) -> Tensor:
            return self.encoder.preprocess(image)
        def features(self, image: Tensor) -> Tensor:
            return self.encoder.features(image)
        def embed_features(self, features: Tensor) -> Tensor:
            return self.encoder.embed_features(features)
        def logits_from_features(self, features: Tensor) -> Tensor:
            embedding = self.embed_features(features)
            norms = torch.norm(self.classifier_weight, 2.0, [1], True)
            weights = self.classifier_weight / torch.clamp(norms, min=1e-12)
            return torch.matmul(embedding, weights.t()) * self.logit_scale
        def probabilities_from_features(self, features: Tensor) -> Tensor:
            return torch.softmax(self.logits_from_features(features), 1)
        def forward(self, image: Tensor) -> Tensor:
            return self.probabilities_from_features(self.features(image))
        def embedding(self, image: Tensor) -> Tensor:
            return self.embed_features(self.features(image))
    )JIT");
    result.eval();
    for (const torch::Tensor& parameter : result.parameters()) parameter.set_requires_grad(false);
    return result;
}

namespace {
constexpr quint32 TemplateMagic = 0x46505432;
QJsonArray Labels(const AlgorithmModelInfo& info) {
    QJsonArray labels;
    for (const std::string& label : info.labels) labels.append(QString::fromStdString(label));
    return labels;
}
QJsonObject TemplateMetadata(const std::string& id, const AlgorithmModelInfo& info) {
    return {{"format", TemplateFormat}, {"version", TemplateVersion},
        {"model_id", QString::fromStdString(id)}, {"output_dim", info.output_size},
        {"weight_sha256", QString::fromStdString(info.weight_sha256)},
        {"label_order_sha256", QString::fromStdString(info.label_order_sha256)}, {"labels", Labels(info)},
        {"preprocessing", QString::fromStdString(info.preprocessing)}};
}
bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}
bool Decode(const std::vector<unsigned char>& bytes, const QJsonObject& expected,
            std::vector<float>& result, std::string& error) {
    if (bytes.size() > 16384 || bytes.size() < 16)
        return Fail(error, "Invalid template; regenerate 30-dimensional enrollment templates.");
    QDataStream in(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), int(bytes.size())));
    in.setVersion(QDataStream::Qt_5_15);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 magic; quint16 version; QByteArray json, payload;
    in >> magic >> version;
    if (magic != TemplateMagic || version != TemplateVersion)
        return Fail(error, "Legacy/unknown template format; regenerate 30-dimensional templates.");
    in >> json >> payload;
    if (in.status() != QDataStream::Ok || !in.atEnd() || QJsonDocument::fromJson(json).object() != expected
        || payload.size() != TemplateSize * int(sizeof(float)))
        return Fail(error, "Template model, weight, labels or preprocessing changed; regenerate enrollment templates.");
    QDataStream values(payload);
    values.setVersion(QDataStream::Qt_5_15);
    values.setByteOrder(QDataStream::LittleEndian); values.setFloatingPointPrecision(QDataStream::SinglePrecision);
    result.resize(TemplateSize);
    double sum = 0;
    for (float& value : result) {
        values >> value;
        if (!std::isfinite(value) || value < 0 || value > 1) return Fail(error, "Invalid probability template.");
        sum += value;
    }
    if (values.status() != QDataStream::Ok || std::abs(sum - 1) > ProbabilityTolerance)
        return Fail(error, "Template probabilities do not sum to one.");
    return true;
}
}

struct FingerprintEncoder::State {
    struct Model { std::string id; torch::jit::Module module; AlgorithmModelInfo info; };
    std::vector<Model> models;
    Model* Find(const std::string& id) {
        for (Model& model : models) if (model.id == id) return &model;
        return nullptr;
    }
};

FingerprintEncoder::FingerprintEncoder() : state_(new State) { torch::set_num_threads(Threads); }
FingerprintEncoder::~FingerprintEncoder() = default;

bool FingerprintEncoder::LoadModel(const std::string& id, const std::string& file, std::string& error) {
    error.clear();
    // Failed reloads cannot expose a previously loaded model with this ID.
    for (std::vector<State::Model>::iterator it = state_->models.begin(); it != state_->models.end();)
        if (it->id == id) it = state_->models.erase(it); else ++it;
    try {
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        torch::jit::Module module = LoadArchive(QString::fromStdString(file), extras);
        const QJsonObject metadata = QJsonDocument::fromJson(QByteArray::fromStdString(extras["metadata.json"])).object();
        const QString preprocessing = metadata.value("preprocessing").toString();
        const bool pretrained = id == "pretrained";
        const bool compatible = preprocessing == Preprocessing
            || (pretrained && preprocessing == CompatibleArchivePreprocessing);
        if (metadata.value("model_id").toString().toStdString() != id || !compatible
            || metadata.value("embedding_size").toInt() != EmbeddingSize)
            return Fail(error, "Incompatible model ID, encoder dimension or preprocessing.");
        if (pretrained) {
            if (metadata.value("format").toString() != "deepprint-torchscript-v1"
                || metadata.value("architecture").toString() != Architecture)
                return Fail(error, "Expected the original pretrained 512-dimensional encoder.");
        } else if (metadata.value("format").toString() != ProbabilityFormat
                   || metadata.value("architecture").toString() != ProbabilityArchitecture
                   || metadata.value("output_dim").toInt() != TemplateSize) {
            return Fail(error, "Legacy/incompatible weights: retrain with retained Cosine Classifier (30 probabilities, v2).");
        }
        AlgorithmModelInfo info;
        info.format = metadata.value("format").toString().toStdString();
        info.weight_sha256 = Hash(QString::fromStdString(file)).toStdString();
        info.preprocessing = preprocessing.toStdString();
        info.output_size = pretrained ? EmbeddingSize : TemplateSize;
        if (!pretrained) {
            const QJsonArray labels = metadata.value("labels").toArray();
            for (const QJsonValue& label : labels) {
                if (!label.isString() || label.toString().isEmpty()) return Fail(error, "Invalid ID label.");
                const std::string identity = label.toString().toStdString();
                if (std::find(info.labels.begin(), info.labels.end(), identity) != info.labels.end())
                    return Fail(error, "Expected 30 unique labels in the recorded label order.");
                info.labels.push_back(identity);
            }
            info.label_order_sha256 = LabelHash(labels).toStdString();
            if (labels.size() != TemplateSize
                || metadata.value("label_order_sha256").toString().toStdString() != info.label_order_sha256)
                return Fail(error, "Expected 30 unique labels in the recorded label order.");
        }
        module.eval();
        torch::NoGradGuard guard;
        torch::Tensor sample = module.forward({torch::zeros({1, 1, InputSize, InputSize})}).toTensor();
        if (pretrained ? (sample.sizes() != torch::IntArrayRef({1, EmbeddingSize}) || !torch::isfinite(sample).all().item<bool>())
                       : !Probabilities(sample, 1))
            return Fail(error, "Invalid model output.");
        state_->models.push_back({id, module, info});
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::GetModelInfo(const std::string& id, AlgorithmModelInfo& info, std::string& error) const {
    info = {}; error.clear();
    const State::Model* model = state_->Find(id);
    if (!model) return Fail(error, "Model is not loaded: " + id);
    info = model->info;
    return true;
}

bool FingerprintEncoder::Preprocess(const std::string& id, const AlgorithmImage& image, PreparedImage& prepared, std::string& error) {
    prepared = {}; error.clear();
    try {
        if (image.width <= 0 || image.height <= 0 || image.pixels.size() != std::size_t(image.width) * image.height)
            return Fail(error, "Expected tightly packed gray8 image.");
        torch::NoGradGuard guard;
        State::Model* model = state_->Find(id);
        if (!model) return Fail(error, "Model is not loaded: " + id);
        torch::Tensor tensor = torch::from_blob(const_cast<unsigned char*>(image.pixels.data()), {1, 1, image.height, image.width}, torch::kUInt8)
                          .to(torch::kFloat32).div(double(GrayMaximum));
        if (image.database_id == CenterCropDatabase) {
            const int side = std::min(image.width, image.height), left = (image.width - side) / 2, top = (image.height - side) / 2;
            tensor = tensor.slice(2, top, top + side).slice(3, left, left + side);
        } else {
            const int side = std::max(image.width, image.height), left = (side - image.width) / 2, top = (side - image.height) / 2;
            tensor = torch::constant_pad_nd(tensor, {left, side - image.width - left, top, side - image.height - top}, 1.0);
        }
        tensor = at::_upsample_bilinear2d_aa(tensor, {InputSize, InputSize}, false, {}, {});
        tensor = model->module.run_method("preprocess", tensor).toTensor().contiguous();
        if (tensor.numel() != InputSize * InputSize || !torch::isfinite(tensor).all().item<bool>())
            return Fail(error, "Invalid preprocessed input.");
        prepared.width = prepared.height = InputSize;
        prepared.values.assign(tensor.data_ptr<float>(), tensor.data_ptr<float>() + tensor.numel());
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::CreateTemplate(const std::string& id, const PreparedImage& prepared, std::vector<unsigned char>& data, std::string& error) {
    data.clear(); error.clear();
    try {
        State::Model* model = state_->Find(id);
        if (!model) return Fail(error, "Model is not loaded: " + id);
        if (model->info.output_size != TemplateSize) return Fail(error, "Pretrained encoder is for training only; load 30-output production weights.");
        if (prepared.width != InputSize || prepared.height != InputSize
            || prepared.values.size() != InputSize * InputSize)
            return Fail(error, "Expected a preprocessed 299x299 image.");
        torch::NoGradGuard guard;
        torch::Tensor input = torch::from_blob(const_cast<float*>(prepared.values.data()), {1, 1, prepared.height, prepared.width}, torch::kFloat32);
        torch::Tensor probabilities = model->module.forward({input}).toTensor().contiguous();
        if (!Probabilities(probabilities, 1)) return Fail(error, "Invalid 30-dimensional probability output.");
        QByteArray payload;
        QDataStream values(&payload, QIODevice::WriteOnly);
        values.setVersion(QDataStream::Qt_5_15);
        values.setByteOrder(QDataStream::LittleEndian); values.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (int i = 0; i < TemplateSize; ++i) values << probabilities[0][i].item<float>();
        QByteArray envelope;
        QDataStream stream(&envelope, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_15);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << TemplateMagic << quint16(TemplateVersion)
               << QJsonDocument(TemplateMetadata(id, model->info)).toJson(QJsonDocument::Compact) << payload;
        if (stream.status() != QDataStream::Ok) return Fail(error, "Template serialization failed.");
        data.assign(envelope.begin(), envelope.end());
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool FingerprintEncoder::Compare(const std::string& id, const std::vector<unsigned char>& enrollment,
                                 const std::vector<unsigned char>& attempt, double& score, std::string& error) {
    score = 0; error.clear();
    const State::Model* model = state_->Find(id);
    if (!model) return Fail(error, "Model is not loaded: " + id);
    if (model->info.output_size != TemplateSize) return Fail(error, "Not a production probability model.");
    const QJsonObject expected = TemplateMetadata(id, model->info);
    std::vector<float> a, b;
    if (!Decode(enrollment, expected, a, error) || !Decode(attempt, expected, b, error)) return false;
    double dot = 0, normA = 0, normB = 0;
    for (int i = 0; i < TemplateSize; ++i) {
        dot += double(a[i]) * b[i]; normA += double(a[i]) * a[i]; normB += double(b[i]) * b[i];
    }
    score = (std::clamp(dot / std::sqrt(normA * normB), 0.0, 1.0) + 1.0) / 2.0;
    return true;
}

} // namespace algorithm
