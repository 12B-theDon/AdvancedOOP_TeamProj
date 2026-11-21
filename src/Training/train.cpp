#include <torch/script.h> // Before Qt's slots macro.
#include <torch/torch.h>
#include "train.h"
#include "augmentConfig.h"
#include "encoder.h"
#include "encoderConfig.h"
#include "trainConfig.h"
#include "ValidationEngine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <exception>

namespace algorithm {

static bool Trainable(const std::string& name) {
    for (const std::string& prefix : TrainablePrefixes)
        if (name.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}
static torch::Tensor Prepared(FingerprintEncoder& encoder, const TrainingImage& image) {
    PreparedImage prepared; std::string error;
    Require(encoder.Preprocess("pretrained", training::Engine::ReadImage(image.path.toStdString(), image.database.toStdString()), prepared, error),
            QString::fromStdString(error));
    return torch::from_blob(prepared.values.data(), {1, 1, prepared.height, prepared.width}, torch::kFloat32).clone();
}

TrainingData PrepareTrainingData(const QString& dataFolder, const QString& checkpoint, const QString& cacheFolder) {
    TrainingData result;
    result.checkpoint = checkpoint;
    QString manifestPath;
    const QVector<TrainingImage> all = PrepareAugmentations(dataFolder, QDir(dataFolder).absoluteFilePath("../augmented"), &manifestPath);
    QJsonArray rows;
    for (const TrainingImage& sample : all) {
        if (sample.validation && sample.augmented) continue;
        result.samples.append(sample);
        rows.append(QJsonObject{{"path", sample.path}, {"database", sample.database}, {"identity", sample.identity},
            {"original", sample.original}, {"validation", sample.validation}, {"augmented", sample.augmented}});
    }
    QFile manifestFile(manifestPath);
    Require(manifestFile.open(QIODevice::ReadOnly), "Cannot read augmentation manifest.");
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    for (const QJsonValue& value : manifest.value("input").toObject().value("sources").toArray()) {
        const QJsonObject source = value.toObject();
        Require(Hash(QDir(dataFolder).filePath(source.value("db").toString() + "/" + source.value("file").toString()))
                    == source.value("sha256").toString(), "Original image changed since augmentation generation.");
    }
    for (const QJsonValue& value : manifest.value("images").toArray()) {
        const QJsonObject image = value.toObject();
        if (image.value("validation").toBool()) continue; // Validation augmentations are never input.
        Require(Hash(QFileInfo(manifestPath).dir().filePath(image.value("file").toString()))
                    == image.value("sha256").toString(), "Training augmentation bytes changed since manifest generation.");
    }
    const QString baseHash = Hash(checkpoint), manifestHash = Hash(manifestPath);
    const QDir cache(cacheFolder);
    QFile metadataFile(cache.filePath("features.json"));
    if (metadataFile.exists()) {
        Require(metadataFile.open(QIODevice::ReadOnly), "Cannot read feature cache metadata.");
        const QJsonObject info = QJsonDocument::fromJson(metadataFile.readAll()).object();
        Require(info.value("base_sha256").toString() == baseHash
            && info.value("augmentation_manifest_sha256").toString() == manifestHash
            && info.value("preprocessing").toString() == Preprocessing
            && info.value("shape").toArray() == QJsonArray{result.samples.size(), FeatureSize}
            && info.value("dtype").toString() == "float32 little endian", "Feature cache provenance/shape mismatch.");
        const QJsonArray savedRows = info.value("rows").toArray();
        Require(savedRows.size() == rows.size(), "Feature cache sample count mismatch.");
        for (int i = 0; i < rows.size(); ++i) {
            const QJsonObject a = rows[i].toObject(), b = savedRows[i].toObject();
            for (const char* key : {"database", "identity", "original", "validation", "augmented"})
                Require(a.value(key) == b.value(key), "Feature cache sample order/split mismatch.");
        }
        Require(Hash(cache.filePath("features.f32")) == info.value("features_sha256").toString(), "Feature cache bytes changed.");
        QFile file(cache.filePath("features.f32"));
        Require(file.open(QIODevice::ReadOnly), "Cannot read feature cache.");
        QByteArray bytes = file.readAll();
        Require(bytes.size() == result.samples.size() * FeatureSize * int(sizeof(float)), "Invalid feature cache length.");
        result.features = torch::from_blob(bytes.data(), {result.samples.size(), FeatureSize}, torch::kFloat32).clone();
        result.cache_info = info;
        QTextStream(stdout) << "Reusing verified native feature cache: " << cache.absolutePath() << Qt::endl;
    } else {
        Require(!cache.exists() || cache.entryList(QDir::Files).empty(), "Preserving existing non-cache output folder.");
        Require(QDir().mkpath(cache.absolutePath()), "Cannot create feature cache.");
        FingerprintEncoder preprocessor; std::string error;
        Require(preprocessor.LoadModel("pretrained", checkpoint.toStdString(), error), QString::fromStdString(error));
        torch::jit::Module base = torch::jit::load(checkpoint.toStdString(), torch::kCPU); base.eval();
        std::vector<torch::Tensor> chunks;
        torch::NoGradGuard guard;
        for (int start = 0; start < result.samples.size(); start += FeatureBatchSize) {
            std::vector<torch::Tensor> batch;
            for (int i = start; i < result.samples.size() && i < start + FeatureBatchSize; ++i) {
                batch.push_back(Prepared(preprocessor, result.samples[i]));
            }
            chunks.push_back(base.run_method("features", torch::cat(batch)).toTensor().clone());
            if (start % (FeatureBatchSize * LogEveryFeatureBatches) == 0)
                QTextStream(stdout) << "Preparing native features " << start << " / " << result.samples.size() << Qt::endl;
        }
        result.features = torch::cat(chunks).contiguous();
        Write(cache.filePath("features.f32"), QByteArray(reinterpret_cast<const char*>(result.features.data_ptr<float>()),
                                                                int(result.features.numel() * sizeof(float))));
        result.cache_info = QJsonObject{{"rows", rows}, {"shape", QJsonArray{result.samples.size(), FeatureSize}},
            {"dtype", "float32 little endian"}, {"producer", "production Engine::ReadImage + FingerprintEncoder::Preprocess + TorchScript features"},
            {"base_sha256", baseHash}, {"augmentation_manifest", manifestPath}, {"augmentation_manifest_sha256", manifestHash},
            {"preprocessing", Preprocessing}, {"features_sha256", Hash(cache.filePath("features.f32"))}};
        Write(cache.filePath("features.json"), QJsonDocument(result.cache_info).toJson());
    }
    Require(torch::isfinite(result.features).all().item<bool>(), "Non-finite cached features.");
    return result;
}

int RunTraining(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        QTextStream(stderr) << "Usage: FingerprintTrain <data/original> <pretrained.bin> <new-output-folder> "
            "[--feature-cache folder] [--baseline old-weight-folder] [--augment-only | --verify-only]" << Qt::endl;
        return 1;
    }
    const QString dataFolder = QDir(args[1]).absolutePath(), checkpoint = QFileInfo(args[2]).absoluteFilePath(), folder = QDir(args[3]).absolutePath();
    QString cacheFolder = QDir(folder).filePath("feature_cache"), baseline;
    bool augmentOnly = false, verifyOnly = false;
    for (int i = 4; i < args.size(); ++i) {
        if (args[i] == "--augment-only") augmentOnly = true;
        else if (args[i] == "--verify-only") verifyOnly = true;
        else if ((args[i] == "--feature-cache" || args[i] == "--baseline") && i + 1 < args.size()) {
            const bool cache = args[i] == "--feature-cache";
            const QString path = QDir(args[++i]).absolutePath();
            if (cache) cacheFolder = path; else baseline = path;
        } else {
            QTextStream(stderr) << "Unknown/incomplete training argument." << Qt::endl;
            return 1;
        }
    }
    if (augmentOnly && verifyOnly) {
        QTextStream(stderr) << "Choose augment-only or verify-only." << Qt::endl;
        return 1;
    }
    try {
        if (augmentOnly) { PrepareAugmentations(dataFolder, QDir(dataFolder).absoluteFilePath("../augmented")); return 0; }
        Require(TrainablePrefixes == std::vector<std::string>{"texture_projection.", "minutia_projection."},
                "This Partial FT path trains only the two final projections plus retained cosine head.");
        Require(Epochs > 0 && BatchSize > 0 && LearningRate > 0, "Invalid training settings.");
        Require(QDir().mkpath(folder), "Cannot create output folder.");
        for (const std::string& id : training::ModelIds)
            Require(verifyOnly || !QFile::exists(QDir(folder).filePath(QString::fromStdString(id) + ".bin")),
                    "Preserving existing weight; use a new output folder.");
        const TrainingData data = PrepareTrainingData(dataFolder, checkpoint, cacheFolder);
        if (verifyOnly) { EvaluateTrainingRun(folder, data, baseline); return 0; }
        QVector<QString> labels;
        for (const TrainingImage& sample : data.samples) if (!labels.contains(sample.identity)) labels.append(sample.identity);
        Require(labels.size() == TemplateSize, "Need30 distinct DB:finger identities across the three provided DBs.");
        const QJsonArray labelsJson = QJsonArray::fromStringList(QStringList(labels.begin(), labels.end()));
        const QString baseHash = Hash(checkpoint), labelHash = LabelHash(labelsJson);
        FingerprintEncoder preprocessor; std::string error;
        Require(preprocessor.LoadModel("pretrained", checkpoint.toStdString(), error), QString::fromStdString(error));
        QJsonArray checks;
        for (int modelIndex = 0; modelIndex < 4; ++modelIndex) {
            const QString id = QString::fromStdString(training::ModelIds[modelIndex]);
            // Reload the original encoder independently. Never use a preceding model's weights.
            torch::jit::Module encoder = torch::jit::load(checkpoint.toStdString(), torch::kCPU); encoder.eval();
            std::vector<torch::Tensor> parameters;
            std::vector<torch::Tensor> initialProjections;
            QJsonArray trainedNames;
            for (const torch::jit::Named<torch::Tensor>& p : encoder.named_parameters()) {
                const bool enabled = Trainable(p.name);
                p.value.set_requires_grad(enabled);
                if (enabled) { parameters.push_back(p.value); initialProjections.push_back(p.value.detach().clone()); trainedNames.append(QString::fromStdString(p.name)); }
            }
            Require(!parameters.empty(), "No trainable projections.");
            std::vector<int64_t> trainIndices, valIndices, targetValues;
            QJsonArray split;
            std::vector<int> active;
            for (int i = 0; i < data.samples.size(); ++i) {
                const TrainingImage& image = data.samples[i];
                const int label = labels.indexOf(image.identity); targetValues.push_back(label);
                if (modelIndex < 3 && image.database.toStdString() != training::DatabaseIds[modelIndex]) continue;
                (image.validation ? valIndices : trainIndices).push_back(i);
                if (!image.validation && std::find(active.begin(), active.end(), label) == active.end()) active.push_back(label);
                if (!image.augmented) split.append(QJsonObject{{"db", image.database}, {"original", image.original},
                    {"identity", image.identity}, {"split", image.validation ? "validation" : "train"}});
            }
            Require(active.size() == (modelIndex == 3 ? 30 : 10)
                && trainIndices.size() == (modelIndex == 3 ? 2340 : 780) && valIndices.size() == (modelIndex == 3 ? 60 : 20),
                "Unexpected train/validation identity count or split.");
            for (int label : active) {
                bool validationFound = false;
                for (int64_t i : valIndices) if (targetValues[i] == label) validationFound = true;
                Require(validationFound, "Every active training identity needs validation originals.");
            }
            torch::Tensor targets = torch::tensor(targetValues, torch::kInt64), train = torch::tensor(trainIndices, torch::kInt64), val = torch::tensor(valIndices, torch::kInt64);
            torch::manual_seed(TrainSeed);
            torch::nn::Linear classifier = torch::nn::Linear(torch::nn::LinearOptions(EmbeddingSize, TemplateSize).bias(false));
            const torch::Tensor initialClassifier = classifier->weight.detach().clone();
            for (const torch::Tensor& p : classifier->parameters()) parameters.push_back(p);
            // This temporary consumes the original head's random draws before batch shuffling.
            torch::manual_seed(TrainSeed);
            torch::nn::Linear(torch::nn::LinearOptions(EmbeddingSize, active.size()).bias(false));
            std::vector<torch::Tensor> orders;
            for (int epoch = 0; epoch < Epochs; ++epoch) orders.push_back(train.index_select(0, torch::randperm(train.numel(), torch::kInt64)));
            QCryptographicHash orderHash(QCryptographicHash::Sha256);
            for (const torch::Tensor& order : orders) orderHash.addData(reinterpret_cast<const char*>(order.data_ptr<int64_t>()), order.numel() * sizeof(int64_t));
            Require(std::string(Optimizer) == "Adam", "Production Partial FT uses Adam.");
            torch::optim::Adam optimizer(parameters, torch::optim::AdamOptions(LearningRate).weight_decay(WeightDecay));
            auto logits = [&](const torch::Tensor& batch) -> torch::Tensor {
                torch::Tensor embeddings = encoder.run_method("embed_features", batch).toTensor();
                torch::Tensor weights = torch::nn::functional::normalize(classifier->weight, torch::nn::functional::NormalizeFuncOptions().dim(1));
                return embeddings.matmul(weights.t()) * CosineLogitScale;
            };
            QByteArray history("epoch,train_loss,validation_loss,validation_classification_accuracy\n");
            double valLoss = 0, accuracy = 0;
            for (int epoch = 0; epoch < Epochs; ++epoch) {
                double lossSum = 0;
                const torch::Tensor& order = orders[epoch];
                for (int start = 0; start < order.numel(); start += BatchSize) {
                    torch::Tensor batch = order.slice(0, start, start + BatchSize);
                    optimizer.zero_grad();
                    torch::Tensor loss = torch::nn::functional::cross_entropy(logits(data.features.index_select(0, batch)), targets.index_select(0, batch));
                    Require(torch::isfinite(loss).item<bool>(), "Non-finite training loss.");
                    loss.backward();
                    for (const torch::Tensor& p : parameters) Require(p.grad().defined() && torch::isfinite(p.grad()).all().item<bool>(), "Missing/non-finite training gradient.");
                    optimizer.step(); lossSum += loss.item<double>() * batch.numel();
                }
                {
                    torch::NoGradGuard guard;
                    torch::Tensor scores = logits(data.features.index_select(0, val));
                    valLoss = torch::nn::functional::cross_entropy(scores, targets.index_select(0, val)).item<double>();
                    accuracy = scores.argmax(1).eq(targets.index_select(0, val)).to(torch::kFloat64).mean().item<double>();
                }
                history += QString("%1,%2,%3,%4\n").arg(epoch + 1).arg(lossSum / train.numel(), 0, 'g', 17).arg(valLoss, 0, 'g', 17).arg(accuracy, 0, 'g', 17).toUtf8();
                output << id << " epoch " << epoch + 1 << "/" << Epochs << " validation CE loss=" << valLoss << Qt::endl;
            }
            for (int index = 0; index < int(initialProjections.size()); ++index)
                Require(!torch::equal(parameters[index], initialProjections[index]), "Projection parameter did not update.");
            Require(!torch::equal(initialClassifier, classifier->weight), "Classifier did not update.");
            torch::jit::Module production = RetainClassifier(encoder, classifier->weight, CosineLogitScale);
            const QJsonObject metadata{{"format", ProbabilityFormat}, {"architecture", ProbabilityArchitecture},
                {"model_id", id}, {"embedding_size", EmbeddingSize}, {"output_dim", TemplateSize},
                {"labels", labelsJson}, {"label_order_sha256", labelHash}, {"preprocessing", Preprocessing},
                {"base_encoder_sha256", baseHash}, {"augmentation_manifest_sha256", data.cache_info.value("augmentation_manifest_sha256")},
                {"features_sha256", data.cache_info.value("features_sha256")}, {"augmentation_source_geometry", SourceGeometry},
                {"train_samples", int(train.numel())}, {"validation_originals", int(val.numel())}, {"active_training_id_count", int(active.size())},
                {"original_splits", split}, {"training", QJsonObject{{"seed", int(TrainSeed)}, {"epochs", Epochs},
                    {"batch", BatchSize}, {"optimizer", Optimizer}, {"learning_rate", LearningRate},
                    {"weight_decay", WeightDecay}, {"logit_scale", CosineLogitScale}, {"batch_norm_dropout", "eval"},
                    {"trainable_encoder_parameters", trainedNames}, {"classifier_retained", true}, {"classifier_bias", false},
                    {"validation_loss", valLoss}, {"validation_classification_accuracy", accuracy},
                    {"orders_sha256", QString::fromLatin1(orderHash.result().toHex())}}}};
            const QByteArray json = QJsonDocument(metadata).toJson();
            const QString path = QDir(folder).filePath(id + ".bin");
            production.save((path + ".partial").toStdString(), {{"metadata.json", json.toStdString()}});
            Require(QFile::rename(path + ".partial", path), "Cannot commit new model archive.");
            QJsonObject sidecar = metadata;
            sidecar.insert("weight_sha256", Hash(path)); // Outside the archive: no recursive self-hash.
            Write(QDir(folder).filePath(id + ".json"), QJsonDocument(sidecar).toJson());
            Write(QDir(folder).filePath(id + "_training.csv"), history);
            {
                torch::NoGradGuard guard;
                torch::jit::Module reloaded = torch::jit::load(path.toStdString(), torch::kCPU); reloaded.eval();
                const torch::Tensor featureProbe = data.features.index_select(0, val.slice(0, 0, 4));
                for (int batch : {1, 2, 4}) {
                    const torch::Tensor f = featureProbe.slice(0, 0, batch);
                    const torch::Tensor p = reloaded.run_method("probabilities_from_features", f).toTensor();
                    Require(Probabilities(p, batch) && torch::allclose(p, logits(f).softmax(1), ReloadRtol, ReloadAtol),
                        "Retained classifier save/reload mismatch.");
                    const torch::Tensor image = Prepared(preprocessor, data.samples[valIndices.front()]).repeat({batch, 1, 1, 1});
                    const torch::Tensor full = reloaded.forward({image}).toTensor();
                    Require(Probabilities(full, batch) && torch::allclose(full, production.forward({image}).toTensor(), ReloadRtol, ReloadAtol),
                            "Image-to-probabilities batch/reload check failed.");
                }
            }
            FingerprintEncoder backend; training::Engine engine;
            Require(engine.LoadSelectedModels({id.toStdString()}, folder.toStdString(), &backend), "Cannot reload production weights through Engine.");
            const TrainingImage& sample = data.samples[trainIndices.front()];
            const AlgorithmImage image = training::Engine::ReadImage(sample.path.toStdString(), sample.database.toStdString());
            const training::EnrollmentTemplates enrollment = engine.Enroll(image);
            const training::SimilarityResult self = engine.Compare(enrollment, image);
            Require(enrollment.success && self.success && std::abs(self.score - 1) < SelfScoreTolerance,
                    "Native enrollment/self comparison failed.");
            checks.append(QJsonObject{{"model", id}, {"sha256", Hash(path)}, {"output_dim", TemplateSize},
                {"labels_sha256", labelHash}, {"classifier_retained", true}, {"reload_batch1_2_4", true},
                {"self_score", self.score}, {"template_bytes", int(enrollment.models.front().data.size())}, {"native_engine_verified", true}});
            Write(QDir(folder).filePath("verification.json"), QJsonDocument(checks).toJson());
        }
        EvaluateTrainingRun(folder, data, baseline);
        output << "All four Cosine30 probability models trained, saved, calibrated and verified: " << folder << Qt::endl;
        return 0;
    } catch (const std::exception& exception) {
        QTextStream(stderr) << exception.what() << Qt::endl; return 1;
    }
}

} // namespace algorithm

int main(int argc, char** argv) {
    return algorithm::RunTraining(argc, argv);
}
