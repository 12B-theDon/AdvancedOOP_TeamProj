#include "encoder.h"
#include "encoderConfig.h"
#include "verified_pairs.h"
#include <ATen/Version.h>
#include <ATen/Context.h>
#include <ATen/Parallel.h>
#include <torch/version.h>
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
#include <chrono>
#include <future>
#include <map>
#include <set>
#include <functional>
#include <optional>
#include <QRegularExpression>
#include <torch/csrc/jit/ir/constants.h>
#include <windows.h>
#include <bcrypt.h>

namespace algorithm {
void Require(bool ok, const QString& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
QString Hash(const QString& path) {
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Cannot open " + path);
    struct Context {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~Context() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        static void Check(NTSTATUS status, const char* operation, const QString& path) {
            if (status < 0)
                throw std::runtime_error(QString("%1 failed (0x%2): %3")
                    .arg(QLatin1String(operation)).arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'))
                    .arg(path).toStdString());
        }
    } context;
    Context::Check(BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
                   "BCryptOpenAlgorithmProvider", path);
    // Windows owns the hash object's storage until BCryptDestroyHash().
    Context::Check(BCryptCreateHash(context.algorithm, &context.hash, nullptr, 0, nullptr, 0, 0), "BCryptCreateHash", path);
    QByteArray buffer(1024 * 1024, '\0');
    for (;;) {
        const qint64 count = file.read(buffer.data(), buffer.size());
        Require(count >= 0, "Cannot read for SHA-256: " + path + " (" + file.errorString() + ")");
        if (count == 0) break;
        Context::Check(BCryptHashData(context.hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0),
                       "BCryptHashData", path);
    }
    QByteArray digest(32, '\0'); // SHA-256 always produces 32 bytes.
    Context::Check(BCryptFinishHash(context.hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0),
                   "BCryptFinishHash", path);
    return QString::fromLatin1(digest.toHex());
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
std::string VerificationRuntime() {
#if defined(_M_X64) && !defined(_DEBUG) && defined(FINGERPRINT_VERIFICATION_IMPLEMENTATION)
    // These are queried from the loaded runtime, rather than archive metadata.
    const std::string description = std::string("windows-x64-release-v1;")
        + std::to_string(_MSC_FULL_VER) + ";" + TORCH_VERSION + ";" + qVersion() + ";"
        + at::show_config() + ";" + at::get_cpu_capability() + ";"
        + std::string(FINGERPRINT_VERIFICATION_CONFIGURATION) + ";" + std::to_string(at::get_num_threads()) + ";" + std::to_string(at::get_num_interop_threads())
        + ";mkldnn=" + std::to_string(at::globalContext().userEnabledMkldnn());
    std::string assumptions = description;
    for (const char* variable : {"ATEN_CPU_CAPABILITY", "MKL_CBWR", "MKL_DEBUG_CPU_TYPE", "MKL_ENABLE_INSTRUCTIONS",
                                 "ONEDNN_MAX_CPU_ISA", "DNNL_MAX_CPU_ISA", "MKL_NUM_THREADS", "OMP_NUM_THREADS"})
        assumptions += std::string(";") + variable + "=" + qgetenv(variable).toStdString();
    return QCryptographicHash::hash(QByteArray::fromStdString(assumptions), QCryptographicHash::Sha256).toHex().toStdString();
#else
    return {}; // No certification applies to other platforms/builds.
#endif
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
bool ApprovedRecord(const VerifiedPairRecord& record) {
#ifdef FINGERPRINT_VERIFICATION_IMPLEMENTATION
    return std::string(record.implementation_sha256) == FINGERPRINT_VERIFICATION_IMPLEMENTATION
        && !VerificationRuntime().empty() && record.runtime_sha256 == VerificationRuntime();
#else
    return false;
#endif
}
bool ApprovedMember(const std::string& hash, const EncoderOptions& options) {
    if (!options.reuse_verified_pairs || !options.shared_features || options.representative_initialization) return false;
    for (const VerifiedPairRecord& record : VerifiedPairs)
        if ((hash == record.first_sha256 || hash == record.second_sha256) && ApprovedRecord(record)) return true;
    return false;
}
bool ApprovedPair(const std::string& first, const std::string& second, const EncoderOptions& options) {
    if (!options.reuse_verified_pairs || !options.shared_features || options.representative_initialization) return false;
    for (const VerifiedPairRecord& record : VerifiedPairs)
        if (((first == record.first_sha256 && second == record.second_sha256)
            || (second == record.first_sha256 && first == record.second_sha256)) && ApprovedRecord(record)) return true;
    return false;
}
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
using Clock = std::chrono::steady_clock;
double Elapsed(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
torch::Tensor PrepareTensor(torch::jit::Module& module, const AlgorithmImage& image) {
    Require(image.width > 0 && image.height > 0 && image.pixels.size() == std::size_t(image.width) * image.height,
            "Expected tightly packed gray8 image.");
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
    tensor = module.run_method("preprocess", tensor).toTensor().contiguous();
    if (tensor.numel() != InputSize * InputSize || !torch::isfinite(tensor).all().item<bool>())
        throw std::runtime_error("Invalid preprocessed input.");
    return tensor;
}
bool Serialize(const std::string& id, const AlgorithmModelInfo& info, const torch::Tensor& probabilities,
               std::vector<unsigned char>& data, std::string& error) {
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
           << QJsonDocument(TemplateMetadata(id, info)).toJson(QJsonDocument::Compact) << payload;
    if (stream.status() != QDataStream::Ok) return Fail(error, "Template serialization failed.");
    data.assign(envelope.begin(), envelope.end());
    return true;
}
bool EqualValue(const c10::IValue& a, const c10::IValue& b) {
    if (std::string(a.tagKind()) != b.tagKind()) return false;
    if (a.isTensor()) {
        const torch::Tensor& x = a.toTensor(); const torch::Tensor& y = b.toTensor();
        return x.scalar_type() == y.scalar_type() && x.device() == y.device() && x.layout() == y.layout()
            && x.sizes() == y.sizes() && x.strides() == y.strides() && torch::equal(x, y);
    }
    if (a.isList()) {
        const c10::List<c10::IValue> x = a.toList(), y = b.toList();
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) if (!EqualValue(x.get(i), y.get(i))) return false;
        return true;
    }
    if (a.isTuple()) {
        const std::vector<c10::IValue>& x = a.toTupleRef().elements(), &y = b.toTupleRef().elements();
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) if (!EqualValue(x[i], y[i])) return false;
        return true;
    }
    if (a.isGenericDict()) {
        const c10::impl::GenericDict x = a.toGenericDict(), y = b.toGenericDict();
        if (x.size() != y.size()) return false;
        for (c10::impl::GenericDict::iterator item = x.begin(); item != x.end(); ++item) {
            const c10::impl::GenericDict::iterator found = y.find(item->key());
            if (found == y.end() || !EqualValue(item->value(), found->value())) return false;
        }
        return true;
    }
    return a == b;
}
QString GraphText(const torch::jit::Method& method) {
    QString text = QString::fromStdString(method.graph()->toString());
    text.remove(QRegularExpression("\\.___torch_mangle_[0-9]+"));
    return text;
}
void Constants(const torch::jit::Block* block, std::vector<c10::IValue>& values) {
    for (const torch::jit::Node* node : block->nodes()) {
        if (node->kind() == torch::jit::prim::Constant && node->outputs().size() == 1) {
            const std::optional<c10::IValue> value = torch::jit::toIValue(node->output());
            if (value) values.push_back(*value);
        }
        for (const torch::jit::Block* child : node->blocks()) Constants(child, values);
    }
}
bool HeadTensor(const std::string& name) {
    return name == "classifier_weight" || name.rfind("encoder.texture_projection.", 0) == 0
        || name.rfind("encoder.minutia_projection.", 0) == 0;
}
using MethodWork = std::pair<torch::jit::Module, std::string>;
std::optional<torch::jit::Module> Receiver(const torch::jit::Module& module,
        const torch::jit::Value* self, const torch::jit::Value* value) {
    if (value == self) return module;
    const torch::jit::Node* node = value->node();
    if (node->kind() != torch::jit::prim::GetAttr || node->inputs().size() != 1) return std::nullopt;
    const std::optional<torch::jit::Module> parent = Receiver(module, self, node->input());
    if (!parent) return std::nullopt;
    const c10::IValue attribute = parent->attr(node->s(torch::jit::attr::name));
    if (!attribute.isObject() || !attribute.toObject()->type()->is_module()) return std::nullopt;
    return attribute.toModule();
}
bool FeatureBlock(const torch::jit::Block* block, const torch::jit::Module& module,
                  const torch::jit::Value* self, std::vector<MethodWork>& pending) {
    for (const torch::jit::Node* node : block->nodes()) {
        const std::string kind = node->kind().toQualString();
        if (kind == "prim::CallFunction" || kind == "prim::PythonOp" || kind == "prim::SetAttr") return false;
        if (node->isNondeterministic()) {
            bool disabledDropout = false;
            if (kind == "aten::dropout" && node->inputs().size() >= 3) {
                const std::optional<c10::IValue> training = torch::jit::toIValue(node->input(2));
                disabledDropout = training && training->isBool() && !training->toBool();
            }
            if (!disabledDropout) return false;
        }
        if (node->kind() == torch::jit::prim::GetAttr) {
            const std::string name = node->s(torch::jit::attr::name);
            if (name == "classifier_weight" || name == "texture_projection" || name == "minutia_projection") return false;
        }
        if (node->kind() == torch::jit::prim::CallMethod) {
            const std::optional<torch::jit::Module> target = Receiver(module, self, node->input(0));
            if (!target) return false;
            pending.emplace_back(*target, node->s(torch::jit::attr::name));
        }
        for (const torch::jit::Block* child : node->blocks()) if (!FeatureBlock(child, module, self, pending)) return false;
    }
    return true;
}
bool SplitForwardAndIndependentFeatures(torch::jit::Module& module) {
    // Only the declared features -> head wrapper may be split. A producer whose
    // features depend on trained heads, state updates or opaque calls falls back.
    const std::shared_ptr<torch::jit::Graph> graph = module.get_method("forward").graph();
    if (graph->inputs().size() != 2 || graph->outputs().size() != 1) return false;
    std::vector<const torch::jit::Node*> nodes;
    for (const torch::jit::Node* node : graph->nodes()) nodes.push_back(node);
    if (nodes.size() != 2 || nodes[0]->kind() != torch::jit::prim::CallMethod
        || nodes[1]->kind() != torch::jit::prim::CallMethod
        || nodes[0]->s(torch::jit::attr::name) != "features"
        || nodes[1]->s(torch::jit::attr::name) != "probabilities_from_features"
        || nodes[0]->inputs().size() != 2 || nodes[1]->inputs().size() != 2
        || nodes[0]->input(0) != graph->inputs()[0] || nodes[0]->input(1) != graph->inputs()[1]
        || nodes[1]->input(0) != graph->inputs()[0] || nodes[1]->input(1) != nodes[0]->output()
        || graph->outputs()[0] != nodes[1]->output()) return false;
    std::vector<MethodWork> pending{{module, "preprocess"}, {module, "features"}};
    std::set<std::pair<std::uintptr_t, std::string>> visited;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const MethodWork work = pending[i];
        if (!visited.emplace(reinterpret_cast<std::uintptr_t>(work.first._ivalue().get()), work.second).second) continue;
        const std::optional<torch::jit::Method> method = work.first.find_method(work.second);
        if (!method || method->graph()->inputs().empty()
            || !FeatureBlock(method->graph()->block(), work.first, method->graph()->inputs()[0], pending)) return false;
    }
    return true;
}
bool SameProgramAndFrozenTensors(torch::jit::Module& a, torch::jit::Module& b) {
    std::map<std::string, torch::jit::Module> modules;
    for (const torch::jit::Named<torch::jit::Module>& m : b.named_modules()) modules.emplace(m.name, m.value);
    std::size_t seen = 0;
    for (const torch::jit::Named<torch::jit::Module>& m : a.named_modules()) {
        const std::map<std::string, torch::jit::Module>::iterator found = modules.find(m.name);
        if (found == modules.end()) return false;
        const torch::jit::Module& other = found->second; ++seen;
        const std::vector<torch::jit::Method> methods = m.value.get_methods(), otherMethods = other.get_methods();
        if (methods.size() != otherMethods.size() || m.value.type()->numAttributes() != other.type()->numAttributes()) return false;
        for (const torch::jit::Method& method : methods) {
            const std::optional<torch::jit::Method> match = other.find_method(method.name());
            if (!match || GraphText(method) != GraphText(*match)) return false;
            std::vector<c10::IValue> x, y; Constants(method.graph()->block(), x); Constants(match->graph()->block(), y);
            if (x.size() != y.size()) return false;
            for (std::size_t i = 0; i < x.size(); ++i) if (!EqualValue(x[i], y[i])) return false;
        }
        for (std::size_t i = 0; i < m.value.type()->numAttributes(); ++i) {
            const std::string& name = m.value.type()->getAttributeName(i);
            if (!other.hasattr(name)) return false;
            const c10::IValue x = m.value.attr(name), y = other.attr(name);
            const std::string full = m.name.empty() ? name : m.name + "." + name;
            if (x.isObject() && y.isObject()) {
                if (!x.toObject()->type()->is_module() || !y.toObject()->type()->is_module()) return false;
                continue; // Module objects were recursively checked above.
            }
            if (!HeadTensor(full) && !EqualValue(x, y)) return false;
        }
    }
    return seen == modules.size();
}
// Keep an immutable read lease while the archive and its digest are being read.
class ModelReadLease {
public:
    explicit ModelReadLease(const QString& path) {
        file_ = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file_ != INVALID_HANDLE_VALUE, "Cannot open stable model input: " + path);
    }
    ~ModelReadLease() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    ModelReadLease(const ModelReadLease&) = delete;
    ModelReadLease& operator=(const ModelReadLease&) = delete;
private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
};

}

