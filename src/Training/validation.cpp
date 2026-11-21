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
namespace {
struct Pair { int enrollment, probe; bool genuine; };
struct Scores { std::vector<double> value; std::vector<bool> genuine; };
struct Metrics { double threshold, tar, far, eer, auc; int genuine, impostor; };
QString Number(double value) { return QString::number(value, 'g', 17); }
QJsonObject Json(const Metrics& m) {
    return {{"threshold", m.threshold}, {"genuine_accept_pct", m.tar * 100}, {"false_accept_pct", m.far * 100},
        {"impostor_reject_pct", (1 - m.far) * 100}, {"eer_pct", m.eer * 100}, {"auc", m.auc},
        {"genuine_pairs", m.genuine}, {"impostor_pairs", m.impostor}, {"target_met", m.far <= training::TargetFar + 1e-12}};
}
double AcceptanceRate(const std::vector<double>& values, double threshold) {
    return double(values.end() - std::lower_bound(values.begin(), values.end(), threshold)) / values.size();
}
Metrics Measure(const Scores& scores, double fixed = -1) {
    std::vector<double> genuineScores, impostorScores;
    for (std::size_t n = 0; n < scores.value.size(); ++n) (scores.genuine[n] ? genuineScores : impostorScores).push_back(scores.value[n]);
    Require(!genuineScores.empty() && !impostorScores.empty(), "Need genuine and impostor validation pairs.");
    std::sort(genuineScores.begin(), genuineScores.end()); std::sort(impostorScores.begin(), impostorScores.end());
    const int allowed = int(std::floor(training::TargetFar * impostorScores.size() + 1e-12));
    double threshold = std::nextafter(impostorScores[impostorScores.size() - allowed - 1], std::numeric_limits<double>::infinity());
    const double precision = std::pow(10., training::ThresholdPrecision);
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
        const QString name = QString::fromStdString(training::DatabaseIds[db]);
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
    torch::jit::Module reference = LoadArchive(data.checkpoint); reference.eval();
    std::vector<torch::jit::Named<torch::Tensor>> referenceParameters, referenceBuffers;
    for (const torch::jit::Named<torch::Tensor>& parameter : reference.named_parameters()) referenceParameters.push_back(parameter);
    for (const torch::jit::Named<torch::Tensor>& buffer : reference.named_buffers()) referenceBuffers.push_back(buffer);
    QJsonArray frozenChecks;
    auto record = [&](const QString& kind, int modelIndex, const Scores groups[3]) -> void {
        const QString id = QString::fromStdString(training::ModelIds[modelIndex]);
        for (int db = 0; db <= 3; ++db) {
            const Metrics m = db == 3 ? Measure(Pool(groups)) : Measure(groups[db]);
            const QString database = db == 3 ? QString("pooled_within_db") : QString::fromStdString(training::DatabaseIds[db]);
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
            raw += (kind + ',' + QString::fromStdString(training::ModelIds[modelIndex]) + ',' + originals[pair.enrollment].database + ','
                + originals[pair.enrollment].original + ',' + originals[pair.probe].original + ',' + (pair.genuine ? "1," : "0,") + Number(score) + '\n').toUtf8();
        }
        record(kind, modelIndex, groups);
    };
    QString labelHash;
    for (int modelIndex = 0; modelIndex < 4; ++modelIndex) {
        const QString id = QString::fromStdString(training::ModelIds[modelIndex]), path = QDir(folder).filePath(id + ".bin");
        torch::jit::ExtraFilesMap extras{{"metadata.json", ""}};
        torch::jit::Module model = LoadArchive(path, extras); model.eval();
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
            torch::jit::Module old = LoadArchive(oldPath, oldExtras); old.eval();
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
    double externalCutoff = 0;
    for (int db = 0; db < 3; ++db) {
        // Only the matching DB model and/or combined can contribute to a decision.
        for (int mask : {1 << db, 8, (1 << db) | 8}) {
            QJsonArray selected;
            for (int m = 0; m < 4; ++m)
                if (mask & (1 << m)) selected.append(QString::fromStdString(training::ModelIds[m]));
            Scores minimum;
            for (std::size_t index = 0; index < pairs[db].size(); ++index) {
                double value = 1;
                for (int m = 0; m < 4; ++m)
                    if (mask & (1 << m)) value = std::min(value, probabilities[m][db].value[index]);
                minimum.value.push_back(value);
                minimum.genuine.push_back(pairs[db][index].genuine);
            }
            const Metrics m = Measure(minimum);
            if (mask == 8) externalCutoff = std::max(externalCutoff, m.threshold);
            QJsonObject entry = Json(m);
            entry.insert("database", QString::fromStdString(training::DatabaseIds[db]));
            entry.insert("model_mask", mask); entry.insert("models", selected); entries.append(entry);
            calibrationCsv += (entry.value("database").toString() + ',' + QString::number(mask) + ',' + Number(m.threshold) + ','
                + Number(m.tar * 100) + ',' + Number(m.far * 100) + ',' + (m.far <= training::TargetFar + 1e-12 ? "true\n" : "false\n")).toUtf8();
        }
    }
    // Unknown DB: combined only, with a conservative cutoff from the known DBs.
    const Metrics external = Measure(Pool(probabilities[3]), externalCutoff);
    QJsonObject entry = Json(external);
    entry.insert("database", "*"); entry.insert("model_mask", 8);
    entry.insert("models", QJsonArray{QString::fromStdString(training::ModelIds[3])});
    entry.insert("external_data_validated", false); entries.append(entry);
    calibrationCsv += ("*,8," + Number(external.threshold) + ',' + Number(external.tar * 100) + ',' + Number(external.far * 100) + ','
        + (external.far <= training::TargetFar + 1e-12 ? "true\n" : "false\n")).toUtf8();
    const QJsonObject calibration{{"format", training::CalibrationFormat}, {"template_format", TemplateFormat},
        {"score_type", training::ScoreType}, {"label_order_sha256", labelHash}, {"weights", hashes},
        {"target_far", training::TargetFar}, {"threshold_decimals", training::ThresholdPrecision},
        {"basis", "known-ID original captures1-6 gallery vs7-8 probes; same validation used to choose thresholds; not independent test"},
        {"aggregation", training::ScoreAggregation}, {"entries", entries}};
    Write(QDir(folder).filePath("validation_scores.csv"), raw);
    Write(QDir(folder).filePath("validation_metrics.csv"), metricsCsv);
    Write(QDir(folder).filePath("validation_metrics.json"), QJsonDocument(summaries).toJson());
    // Recheck the cache-based audit against actual native image/Template/Compare calls.
    FingerprintEncoder backend; training::Engine engine;
    Require(engine.LoadSelectedModels(training::ModelIds, folder.toStdString(), &backend), "Cannot load final models for native verification.");
    for (const training::ModelState& state : engine.Models()) Require(state.loaded, QString::fromStdString(state.error));
    QJsonArray runtime;
    for (int db = 0; db < 3; ++db) {
        const int a = pairs[db].front().enrollment;
        int positive = -1, negative = -1;
        for (int n = 0; n < int(pairs[db].size()); ++n) if (pairs[db][n].enrollment == a) {
            if (pairs[db][n].genuine && positive < 0) positive = n;
            if (!pairs[db][n].genuine && negative < 0) negative = n;
        }
        const TrainingImage& source = originals[a];
        const training::EnrollmentTemplates enrollment = engine.Enroll(training::Engine::ReadImage(source.path.toStdString(), source.database.toStdString()));
        Require(enrollment.success, "Native audit enrollment failed.");
        for (int index : {positive, negative}) {
            Require(index >= 0, "Missing native audit probe.");
            const TrainingImage& probe = originals[pairs[db][index].probe];
            const training::SimilarityResult compared = engine.Compare(enrollment, training::Engine::ReadImage(probe.path.toStdString(), probe.database.toStdString()));
            Require(compared.success, "Native audit comparison failed.");
            const double expectedMinimum = std::min(probabilities[db][db].value[index], probabilities[3][db].value[index]);
            Require(std::abs(compared.score - expectedMinimum) < 1e-5, "Native minimum-score decision disagrees with cached scores.");
            for (int m = 0; m < 4; ++m) {
                const training::ModelScore& score = compared.model_scores[m];
                const double error = std::abs(score.score - probabilities[m][db].value[index]);
                Require(score.success && error < 1e-5, "Native image and feature-cache scores disagree.");
                runtime.append(QJsonObject{{"model", QString::fromStdString(score.model_id)}, {"database", source.database},
                    {"enrollment", source.original}, {"probe", probe.original}, {"score", score.score}, {"cache_score_error", error}, {"decision_score", compared.score}, {"decision_model", QString::fromStdString(compared.model_id)}});
            }
        }
    }
    Write(QDir(folder).filePath("native_audit.json"), QJsonDocument(runtime).toJson());
    // Publish recommendations only after native image and frozen-backbone verification.
    Write(QDir(folder).filePath(training::CalibrationFile), QJsonDocument(calibration).toJson());
    Write(QDir(folder).filePath("calibration.csv"), calibrationCsv);
    Write(QDir(folder).filePath("frozen_backbone_verification.json"), QJsonDocument(frozenChecks).toJson());
    QString report = "<!doctype html><html lang='ko'><meta charset='utf-8'><title>Final production validation</title>"
        "<style>body{font:15px/1.6 system-ui;max-width:1300px;margin:30px auto}table{border-collapse:collapse;width:100%;margin:20px 0}td,th{border:1px solid #ccd5df;padding:6px;text-align:right}td:first-child{text-align:left}pre{white-space:pre-wrap}</style>"
        "<h1>Production Cosine30 validation</h1><p>확률 Template: normalized512 embedding → retained cosine classifier ×16 → Softmax30. "
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
    report += "</table><h2>GUI minimum-score calibration</h2><p>Thresholds are rounded upward to four decimals. "
        "target_met=false means FAR1% is not attainable at the supported threshold precision for that selection. "
        "External fallback is conservative only on known validation DBs; it is not external-DB calibration.</p><pre>"
        + QString::fromUtf8(QJsonDocument(calibration).toJson()).toHtmlEscaped() + "</pre><p>"
        "<a href='validation_metrics.csv'>Metrics</a> · <a href='validation_scores.csv'>Pair scores</a> · "
        "<a href='calibration.csv'>All10 decision-model/DB cutoffs</a> · <a href='native_audit.json'>Native image checks</a></p></html>";
    Write(QDir(folder).filePath("validation_report.html"), report.toUtf8());
    QTextStream(stdout) << "Validated24 native model comparisons; calibrated10 minimum-score DB/model operating points." << Qt::endl;
}


} // namespace algorithm
