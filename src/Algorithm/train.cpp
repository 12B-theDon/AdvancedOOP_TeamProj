#include <torch/script.h> // Before Qt's slots macro.
#include <torch/torch.h>
#include "train.h"
#include "augmentConfig.h"
#include "encoder.h"
#include "encoderConfig.h"
#include "trainConfig.h"
#include "../Engine/Engine.h"
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
    Require(encoder.Preprocess("pretrained", engine::Engine::ReadImage(image.path.toStdString(), image.database.toStdString()), prepared, error),
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

namespace {
struct Pair { int enrollment, probe; bool genuine; };
struct Scores { std::vector<double> value; std::vector<bool> genuine; };
struct Metrics { double threshold, tar, far, eer, auc; int genuine, impostor; };
QString Number(double value) { return QString::number(value, 'g', 17); }
QJsonObject Json(const Metrics& m) {
    return {{"threshold", m.threshold}, {"genuine_accept_pct", m.tar * 100}, {"false_accept_pct", m.far * 100},
        {"impostor_reject_pct", (1 - m.far) * 100}, {"eer_pct", m.eer * 100}, {"auc", m.auc},
        {"genuine_pairs", m.genuine}, {"impostor_pairs", m.impostor}, {"target_met", m.far <= engine::TargetFar + 1e-12}};
}
double AcceptanceRate(const std::vector<double>& values, double threshold) {
    return double(values.end() - std::lower_bound(values.begin(), values.end(), threshold)) / values.size();
}
Metrics Measure(const Scores& scores, double fixed = -1) {
    std::vector<double> genuineScores, impostorScores;
    for (std::size_t n = 0; n < scores.value.size(); ++n) (scores.genuine[n] ? genuineScores : impostorScores).push_back(scores.value[n]);
    Require(!genuineScores.empty() && !impostorScores.empty(), "Need genuine and impostor validation pairs.");
    std::sort(genuineScores.begin(), genuineScores.end()); std::sort(impostorScores.begin(), impostorScores.end());
    const int allowed = int(std::floor(engine::TargetFar * impostorScores.size() + 1e-12));
    double threshold = std::nextafter(impostorScores[impostorScores.size() - allowed - 1], std::numeric_limits<double>::infinity());
    const double precision = std::pow(10., engine::ThresholdPrecision);
    threshold = std::min(1., std::ceil(threshold * precision) / precision);
    if (fixed >= 0) threshold = fixed;
    double auc = 0;
    for (double value : genuineScores) {
        const std::vector<double>::const_iterator low = std::lower_bound(impostorScores.begin(), impostorScores.end(), value), high = std::upper_bound(impostorScores.begin(), impostorScores.end(), value);
        auc += double(low - impostorScores.begin()) + 0.5 * (high - low);
    }
    auc /= double(genuineScores.size()) * impostorScores.size();
    std::vector<double> thresholds = genuineScores;
    thresholds.insert(thresholds.end(), impostorScores.begin(), impostorScores.end());
    std::sort(thresholds.begin(), thresholds.end());
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end()), thresholds.end());
    thresholds.push_back(std::numeric_limits<double>::infinity());
    double previousFar = 1, previousFnmr = 0, eer = 1;
    for (double t : thresholds) {
        const double far = AcceptanceRate(impostorScores, t), fnmr = 1 - AcceptanceRate(genuineScores, t), delta = far - fnmr;
        if (std::abs(delta) < 1e-14) { eer = far; break; }
        if (delta < 0) {
            const double ratio = (previousFar - previousFnmr) / (previousFar - previousFnmr - delta);
            eer = previousFar + ratio * (far - previousFar); break;
        }
        previousFar = far; previousFnmr = fnmr;
    }
    return {threshold, AcceptanceRate(genuineScores, threshold), AcceptanceRate(impostorScores, threshold), eer, auc, int(genuineScores.size()), int(impostorScores.size())};
}
double Score(const torch::Tensor& rows, int a, int b) {
    const torch::Tensor x = rows[a].to(torch::kFloat64), y = rows[b].to(torch::kFloat64);
    return std::clamp((torch::dot(x, y).item<double>() / (x.norm().item<double>() * y.norm().item<double>()) + 1) / 2, 0., 1.);
}
Scores Pool(const Scores groups[3]) {
    Scores result;
    for (int db = 0; db < 3; ++db) {
        result.value.insert(result.value.end(), groups[db].value.begin(), groups[db].value.end());
        result.genuine.insert(result.genuine.end(), groups[db].genuine.begin(), groups[db].genuine.end());
    }
    return result;
}
}