struct FingerprintEncoder::State {
    struct Model {
        std::string id; torch::jit::Module module; AlgorithmModelInfo info;
        torch::Tensor validation_input, validation_output; EncoderProfile profile;
        bool validation_deferred = false;
    };
    struct Built { std::unique_ptr<Model> model; std::string error; };
    EncoderOptions options; EncoderProfile profile;
    std::vector<Model> models;
    std::map<std::pair<std::string, std::string>, bool> compatible;
    Model* Find(const std::string& id) {
        for (Model& model : models) if (model.id == id) return &model;
        return nullptr;
    }
    static Built Reject(std::unique_ptr<Model>& model, const std::string& error) { model.reset(); return {nullptr, error}; }
    static Built Build(const std::string& id, const std::string& file, const EncoderOptions& options,
                       const AlgorithmImage* initialization) {
        std::unique_ptr<Model> result(new Model); result->id = id;
        try {
            ModelReadLease lease(QString::fromStdString(file));
            std::future<std::pair<QString, double>> hashing;
            if (options.overlap_hash) hashing = std::async(std::launch::async, [file]() {
                const Clock::time_point start = Clock::now();
                const QString digest = Hash(QString::fromStdString(file));
                return std::make_pair(digest, Elapsed(start));
            });
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        const Clock::time_point archiveStart = Clock::now();
        torch::jit::Module module = LoadArchive(QString::fromStdString(file), extras);
        result->profile.archive_ms = Elapsed(archiveStart);
        const QJsonObject metadata = QJsonDocument::fromJson(QByteArray::fromStdString(extras["metadata.json"])).object();
        const QString preprocessing = metadata.value("preprocessing").toString();
        const bool pretrained = id == "pretrained";
        const bool compatible = preprocessing == Preprocessing
            || (pretrained && preprocessing == CompatibleArchivePreprocessing);
        if (metadata.value("model_id").toString().toStdString() != id || !compatible
            || metadata.value("embedding_size").toInt() != EmbeddingSize)
            return Reject(result, "Incompatible model ID, encoder dimension or preprocessing.");
        if (pretrained) {
            if (metadata.value("format").toString() != "deepprint-torchscript-v1"
                || metadata.value("architecture").toString() != Architecture)
                return Reject(result, "Expected the original pretrained 512-dimensional encoder.");
        } else if (metadata.value("format").toString() != ProbabilityFormat
                   || metadata.value("architecture").toString() != ProbabilityArchitecture
                   || metadata.value("output_dim").toInt() != TemplateSize) {
            return Reject(result, "Legacy/incompatible weights: retrain with retained Cosine Classifier (30 probabilities, v2).");
        }
        AlgorithmModelInfo info;
        info.format = metadata.value("format").toString().toStdString();
        if (options.overlap_hash) {
            const std::pair<QString, double> digest = hashing.get();
            info.weight_sha256 = digest.first.toStdString(); result->profile.hash_ms = digest.second;
        } else {
            const Clock::time_point hashStart = Clock::now();
            info.weight_sha256 = Hash(QString::fromStdString(file)).toStdString(); result->profile.hash_ms = Elapsed(hashStart);
        }
        info.preprocessing = preprocessing.toStdString();
        info.output_size = pretrained ? EmbeddingSize : TemplateSize;
        if (!pretrained) {
            const QJsonArray labels = metadata.value("labels").toArray();
            for (const QJsonValue& label : labels) {
                if (!label.isString() || label.toString().isEmpty()) return Reject(result, "Invalid ID label.");
                const std::string identity = label.toString().toStdString();
                if (std::find(info.labels.begin(), info.labels.end(), identity) != info.labels.end())
                    return Reject(result, "Expected 30 unique labels in the recorded label order.");
                info.labels.push_back(identity);
            }
            info.label_order_sha256 = LabelHash(labels).toStdString();
            if (labels.size() != TemplateSize
                || metadata.value("label_order_sha256").toString().toStdString() != info.label_order_sha256)
                return Reject(result, "Expected 30 unique labels in the recorded label order.");
        }
        module.eval();
        result->module = std::move(module); result->info = std::move(info);
        result->validation_deferred = !pretrained && ApprovedMember(result->info.weight_sha256, options);
        if (!result->validation_deferred) Validate(*result, options, initialization);
        return {std::move(result), {}};
        } catch (const std::exception& exception) { return Reject(result, exception.what()); }
    }
    static void Validate(Model& model, const EncoderOptions& options, const AlgorithmImage* initialization) {
        torch::NoGradGuard guard;
        const Clock::time_point validationStart = Clock::now();
        torch::Tensor input = torch::zeros({1, 1, InputSize, InputSize});
        if (options.representative_initialization && initialization) input = PrepareTensor(model.module, *initialization);
        torch::Tensor sample = model.module.forward({input}).toTensor();
        model.profile.validation_ms += Elapsed(validationStart);
        model.validation_input = input.clone(); model.validation_output = sample.clone();
        const bool pretrained = model.id == "pretrained";
        Require(pretrained ? (sample.sizes() == torch::IntArrayRef({1, EmbeddingSize}) && torch::isfinite(sample).all().item<bool>())
                           : Probabilities(sample, 1), "Invalid model output.");
        model.validation_deferred = false;
    }
    bool CanShare(Model& a, Model& b) {
        const std::pair<std::string, std::string> key(a.id, b.id);
        const std::map<std::pair<std::string, std::string>, bool>::iterator found = compatible.find(key);
        if (found != compatible.end()) return found->second;
        if (ApprovedPair(a.info.weight_sha256, b.info.weight_sha256, options)) {
            profile.sharing_status = "approved content pair and implementation/runtime";
            compatible[key] = true;
            return true;
        }
        const Clock::time_point start = Clock::now(); bool ok = false;
        try {
            // A previously approved model may later be paired with an unknown reload.
            if (a.validation_deferred) Validate(a, options, nullptr);
            if (b.validation_deferred) Validate(b, options, nullptr);
            torch::NoGradGuard guard;
            ok = a.info.preprocessing == b.info.preprocessing && a.info.output_size == TemplateSize
                && b.info.output_size == TemplateSize && a.module.find_method("features")
                && b.module.find_method("features") && a.module.find_method("probabilities_from_features")
                && b.module.find_method("probabilities_from_features") && SplitForwardAndIndependentFeatures(a.module)
                && SplitForwardAndIndependentFeatures(b.module) && SameProgramAndFrozenTensors(a.module, b.module)
                && torch::equal(a.validation_input, b.validation_input);
            if (ok) {
                torch::Tensor features = a.module.run_method("features", a.validation_input).toTensor();
                const torch::Tensor saved = features.clone();
                const torch::Tensor first = a.module.run_method("probabilities_from_features", features).toTensor();
                ok = torch::equal(features, saved) && torch::equal(first, a.validation_output);
                const torch::Tensor second = b.module.run_method("probabilities_from_features", features).toTensor();
                ok = ok && torch::equal(features, saved) && torch::equal(second, b.validation_output);
            }
        } catch (const std::exception&) { ok = false; }
        profile.compatibility_ms += Elapsed(start);
        profile.sharing_status = ok ? "verified program, constants, frozen tensors and exact outputs" : "unverified pair; legacy fallback";
        compatible[key] = ok; return ok;
    }
};
FingerprintEncoder::FingerprintEncoder(const EncoderOptions& options) : state_(new State) {
    state_->options = options; torch::set_num_threads(Threads);
}
FingerprintEncoder::~FingerprintEncoder() = default;
EncoderProfile FingerprintEncoder::Profile() const { return state_->profile; }
bool FingerprintEncoder::LoadModel(const std::string& id, const std::string& file, std::string& error) {
    std::vector<std::string> errors;
    const std::vector<bool> result = LoadModels({id}, {file}, errors);
    error = errors.front(); return result.front();
}
std::vector<bool> FingerprintEncoder::LoadModels(const std::vector<std::string>& ids,
        const std::vector<std::string>& files, std::vector<std::string>& errors, const AlgorithmImage* initialization) {
    errors.assign(ids.size(), {}); std::vector<bool> loaded(ids.size(), false);
    if (ids.size() != files.size()) { errors.assign(ids.size(), "Model ID/path count mismatch."); return loaded; }
    state_->compatible.clear();
    for (const std::string& id : ids)
        for (std::vector<State::Model>::iterator it = state_->models.begin(); it != state_->models.end();)
            if (it->id == id) it = state_->models.erase(it); else ++it;
    std::vector<std::future<State::Built>> tasks;
    if (state_->options.parallel_loading)
        for (std::size_t i = 0; i < ids.size(); ++i) tasks.push_back(std::async(std::launch::async,
            [id = ids[i], file = files[i], options = state_->options, initialization]() { return State::Build(id, file, options, initialization); }));
    for (std::size_t i = 0; i < ids.size(); ++i) {
        State::Built result = state_->options.parallel_loading ? tasks[i].get() : State::Build(ids[i], files[i], state_->options, initialization);
        errors[i] = result.error;
        if (result.model) {
            state_->profile.archive_ms += result.model->profile.archive_ms;
            state_->profile.hash_ms += result.model->profile.hash_ms;
            state_->profile.validation_ms += result.model->profile.validation_ms;
            state_->models.push_back(std::move(*result.model)); loaded[i] = true;
        }
    }
    // Deferral is allowed only with both actual, leased file digests present.
    // Single-model and uncertified/mixed pairs retain the original dummy validation.
    for (std::size_t i = 0; i < ids.size(); ++i) {
        State::Model* model = state_->Find(ids[i]);
        if (!model || !model->validation_deferred) continue;
        bool approved = false;
        for (const State::Model& other : state_->models)
            if (other.id != model->id && ApprovedPair(model->info.weight_sha256, other.info.weight_sha256, state_->options)) approved = true;
        if (approved) continue;
        try {
            State::Validate(*model, state_->options, initialization);
            state_->profile.validation_ms += model->profile.validation_ms;
        } catch (const std::exception& exception) {
            errors[i] = exception.what(); loaded[i] = false;
            for (std::vector<State::Model>::iterator it = state_->models.begin(); it != state_->models.end(); ++it)
                if (it->id == ids[i]) { state_->models.erase(it); break; }
        }
    }
    return loaded;
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
        const Clock::time_point start = Clock::now();
        torch::Tensor tensor = PrepareTensor(model->module, image);
        state_->profile.preprocess_ms += Elapsed(start);
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
        ++state_->profile.legacy_model_calls;
        return Serialize(id, model->info, probabilities, data, error);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

std::vector<TemplateResult> FingerprintEncoder::CreateTemplates(const std::vector<std::string>& ids, const AlgorithmImage& image) {
    std::vector<TemplateResult> result(ids.size());
    bool shared = false;
    if (ids.size() == 2 && state_->options.shared_features) {
        State::Model* first = state_->Find(ids[0]); State::Model* second = state_->Find(ids[1]);
        if (first && second && state_->CanShare(*first, *second)) {
            try {
                PreparedImage prepared; std::string error;
                if (!Preprocess(ids[0], image, prepared, error)) throw std::runtime_error(error);
                torch::NoGradGuard guard;
                torch::Tensor input = torch::from_blob(prepared.values.data(), {1, 1, InputSize, InputSize}, torch::kFloat32);
                const Clock::time_point start = Clock::now();
                torch::Tensor features = first->module.run_method("features", input).toTensor();
                Require(features.sizes() == torch::IntArrayRef({1, FeatureSize}) && torch::isfinite(features).all().item<bool>(), "Invalid shared features.");
                state_->profile.features_ms += Elapsed(start);
                const torch::Tensor saved = features.clone();
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    try {
                        State::Model* model = state_->Find(ids[i]); const Clock::time_point headStart = Clock::now();
                        torch::Tensor probabilities = model->module.run_method("probabilities_from_features", features).toTensor().contiguous();
                        Require(torch::equal(features, saved), "Head modified shared features.");
                        result[i].success = Serialize(ids[i], model->info, probabilities, result[i].data, result[i].error);
                        state_->profile.heads_ms += Elapsed(headStart);
                    } catch (const std::exception& exception) {
                        result[i] = {}; result[i].error = exception.what(); features = saved.clone();
                    }
                }
                ++state_->profile.shared_images; shared = true;
            } catch (const std::exception&) { /* Independent original paths preserve partial success. */ }
        }
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (shared && result[i].success) continue;
        PreparedImage prepared;
        result[i].success = Preprocess(ids[i], image, prepared, result[i].error)
            && CreateTemplate(ids[i], prepared, result[i].data, result[i].error);
    }
    return result;
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
    score = std::clamp(dot / std::sqrt(normA * normB), 0.0, 1.0);
    return true;
}

} // namespace algorithm
