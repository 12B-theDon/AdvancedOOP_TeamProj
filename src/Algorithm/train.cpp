#include "augment.h"
#include "encoder.h"
#include "encoderConfig.h"
#include "trainConfig.h"
#include "../Engine/Engine.h"
#include <torch/script.h>
#include <torch/torch.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <memory>

static void Require(bool condition, const QString& error) {
    if (!condition) throw std::runtime_error(error.toStdString());
}

static QByteArray FileHash(const QString& path) {
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Cannot open " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Require(hash.addData(&file), "Cannot hash " + path);
    return hash.result().toHex();
}

static void WriteFile(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    Require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit(), "Cannot write " + path);
}

static AlgorithmImage ReadImage(const QString& path) {
    const auto image = Engine::ReadImage(path.toStdString());
    Require(image.width > 0 && image.height > 0, "Cannot read " + path);
    return image;
}

static torch::Tensor PreparedTensor(FingerprintEncoder& encoder, const QString& path) {
    PreparedImage prepared;
    std::string error;
    Require(encoder.Preprocess("pretrained", ReadImage(path), prepared, error), QString::fromStdString(error));
    return torch::from_blob(prepared.values.data(), {1, 1, prepared.height, prepared.width}, torch::kFloat32).clone();
}

static bool Trainable(const std::string& name) {
    for (const auto& prefix : TrainConfig::TrainablePrefixes)
        if (!prefix.empty() && name.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    try {
        const auto args = app.arguments();
        Require(args.size() == 4 || (args.size() == 5 && args[4] == "--augment-only"),
                "Usage: FingerprintTrain <data/original> <pretrained.bin> <new-output-folder> [--augment-only]");
        const QString dataFolder = QDir(args[1]).absolutePath();
        const QString checkpoint = QDir::cleanPath(QFileInfo(args[2]).absoluteFilePath());
        const QString folder = QDir(args[3]).absolutePath();
        Require(QDir().mkpath(folder), "Cannot create output folder.");
        QStringList ids;
        for (const auto& id : EngineConfig::ModelIds) ids.append(QString::fromStdString(id));
        if (args.size() == 4)
            for (const auto& id : ids)
                Require(!QFile::exists(folder + "/" + id + EngineConfig::WeightExtension), "Preserving existing model: " + id);
        QString augmentationManifest;
        const auto images = PrepareAugmentations(dataFolder, QDir(dataFolder).absoluteFilePath("../augmented"), &augmentationManifest);
        if (args.size() == 5) return 0;
        Require(TrainConfig::Epochs > 0 && TrainConfig::BatchSize > 0 && TrainConfig::FeatureBatchSize > 0
                && TrainConfig::LearningRate > 0 && TrainConfig::ValidationFirstCapture > 1,
                "Invalid training settings.");
        FingerprintEncoder preprocessor;
        std::string error;
        Require(preprocessor.LoadModel("pretrained", checkpoint.toStdString(), error), QString::fromStdString(error));
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        auto base = torch::jit::load(checkpoint.toStdString(), torch::kCPU, extras);
        base.eval();
        auto metadata = QJsonDocument::fromJson(QByteArray::fromStdString(extras["metadata.json"])).object();
        const QString baseHash = QString::fromLatin1(FileHash(checkpoint));
        bool cacheFeatures = true;
        for (const auto& prefix : TrainConfig::TrainablePrefixes)
            if (prefix != "texture_projection." && prefix != "minutia_projection.") cacheFeatures = false;
        QVector<TrainingImage> samples;
        for (const auto& image : images)
            if (!image.validation || !image.augmented) samples.append(image);
        std::vector<torch::Tensor> chunks;
        for (int start = 0; start < samples.size(); start += TrainConfig::FeatureBatchSize) {
            std::vector<torch::Tensor> batch;
            for (int index = start; index < samples.size() && index < start + TrainConfig::FeatureBatchSize; ++index)
                batch.push_back(PreparedTensor(preprocessor, samples[index].path));
            torch::NoGradGuard guard;
            auto input = torch::cat(batch, 0);
            chunks.push_back(cacheFeatures ? base.run_method("features", input).toTensor().clone() : input);
            if (start % (TrainConfig::FeatureBatchSize * TrainConfig::LogEveryFeatureBatches) == 0)
                output << "Preparing features " << start << " / " << samples.size() << Qt::endl;
        }
        auto inputs = torch::cat(chunks, 0);
        chunks.clear();
        QJsonArray checks;
        QStringList databases;
        for (const auto& id : EngineConfig::DatabaseIds) databases.append(QString::fromStdString(id));
        for (int modelIndex = 0; modelIndex < ids.size(); ++modelIndex) {
            // Reload the same original encoder; never initialize from another FVC model.
            torch::manual_seed(TrainConfig::Seed);
            auto model = torch::jit::load(checkpoint.toStdString(), torch::kCPU);
            model.eval();
            std::vector<torch::Tensor> parameters;
            QJsonArray trainedNames;
            for (const auto& parameter : model.named_parameters()) {
                const bool enabled = Trainable(parameter.name);
                parameter.value.set_requires_grad(enabled);
                if (enabled) { parameters.push_back(parameter.value); trainedNames.append(QString::fromStdString(parameter.name)); }
            }
            Require(!parameters.empty(), "No encoder layers selected for fine-tuning.");
            QVector<QString> labels;
            std::vector<int64_t> trainIndices, validationIndices, labelValues(samples.size(), -1);
            QJsonArray split;
            for (int index = 0; index < samples.size(); ++index) {
                const auto& image = samples[index];
                if (modelIndex < databases.size() && image.database != databases[modelIndex]) continue;
                int label = labels.indexOf(image.identity);
                if (label < 0) { label = labels.size(); labels.append(image.identity); }
                labelValues[index] = label;
                (image.validation ? validationIndices : trainIndices).push_back(index);
                if (!image.augmented)
                    split.append(QJsonObject{{"db", image.database}, {"original", image.original},
                                              {"identity", image.identity}, {"split", image.validation ? "validation" : "train"}});
            }
            Require(labels.size() >= 2 && !trainIndices.empty() && !validationIndices.empty(), "Need train and validation captures with at least two fingers.");
            for (int label = 0; label < labels.size(); ++label) {
                bool trainFound = false, validationFound = false;
                for (auto index : trainIndices) if (labelValues[index] == label) trainFound = true;
                for (auto index : validationIndices) if (labelValues[index] == label) validationFound = true;
                Require(trainFound && validationFound, "Each identity needs both train and validation originals.");
            }
            auto targets = torch::tensor(labelValues, torch::kInt64);
            auto train = torch::tensor(trainIndices, torch::kInt64);
            auto validation = torch::tensor(validationIndices, torch::kInt64);
            auto classifier = torch::nn::Linear(torch::nn::LinearOptions(EncoderConfig::EmbeddingSize, labels.size()).bias(false));
            for (const auto& parameter : classifier->parameters()) parameters.push_back(parameter);
            std::unique_ptr<torch::optim::Optimizer> optimizer;
            if (std::string(TrainConfig::Optimizer) == "Adam")
                optimizer.reset(new torch::optim::Adam(parameters, torch::optim::AdamOptions(TrainConfig::LearningRate).weight_decay(TrainConfig::WeightDecay)));
            else if (std::string(TrainConfig::Optimizer) == "SGD")
                optimizer.reset(new torch::optim::SGD(parameters, torch::optim::SGDOptions(TrainConfig::LearningRate).momentum(TrainConfig::Momentum).weight_decay(TrainConfig::WeightDecay)));
            else throw std::runtime_error("Optimizer must be Adam or SGD.");
            auto logits = [&](const torch::Tensor& batch) {
                auto embeddings = cacheFeatures ? model.run_method("embed_features", batch).toTensor() : model.forward({batch}).toTensor();
                auto weights = torch::nn::functional::normalize(classifier->weight, torch::nn::functional::NormalizeFuncOptions().dim(1));
                return embeddings.matmul(weights.t()) * TrainConfig::CosineLogitScale;
            };
            QByteArray history("epoch,train_loss,validation_loss,validation_classification_accuracy\n");
            double finalValidationLoss = 0, finalAccuracy = 0;
            torch::Tensor initialProjection;
            for (const auto& parameter : model.named_parameters())
                if (Trainable(parameter.name)) { initialProjection = parameter.value.detach().clone(); break; }
            for (int epoch = 1; epoch <= TrainConfig::Epochs; ++epoch) {
                const auto order = train.index_select(0, torch::randperm(train.numel(), torch::kInt64));
                double lossSum = 0;
                for (int start = 0; start < order.numel(); start += TrainConfig::BatchSize) {
                    const auto indices = order.slice(0, start, start + TrainConfig::BatchSize);
                    optimizer->zero_grad();
                    const auto loss = torch::nn::functional::cross_entropy(logits(inputs.index_select(0, indices)), targets.index_select(0, indices));
                    Require(torch::isfinite(loss).item<bool>(), "Non-finite training loss.");
                    loss.backward();
                    optimizer->step();
                    lossSum += loss.item<double>() * indices.numel();
                }
                double validationSum = 0, correct = 0;
                {
                    torch::NoGradGuard guard;
                    for (int start = 0; start < validation.numel(); start += TrainConfig::BatchSize) {
                        const auto indices = validation.slice(0, start, start + TrainConfig::BatchSize);
                        const auto scores = logits(inputs.index_select(0, indices));
                        const auto expected = targets.index_select(0, indices);
                        validationSum += torch::nn::functional::cross_entropy(scores, expected).item<double>() * indices.numel();
                        correct += scores.argmax(1).eq(expected).sum().item<double>();
                    }
                }
                finalValidationLoss = validationSum / validation.numel();
                finalAccuracy = correct / validation.numel();
                history += QString("%1,%2,%3,%4\n").arg(epoch).arg(lossSum / train.numel(), 0, 'g', 10)
                               .arg(finalValidationLoss, 0, 'g', 10).arg(finalAccuracy, 0, 'g', 10).toUtf8();
                output << ids[modelIndex] << " epoch " << epoch << "/" << TrainConfig::Epochs
                       << " loss=" << lossSum / train.numel() << " validation=" << finalValidationLoss << Qt::endl;
            }
            bool changed = false;
            for (const auto& parameter : model.named_parameters())
                if (Trainable(parameter.name)) { changed = !torch::equal(initialProjection, parameter.value); break; }
            Require(changed, "Fine-tuning did not update encoder weights.");
            auto modelMetadata = metadata;
            modelMetadata.insert("model_id", ids[modelIndex]);
            modelMetadata.insert("base_encoder_sha256", baseHash);
            modelMetadata.insert("augmentation_manifest", augmentationManifest);
            modelMetadata.insert("augmentation_manifest_sha256", QString::fromLatin1(FileHash(augmentationManifest)));
            modelMetadata.insert("data_scope", modelIndex < databases.size() ? databases[modelIndex] : databases.join("+"));
            modelMetadata.insert("original_splits", split);
            modelMetadata.insert("train_samples", int(train.numel()));
            modelMetadata.insert("validation_originals", int(validation.numel()));
            modelMetadata.insert("labels", QJsonArray::fromStringList(QStringList(labels.begin(), labels.end())));
            modelMetadata.insert("training", QJsonObject{{"seed", int(TrainConfig::Seed)}, {"epochs", TrainConfig::Epochs},
                {"batch", TrainConfig::BatchSize}, {"learning_rate", TrainConfig::LearningRate}, {"optimizer", TrainConfig::Optimizer},
                {"weight_decay", TrainConfig::WeightDecay}, {"loss", "cosine-classifier cross entropy"},
                {"logit_scale", TrainConfig::CosineLogitScale}, {"trainable_parameters", trainedNames}, {"cached_frozen_features", cacheFeatures},
                {"batch_norm_dropout", "eval"}, {"encoder_weights_changed", changed},
                {"validation_loss", finalValidationLoss}, {"validation_classification_accuracy", finalAccuracy}});
            const QString path = folder + "/" + ids[modelIndex] + EngineConfig::WeightExtension;
            const QByteArray json = QJsonDocument(modelMetadata).toJson();
            model.save((path + ".partial").toStdString(), {{"metadata.json", json.toStdString()}});
            Require(QFile::rename(path + ".partial", path), "Cannot commit trained model.");
            WriteFile(folder + "/" + ids[modelIndex] + ".json", json);
            WriteFile(folder + "/" + ids[modelIndex] + "_training.csv", history);
            FingerprintEncoder backend;
            Engine engine;
            Require(engine.LoadSelectedModels({ids[modelIndex].toStdString()}, folder.toStdString(), &backend), "Cannot reload saved model.");
            const auto enrollmentImage = ReadImage(samples[trainIndices.front()].path);
            const auto attemptImage = ReadImage(samples[validationIndices.front()].path);
            const auto enrollment = engine.Enroll(enrollmentImage);
            const auto self = engine.Compare(enrollment, enrollmentImage);
            const auto result = engine.Compare(enrollment, attemptImage);
            int64_t impostorIndex = -1;
            for (auto index : validationIndices)
                if (labelValues[index] != labelValues[trainIndices.front()]) { impostorIndex = index; break; }
            Require(impostorIndex >= 0, "No impostor verification image.");
            const auto impostor = engine.Compare(enrollment, ReadImage(samples[impostorIndex].path));
            Require(enrollment.success && self.success && std::abs(self.score - 1.0) < TrainConfig::SelfScoreTolerance && result.success,
                    "Reloaded Engine enrollment/attempt verification failed.");
            Require(impostor.success, "Reloaded Engine impostor verification failed.");
            {
                torch::NoGradGuard guard;
                auto reloaded = torch::jit::load(path.toStdString(), torch::kCPU);
                const auto probe = PreparedTensor(preprocessor, samples[validationIndices.front()].path);
                Require(torch::allclose(model.forward({probe}).toTensor(), reloaded.forward({probe}).toTensor(), TrainConfig::ReloadRtol, TrainConfig::ReloadAtol),
                        "Saved encoder differs after reload.");
            }
            checks.append(QJsonObject{{"model", ids[modelIndex]}, {"file", path}, {"sha256", QString::fromLatin1(FileHash(path))},
                {"base_encoder_sha256", baseHash}, {"reload_equal", true}, {"score_type", EngineConfig::ScoreType},
                {"self_score", self.score}, {"genuine_score", result.score}, {"impostor_score", impostor.score},
                {"template_bytes", int(enrollment.models.front().data.size())}});
            WriteFile(folder + "/verification.json", QJsonDocument(QJsonObject{{"models", checks}}).toJson());
        }
        output << "All four models trained, reloaded and verified through Engine: " << folder << Qt::endl;
        return 0;
    } catch (const std::exception& exception) {
        QTextStream(stderr) << exception.what() << Qt::endl;
        return 1;
    }
}