void EvaluateTrainingRun(const QString& folder, const TrainingData& data, const QString& baselineFolder) {
    torch::NoGradGuard guard;
    QVector<TrainingImage> originals;
    std::vector<int64_t> indices;
    for (int n = 0; n < data.samples.size(); ++n) if (!data.samples[n].augmented) {
        originals.append(data.samples[n]); indices.push_back(n);
    }
    torch::Tensor features = data.features.index_select(0, torch::tensor(indices, torch::kInt64));
    std::vector<Pair> pairs[3];
    for (int db = 0; db < 3; ++db) {
        const QString name = QString::fromStdString(engine::DatabaseIds[db]);
        for (int a = 0; a < originals.size(); ++a) if (originals[a].database == name && !originals[a].validation)
            for (int b = 0; b < originals.size(); ++b) if (originals[b].database == name && originals[b].validation)
                pairs[db].push_back({a, b, originals[a].identity == originals[b].identity});
        Require(pairs[db].size() == 1200, "Expected60 gallery and20 validation originals per DB.");
    }
    Scores probabilities[4][3];
    QJsonObject hashes;
    QByteArray raw("representation,model,database,enrollment,probe,genuine,score\n");
    QByteArray metricsCsv("representation,model,database,eer_pct,auc,threshold_at_far1,genuine_accept_pct,false_accept_pct,genuine_pairs,impostor_pairs\n");
    QJsonArray summaries;
    torch::jit::Module reference = torch::jit::load(data.checkpoint.toStdString(), torch::kCPU); reference.eval();
    std::vector<torch::jit::Named<torch::Tensor>> referenceParameters, referenceBuffers;
    for (const torch::jit::Named<torch::Tensor>& parameter : reference.named_parameters()) referenceParameters.push_back(parameter);
    for (const torch::jit::Named<torch::Tensor>& buffer : reference.named_buffers()) referenceBuffers.push_back(buffer);
    QJsonArray frozenChecks;
    auto record = [&](const QString& kind, int modelIndex, const Scores groups[3]) -> void {
        const QString id = QString::fromStdString(engine::ModelIds[modelIndex]);
        for (int db = 0; db <= 3; ++db) {
            const Metrics m = db == 3 ? Measure(Pool(groups)) : Measure(groups[db]);
            const QString database = db == 3 ? QString("pooled_within_db") : QString::fromStdString(engine::DatabaseIds[db]);
            QJsonObject json = Json(m); json.insert("representation", kind); json.insert("model", id); json.insert("database", database);
            json.insert("model_scope", db == 3 ? modelIndex == 3 : modelIndex == db);
            summaries.append(json);
            metricsCsv += (kind + ',' + id + ',' + database + ',' + Number(m.eer * 100) + ',' + Number(m.auc) + ',' + Number(m.threshold) + ','
                + Number(m.tar * 100) + ',' + Number(m.far * 100) + ',' + QString::number(m.genuine) + ',' + QString::number(m.impostor) + '\n').toUtf8();
        }
    };
    auto makeGroups = [&](const QString& kind, int modelIndex, const torch::Tensor& values, Scores groups[3]) -> void {
        for (int db = 0; db < 3; ++db) groups[db] = Scores();
        for (int db = 0; db < 3; ++db) for (const Pair& pair : pairs[db]) {
            const double score = Score(values, pair.enrollment, pair.probe);
            groups[db].value.push_back(score); groups[db].genuine.push_back(pair.genuine);
            raw += (kind + ',' + QString::fromStdString(engine::ModelIds[modelIndex]) + ',' + originals[pair.enrollment].database + ','
                + originals[pair.enrollment].original + ',' + originals[pair.probe].original + ',' + (pair.genuine ? "1," : "0,") + Number(score) + '\n').toUtf8();
        }
        record(kind, modelIndex, groups);
    };
    QString labelHash;
    for (int modelIndex = 0; modelIndex < 4; ++modelIndex) {
        const QString id = QString::fromStdString(engine::ModelIds[modelIndex]), path = QDir(folder).filePath(id + ".bin");
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        torch::jit::Module model = torch::jit::load(path.toStdString(), torch::kCPU, extras); model.eval();
        const QJsonObject metadata = QJsonDocument::fromJson(QByteArray::fromStdString(extras["metadata.json"])).object();
        Require(metadata.value("format").toString() == ProbabilityFormat, "Not a v2 probability model.");
        Require(metadata.value("base_encoder_sha256").toString() == data.cache_info.value("base_sha256").toString(),
                "Model and feature cache use different pretrained producers.");
        const torch::jit::Module trainedEncoder = model.attr("encoder").toModule();
        int frozenParameters = 0, frozenBuffers = 0, parameterIndex = 0, bufferIndex = 0;
        for (const torch::jit::Named<torch::Tensor>& p : trainedEncoder.named_parameters()) {
            Require(parameterIndex < int(referenceParameters.size()) && p.name == referenceParameters[parameterIndex].name, "Encoder parameter order changed.");
            const bool projection = Trainable(p.name);
            if (!projection) {
                Require(torch::equal(p.value, referenceParameters[parameterIndex].value), "Frozen encoder parameter changed.");
                ++frozenParameters;
            }
            ++parameterIndex;
        }
        for (const torch::jit::Named<torch::Tensor>& b : trainedEncoder.named_buffers()) {
            Require(bufferIndex < int(referenceBuffers.size()) && b.name == referenceBuffers[bufferIndex].name
                && torch::equal(b.value, referenceBuffers[bufferIndex].value), "Frozen BatchNorm/Gabor buffer changed.");
            ++bufferIndex;
            ++frozenBuffers;
        }
        Require(parameterIndex == int(referenceParameters.size()) && bufferIndex == int(referenceBuffers.size()), "Encoder structure changed.");
        frozenChecks.append(QJsonObject{{"model", id}, {"frozen_parameter_tensors_equal", frozenParameters},
            {"frozen_buffer_tensors_equal", frozenBuffers}, {"all_equal_to_pretrained", true}});
        const QJsonArray labels = metadata.value("labels").toArray();
        if (labelHash.isEmpty()) labelHash = LabelHash(labels);
        Require(labelHash == LabelHash(labels), "Models have different Label orders.");
        hashes.insert(id, Hash(path));
        const torch::Tensor p = model.run_method("probabilities_from_features", features).toTensor();
        Require(Probabilities(p, originals.size()), "Invalid validation probabilities.");
        makeGroups("probability30", modelIndex, p, probabilities[modelIndex]);
        Scores diagnostic[3];
        makeGroups("same_encoder512", modelIndex, model.run_method("embed_features", features).toTensor(), diagnostic);
        if (!baselineFolder.isEmpty()) {
            const QString oldPath = QDir(baselineFolder).filePath(id + ".bin");
            const QString oldHash = Hash(oldPath);
            torch::jit::ExtraFilesMap oldExtras{{"metadata.json", ""}};
            torch::jit::Module old = torch::jit::load(oldPath.toStdString(), torch::kCPU, oldExtras); old.eval();
            const QJsonObject oldMeta = QJsonDocument::fromJson(QByteArray::fromStdString(oldExtras["metadata.json"])).object();
            Require(oldMeta.value("format").toString() == "deepprint-torchscript-v1"
                && oldMeta.value("model_id").toString() == id
                && oldMeta.value("base_encoder_sha256").toString() == data.cache_info.value("base_sha256").toString(),
                "Baseline does not share the same frozen feature producer.");
            makeGroups("previous_saved512", modelIndex, old.run_method("embed_features", features).toTensor(), diagnostic);
            Require(Hash(oldPath) == oldHash, "Baseline changed during evaluation.");
        }
    }
    QJsonArray entries;
    QByteArray calibrationCsv("database,model_mask,threshold,genuine_accept_pct,false_accept_pct,target_met\n");
    for (int mask = 1; mask < 16; ++mask) {
        QJsonArray selected;
        for (int m = 0; m < 4; ++m) if (mask & (1 << m)) selected.append(QString::fromStdString(engine::ModelIds[m]));
        Scores best[3];
        double conservative = 0;
        for (int db = 0; db < 3; ++db) {
            for (std::size_t index = 0; index < pairs[db].size(); ++index) {
                double value = 0;
                for (int m = 0; m < 4; ++m) if (mask & (1 << m)) value = std::max(value, probabilities[m][db].value[index]);
                best[db].value.push_back(value); best[db].genuine.push_back(pairs[db][index].genuine);
            }
            const Metrics m = Measure(best[db]);
            conservative = std::max(conservative, m.threshold);
            QJsonObject entry = Json(m); entry.insert("database", QString::fromStdString(engine::DatabaseIds[db]));
            entry.insert("model_mask", mask); entry.insert("models", selected); entries.append(entry);
            calibrationCsv += (entry.value("database").toString() + ',' + QString::number(mask) + ',' + Number(m.threshold) + ','
                + Number(m.tar * 100) + ',' + Number(m.far * 100) + ',' + (m.far <= engine::TargetFar + 1e-12 ? "true\n" : "false\n")).toUtf8();
        }
        // Unknown external DB: conservative across the three known DB validation cutoffs.
        const Metrics m = Measure(Pool(best), conservative);
        QJsonObject entry = Json(m); entry.insert("database", "*"); entry.insert("model_mask", mask); entry.insert("models", selected);
        entry.insert("external_data_validated", false); entries.append(entry);
        calibrationCsv += ("*," + QString::number(mask) + ',' + Number(m.threshold) + ',' + Number(m.tar * 100) + ','
            + Number(m.far * 100) + ',' + (m.far <= engine::TargetFar + 1e-12 ? "true\n" : "false\n")).toUtf8();
    }
    const QJsonObject calibration{{"format", "fingerprint-calibration-v2"}, {"template_format", TemplateFormat},
        {"score_type", engine::ScoreType}, {"label_order_sha256", labelHash}, {"weights", hashes},
        {"target_far", engine::TargetFar}, {"threshold_decimals", engine::ThresholdPrecision},
        {"basis", "known-ID original captures1-6 gallery vs7-8 probes; same validation used to choose thresholds; not independent test"},
        {"aggregation", "maximum successful model score; every nonempty subset calibrated"}, {"entries", entries}};
    Write(QDir(folder).filePath("validation_scores.csv"), raw);
    Write(QDir(folder).filePath("validation_metrics.csv"), metricsCsv);
    Write(QDir(folder).filePath("validation_metrics.json"), QJsonDocument(summaries).toJson());
    // Recheck the cache-based audit against actual native image/Template/Compare calls.
    FingerprintEncoder backend; engine::Engine engine;
    Require(engine.LoadSelectedModels(engine::ModelIds, folder.toStdString(), &backend), "Cannot load final models for native verification.");
    for (const engine::ModelState& state : engine.Models()) Require(state.loaded, QString::fromStdString(state.error));
    QJsonArray runtime;
    for (int db = 0; db < 3; ++db) {
        const int a = pairs[db].front().enrollment;
        int positive = -1, negative = -1;
        for (int n = 0; n < int(pairs[db].size()); ++n) if (pairs[db][n].enrollment == a) {
            if (pairs[db][n].genuine && positive < 0) positive = n;
            if (!pairs[db][n].genuine && negative < 0) negative = n;
        }
        const TrainingImage& source = originals[a];
        const engine::EnrollmentTemplates enrollment = engine.Enroll(engine::Engine::ReadImage(source.path.toStdString(), source.database.toStdString()));
        Require(enrollment.success, "Native audit enrollment failed.");
        for (int index : {positive, negative}) {
            Require(index >= 0, "Missing native audit probe.");
            const TrainingImage& probe = originals[pairs[db][index].probe];
            const engine::SimilarityResult compared = engine.Compare(enrollment, engine::Engine::ReadImage(probe.path.toStdString(), probe.database.toStdString()));
            Require(compared.success, "Native audit comparison failed.");
            for (int m = 0; m < 4; ++m) {
                const engine::ModelScore& score = compared.model_scores[m];
                const double error = std::abs(score.score - probabilities[m][db].value[index]);
                Require(score.success && error < 1e-5, "Native image and feature-cache scores disagree.");
                runtime.append(QJsonObject{{"model", QString::fromStdString(score.model_id)}, {"database", source.database},
                    {"enrollment", source.original}, {"probe", probe.original}, {"score", score.score}, {"cache_score_error", error}});
            }
        }
    }
    Write(QDir(folder).filePath("native_audit.json"), QJsonDocument(runtime).toJson());
    // Publish recommendations only after native image and frozen-backbone verification.
    Write(QDir(folder).filePath(engine::CalibrationFile), QJsonDocument(calibration).toJson());
    Write(QDir(folder).filePath("calibration.csv"), calibrationCsv);
    Write(QDir(folder).filePath("frozen_backbone_verification.json"), QJsonDocument(frozenChecks).toJson());
    QString report = "<!doctype html><html lang='ko'><meta charset='utf-8'><title>Final production validation</title>"
        "<style>body{font:15px/1.6 system-ui;max-width:1300px;margin:30px auto}table{border-collapse:collapse;width:100%;margin:20px 0}td,th{border:1px solid #ccd5df;padding:6px;text-align:right}td:first-child{text-align:left}pre{white-space:pre-wrap}</style>"
        "<h1>Production Cosine30 validation — Seed42,20Epoch</h1><p>확률 Template: normalized512 embedding → retained cosine classifier ×16 → Softmax30. "
        "같은 Weight의512 특징 및 실제 이전 저장512 Weight와 구분해 비교한다. 학습 ID의 다른 촬영본 검증이며 독립 Test가 아니다. "
        "아래 임계값은 같은 Validation의 FAR1% 목표로 고른 값이다. 5Seed 실험 평균과 섞지 않는다.</p>"
        "<table><tr><th>Representation / model / DB</th><th>EER % ↓</th><th>AUC ↑</th><th>Threshold</th><th>Genuine %</th><th>FAR %</th></tr>";
    for (const QJsonValue& value : summaries) {
        const QJsonObject row = value.toObject();
        if (!row.value("model_scope").toBool()) continue;
        report += "<tr><td>" + row.value("representation").toString() + " / " + row.value("model").toString() + " / " + row.value("database").toString() + "</td>";
        for (const char* key : {"eer_pct", "auc", "threshold", "genuine_accept_pct", "false_accept_pct"})
            report += "<td>" + Number(row.value(key).toDouble()) + "</td>";
        report += "</tr>";
    }
    report += "</table><h2>GUI single / Best-score calibration</h2><p>Thresholds are rounded upward to four decimals. "
        "target_met=false means FAR1% is not attainable at the supported threshold precision for that selection. "
        "External fallback is conservative only on known validation DBs; it is not external-DB calibration.</p><pre>"
        + QString::fromUtf8(QJsonDocument(calibration).toJson()).toHtmlEscaped() + "</pre><p>"
        "<a href='validation_metrics.csv'>Metrics</a> · <a href='validation_scores.csv'>Pair scores</a> · "
        "<a href='calibration.csv'>All60 model-subset/DB cutoffs</a> · <a href='native_audit.json'>Native image checks</a></p></html>";
    Write(QDir(folder).filePath("validation_report.html"), report.toUtf8());
    QTextStream(stdout) << "Validated24 native model comparisons; calibrated60 DB/model-subset operating points." << Qt::endl;
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
        for (const std::string& id : engine::ModelIds)
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
            const QString id = QString::fromStdString(engine::ModelIds[modelIndex]);
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
                if (modelIndex < 3 && image.database.toStdString() != engine::DatabaseIds[modelIndex]) continue;
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
                output << id << " epoch " << epoch + 1 << "/" << Epochs << " validation=" << valLoss << Qt::endl;
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
            FingerprintEncoder backend; engine::Engine engine;
            Require(engine.LoadSelectedModels({id.toStdString()}, folder.toStdString(), &backend), "Cannot reload production weights through Engine.");
            const TrainingImage& sample = data.samples[trainIndices.front()];
            const AlgorithmImage image = engine::Engine::ReadImage(sample.path.toStdString(), sample.database.toStdString());
            const engine::EnrollmentTemplates enrollment = engine.Enroll(image);
            const engine::SimilarityResult self = engine.Compare(enrollment, image);
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
