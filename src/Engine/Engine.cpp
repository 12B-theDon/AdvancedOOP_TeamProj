#include "Engine.h"
#include <random>
#include <chrono>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace engine {

static double ElapsedMs(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

static bool Contains(const std::vector<int>& values, int target) {
    for (int value : values) {
        if (value == target)
            return true;
    }
    return false;
}

static bool SameImage(const std::vector<ImageInfo>& images, int a, int b) {
    return a == b || (images[a].source_id >= 0 && images[a].source_id == images[b].source_id);
}

template <typename T>
static void Shuffle(std::vector<T>& values, std::mt19937& random) {
    for (int last = int(values.size()) - 1; last > 0; --last) {
        std::uniform_int_distribution<int> pick(0, last);
        const int index = pick(random);
        T temp = values[last];
        values[last] = values[index];
        values[index] = temp;
    }
}

bool Engine::PlanComparisons(const std::vector<ImageInfo>& images,
                             const std::vector<int>& enrollments, bool genuine,
                             const ComparisonOptions& options,
                             std::vector<ComparisonPair>& pairs, std::string& error) {
    pairs.clear();
    error.clear();
    std::vector<ComparisonPair> planned;
    for (int index : enrollments) {
        if (index < 0 || index >= int(images.size())) {
            error = "Enrollment image is not in this DB.";
            return false;
        }
    }
    if (options.rule == ComparisonRule::FromCsv) {
        for (const ComparisonPair& pair : options.csv_pairs) {
            if (pair.enrollment < 0 || pair.enrollment >= int(images.size())
                || pair.attempt < 0 || pair.attempt >= int(images.size())
                || !Contains(enrollments, pair.enrollment)) {
                error = "CSV enrollment must be selected, and both images must be in this DB.";
                return false;
            }
            if (SameImage(images, pair.enrollment, pair.attempt)) {
                error = "An image cannot be compared with itself or an identical copy.";
                return false;
            }
            const int a = images[pair.enrollment].finger, b = images[pair.attempt].finger;
            if (a > 0 && b > 0 && (a == b) != genuine) {
                error = "CSV pair conflicts with the known Genuine/Impostor label.";
                return false;
            }
            for (const ComparisonPair& previous : planned) {
                if (previous.enrollment == pair.enrollment && previous.attempt == pair.attempt) {
                    error = "CSV contains a duplicate pair.";
                    return false;
                }
            }
            planned.push_back(pair);
        }
        pairs = planned;
        return true;
    }
    if ((options.rule == ComparisonRule::RandomCount && options.count < 1)
        || (options.rule == ComparisonRule::CaptureNumbers && options.captures.empty())) {
        error = "Enter a positive count or at least one capture number.";
        return false;
    }
    std::mt19937 random(options.seed);
    for (int enrollment : enrollments) {
        if (images[enrollment].finger == 0)
            continue;
        std::vector<int> candidates;
        for (int attempt = 0; attempt < int(images.size()); ++attempt) {
            if (SameImage(images, enrollment, attempt) || images[attempt].finger == 0
                || (images[attempt].finger == images[enrollment].finger) != genuine)
                continue;
            if (options.rule == ComparisonRule::CaptureNumbers
                && !Contains(options.captures, images[attempt].capture))
                continue;
            candidates.push_back(attempt);
        }
        if (options.rule == ComparisonRule::FirstCapture) {
            std::vector<int> first;
            for (int candidate : candidates) {
                int position = -1;
                for (int i = 0; i < int(first.size()); ++i) {
                    if (images[first[i]].finger == images[candidate].finger) {
                        position = i;
                        break;
                    }
                }
                if (position < 0)
                    first.push_back(candidate);
                else {
                    const ImageInfo& saved = images[first[position]];
                    const ImageInfo& current = images[candidate];
                    if (current.capture < saved.capture
                        || (current.capture == saved.capture && current.name < saved.name))
                        first[position] = candidate;
                }
            }
            candidates = first;
        } else if (options.rule == ComparisonRule::RandomCount) {
            Shuffle(candidates, random);
            if (int(candidates.size()) > options.count)
                candidates.resize(options.count);
        }
        for (int attempt : candidates)
            planned.push_back({enrollment, attempt});
    }
    // Save the resulting order; CSV replay keeps it unchanged.
    Shuffle(planned, random);
    pairs = planned;
    return true;
}

std::vector<ModelState> Engine::SelectModels(const std::string& database_id, bool external,
                                            const std::string& weights_folder) {
    std::string folder = weights_folder;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
        folder += '/';
    std::vector<ModelState> selected;
    for (int index = 0; index < int(ModelIds.size()); ++index) {
        if (external || (index < 3 && (database_id == DatabaseIds[index] || database_id + "_B" == DatabaseIds[index])))
            selected.push_back({ModelIds[index], folder + ModelIds[index] + WeightExtension, false, ""});
    }
    return selected;
}

bool Engine::LoadSelectedModels(const std::vector<std::string>& ids, const std::string& weights_folder,
                                algorithm::Algorithm* backend) {
    backend_ = backend;
    weights_folder_ = weights_folder;
    ++revision_;
    models_.clear();
    std::string folder = weights_folder;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
    for (const std::string& id : ids) {
        bool duplicate = false;
        for (const ModelState& model : models_) if (model.model_id == id) duplicate = true;
        if (duplicate) continue;
        bool known = false;
        for (const std::string& candidate : ModelIds) if (id == candidate) known = true;
        if (known)
            models_.push_back({id, folder + id + WeightExtension, false, ""});
        else models_.push_back({id, "", false, "Unknown model ID."});
    }
    bool anyLoaded = false;
    std::string labelOrder;
    for (ModelState& model : models_) {
        if (!model.error.empty()) continue;
        if (!backend_)
            model.error = "Algorithm backend is not configured.";
        else {
            model.loaded = backend_->LoadModel(model.model_id, model.weight_file, model.error);
            if (model.loaded)
                model.loaded = backend_->GetModelInfo(model.model_id, model.info, model.error);
            if (model.loaded) {
                if (labelOrder.empty()) labelOrder = model.info.label_order_sha256;
                else if (labelOrder != model.info.label_order_sha256) {
                    model.loaded = false;
                    model.error = "Selected models have different ID label orders.";
                }
            }
            if (!model.loaded && model.error.empty())
                model.error = "Model loading failed.";
            if (model.loaded)
                model.error.clear();
        }
        if (model.loaded)
            anyLoaded = true;
    }
    return anyLoaded;
}

bool Engine::IsDecisionModel(const std::string& model_id, const std::string& database_id) {
    if (model_id == "combined") return true;
    for (int db = 0; db < int(DatabaseIds.size()); ++db)
        if (database_id == DatabaseIds[db] || database_id + "_B" == DatabaseIds[db])
            return model_id == ModelIds[db];
    return false;
}

void Engine::SelectDecisionScore(SimilarityResult& result, const std::string& database_id) {
    result.success = false;
    result.score = 0;
    result.model_id.clear();
    for (const ModelScore& score : result.model_scores) {
        if (!score.success || !IsDecisionModel(score.model_id, database_id)) continue;
        if (!result.success || score.score < result.score) {
            result.success = true;
            result.score = score.score;
            result.model_id = score.model_id;
        }
    }
}

ThresholdRecommendation Engine::RecommendedThreshold(const std::string& database_id) const {
    ThresholdRecommendation result;
    QFile file(QDir(QString::fromStdString(weights_folder_)).filePath(CalibrationFile));
    if (!file.open(QIODevice::ReadOnly)) { result.note = "No matching calibration; adjust thresholds manually."; return result; }
    const QJsonObject calibration = QJsonDocument::fromJson(file.readAll()).object();
    if (calibration.value("format").toString() != CalibrationFormat
        || calibration.value("aggregation").toString() != ScoreAggregation
        || calibration.value("score_type").toString() != ScoreType
        || calibration.value("template_format").toString() != "fingerprint-probability-template-v2"
        || std::abs(calibration.value("target_far").toDouble(-1) - TargetFar) > MetricTolerance
        || calibration.value("threshold_decimals").toInt() != ThresholdPrecision) {
        result.note = "Minimum-score calibration required. Run FingerprintTrain with --verify-only to regenerate it."; return result;
    }
    int mask = 0;
    for (const ModelState& model : models_) if (model.loaded && IsDecisionModel(model.model_id, database_id)) {
        if (calibration.value("weights").toObject().value(QString::fromStdString(model.model_id)).toString().toStdString() != model.info.weight_sha256
            || calibration.value("label_order_sha256").toString().toStdString() != model.info.label_order_sha256) {
            result.note = "Weights/labels changed; recalibrate before using a validation cutoff."; return result;
        }
        for (int m = 0; m < int(ModelIds.size()); ++m)
            if (model.model_id == ModelIds[m]) mask |= 1 << m;
    }
    if (!mask) { result.note = "Select the matching-DB or combined model to obtain a cutoff."; return result; }
    QString requested = "*";
    for (const std::string& db : DatabaseIds)
        if (db == database_id || db == database_id + "_B") requested = QString::fromStdString(db);
    const bool known = requested != "*";
    for (const QJsonValue& value : calibration.value("entries").toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value("model_mask").toInt() != mask || entry.value("database").toString() != requested) continue;
        const double cutoff = entry.value("threshold").toDouble(-1);
        if (!(cutoff >= 0.5 && cutoff <= 1)) continue;
        result.available = true; result.threshold = cutoff; result.target_met = entry.value("target_met").toBool();
        result.external_fallback = !known;
        result.note = known ? "Known-ID validation cutoff for the minimum matching-DB/combined score; not an independent-test guarantee."
                            : "Conservative known-DB fallback; external DB performance is not validated.";
        if (!result.target_met) result.note += " FAR target is not met at the supported threshold precision; reconsider selected models.";
        return result;
    }
    result.note = "No calibration for this model selection; adjust thresholds manually.";
    return result;
}

ModelTemplate Engine::MakeTemplate(const ModelState& model, const algorithm::AlgorithmImage& image) {
    ModelTemplate result;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    result.model_id = model.model_id;
    result.dimensions = model.info.output_size;
    if (!model.loaded || !backend_) {
        result.stage = "load";
        result.error = model.error;
    } else if (image.width <= 0 || image.height <= 0
               || image.pixels.size() != std::size_t(image.width) * image.height) {
        result.stage = "input";
        result.error = "Expected tightly packed 8-bit grayscale pixels.";
    } else {
        algorithm::PreparedImage prepared;
        result.stage = "preprocess";
        if (!backend_->Preprocess(model.model_id, image, prepared, result.error)
            || prepared.width <= 0 || prepared.height <= 0 || prepared.values.empty()) {
            if (result.error.empty()) result.error = "Preprocessing failed.";
        } else {
            result.stage = "template";
            if (!backend_->CreateTemplate(model.model_id, prepared, result.data, result.error) || result.data.empty()) {
                result.data.clear();
                if (result.error.empty()) result.error = "Template generation failed.";
            } else {
                result.success = true;
                result.stage.clear();
                result.error.clear();
            }
        }
    }
    result.time_ms = ElapsedMs(start);
    return result;
}

EnrollmentTemplates Engine::Enroll(const algorithm::AlgorithmImage& image) {
    EnrollmentTemplates result;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    result.executed = true;
    result.revision = revision_;
    for (const ModelState& model : models_) {
        result.models.push_back(MakeTemplate(model, image));
        if (result.models.back().success)
            result.success = true;
    }
    result.time_ms = ElapsedMs(start);
    return result;
}

SimilarityResult Engine::Compare(const EnrollmentTemplates& enrollment, const algorithm::AlgorithmImage& attempt) {
    SimilarityResult result;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (const ModelState& model : models_) {
        const std::chrono::steady_clock::time_point modelStart = std::chrono::steady_clock::now();
        ModelScore score;
        score.model_id = model.model_id;
        const ModelTemplate* enrolled = nullptr;
        for (const ModelTemplate& item : enrollment.models) {
            if (item.model_id == model.model_id)
                enrolled = &item;
        }
        if (enrollment.revision != revision_) {
            score.stage = "enrollment";
            score.error = "Models changed; create enrollment templates again.";
        } else if (!enrolled || !enrolled->success) {
            score.stage = enrolled ? enrolled->stage : "enrollment";
            score.error = enrolled ? enrolled->error : "Enrollment template is missing.";
        } else {
            // Enrollment and attempt both use this exact model-specific path.
            const ModelTemplate attempted = MakeTemplate(model, attempt);
            if (!attempted.success) {
                score.stage = attempted.stage;
                score.error = attempted.error;
            } else {
                score.stage = "compare";
                score.success = backend_->Compare(model.model_id, enrolled->data, attempted.data, score.score, score.error);
                if (score.success && !(score.score >= 0.0 && score.score <= 1.0)) {
                    score.success = false;
                    score.error = "Model returned a score outside [0, 1].";
                }
                if (score.success) {
                    score.stage.clear();
                    score.error.clear();
                } else if (score.error.empty())
                    score.error = "Comparison failed.";
            }
        }
        score.time_ms = ElapsedMs(modelStart);
        result.model_scores.push_back(score);
    }
    SelectDecisionScore(result, attempt.database_id);
    result.time_ms = ElapsedMs(start);
    return result;
}

algorithm::AlgorithmImage Engine::ReadImage(const std::string& path, const std::string& database_id) {
    const QImage image = QImage(QString::fromStdString(path)).convertToFormat(QImage::Format_Grayscale8);
    algorithm::AlgorithmImage result;
    // Explicit GUI/training context wins; known parent names also support CLI callers.
    const std::string source = database_id.empty() ? QFileInfo(QString::fromStdString(path)).dir().dirName().toStdString() : database_id;
    for (const std::string& known : DatabaseIds)
        if (source == known || source + "_B" == known) result.database_id = known;
    result.width = image.width();
    result.height = image.height();
    result.pixels.resize(std::size_t(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            result.pixels[std::size_t(y) * image.width() + x] = image.constScanLine(y)[x];
    return result;
}

std::vector<SimilarityResult> Engine::Execute(const std::vector<ComparisonPair>& pairs,
                                             const std::vector<ImageInfo>& images,
                                             const std::vector<EnrollmentTemplates>& templates) {
    std::vector<SimilarityResult> results;
    for (const ComparisonPair& pair : pairs) {
        const bool unsupported = TimeoutMs != 0 || Concurrency != 1;
        const bool invalid = pair.enrollment < 0 || pair.attempt < 0 || pair.enrollment >= int(templates.size())
            || pair.enrollment >= int(images.size()) || pair.attempt >= int(images.size())
            || SameImage(images, pair.enrollment, pair.attempt);
        if (unsupported || invalid) {
            SimilarityResult result;
            for (const ModelState& model : models_) {
                ModelScore failure;
                failure.model_id = model.model_id;
                failure.stage = unsupported ? "execution" : "input";
                failure.error = unsupported ? "This runner supports timeout=0 and concurrency=1." : "Invalid comparison pair or identical image.";
                result.model_scores.push_back(failure);
            }
            results.push_back(result);
        } else {
            const algorithm::AlgorithmImage attempt = ReadImage(images[pair.attempt].path, images[pair.attempt].database_id);
            SimilarityResult result = Compare(templates[pair.enrollment], attempt);
            // Keep explicit unknown-DB selection separate from inferred preprocessing geometry.
            if (attempt.database_id != images[pair.attempt].database_id)
                SelectDecisionScore(result, images[pair.attempt].database_id);
            results.push_back(result);
        }
    }
    return results;
}

const char* Engine::ClassifyComparison(bool genuine, const SimilarityResult& result, double threshold) {
    if (!result.success) return "ERR";
    const bool accepted = result.score >= threshold;
    if (genuine) return accepted ? "PASS" : "MISS";
    return accepted ? "FA" : "REJ";
}

static QString Csv(QString value) {
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

static bool WriteText(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}

bool Engine::SaveRawResults(const EvaluationRun& run, const std::string& folder) {
    const QDir destination(QString::fromStdString(folder));
    QString models = "model_id,weight_file,status,error,format,output_dim,weight_sha256,label_order_sha256,preprocessing\n";
    for (const ModelState& model : run.models)
        models += Csv(QString::fromStdString(model.model_id)) + ',' + Csv(QString::fromStdString(model.weight_file))
            + ',' + Csv(model.loaded ? "Ready" : "Failed") + ',' + Csv(QString::fromStdString(model.error))
            + ',' + Csv(QString::fromStdString(model.info.format)) + ',' + QString::number(model.info.output_size)
            + ',' + Csv(QString::fromStdString(model.info.weight_sha256)) + ',' + Csv(QString::fromStdString(model.info.label_order_sha256))
            + ',' + Csv(QString::fromStdString(model.info.preprocessing)) + '\n';
    QString enrollments = "image,model_id,status,template_bytes,template_file,stage,error,time_ms,output_dim\n";
    for (const EnrollmentRecord& record : run.enrollments) {
        for (const ModelState& model : run.models) {
            const ModelTemplate* value = nullptr;
            for (const ModelTemplate& item : record.result.models) if (item.model_id == model.model_id) value = &item;
            QString path;
            if (record.result.executed && value && value->success) {
                if (!QDir().mkpath(destination.filePath("templates"))) return false;
                path = "templates/" + QString::number(record.image_index) + "_" + QString::fromStdString(model.model_id) + TemplateExtension;
                QFile file(destination.filePath(path));
                if (!file.open(QIODevice::WriteOnly)
                    || file.write(reinterpret_cast<const char*>(value->data.data()), qint64(value->data.size())) != qint64(value->data.size())) return false;
                file.close();
                if (file.error() != QFileDevice::NoError) return false;
            }
            enrollments += Csv(QString::fromStdString(record.name)) + ',' + Csv(QString::fromStdString(model.model_id)) + ','
                + Csv(!record.result.executed ? "Not run" : value && value->success ? "OK" : "Failed") + ','
                + (value && value->success ? QString::number(value->data.size()) : QString()) + ',' + Csv(path) + ','
                + Csv(value ? QString::fromStdString(value->stage) : QString()) + ','
                + Csv(value ? QString::fromStdString(value->error) : QString()) + ','
                + (record.result.executed && value ? QString::number(value->time_ms, 'g', CsvPrecision) : QString())
                + ',' + (value ? QString::number(value->dimensions) : QString()) + '\n';
        }
    }
    QString comparisons = "type,enrollment,attempt,status,score,time_ms,model_id,model_scores,index,threshold,result\n";
    QString scores = "type,enrollment,attempt,model_id,status,score,stage,error,time_ms\n";
    int indices[2] = {};
    for (const ComparisonRecord& record : run.comparisons) {
        const int index = ++indices[record.genuine ? 0 : 1];
        const double threshold = record.genuine ? run.genuine_threshold : run.impostor_threshold;
        const QString decision = !record.executed ? "Not run"
            : ClassifyComparison(record.genuine, record.result, threshold);
        const QString type = record.genuine ? "Genuine" : "Impostor";
        const QString common = type + ',' + Csv(QString::fromStdString(record.enrollment)) + ',' + Csv(QString::fromStdString(record.attempt));
        QStringList summary;
        for (const ModelState& model : run.models) {
            const ModelScore* value = nullptr;
            for (const ModelScore& item : record.result.model_scores) if (item.model_id == model.model_id) value = &item;
            const QString status = !record.executed ? "Not run" : value && value->success ? "OK" : "Failed";
            const QString number = value && value->success ? QString::number(value->score, 'g', CsvPrecision) : QString();
            summary.append(QString::fromStdString(model.model_id) + "=" + (value && value->success ? number : status));
            scores += common + ',' + Csv(QString::fromStdString(model.model_id)) + ',' + Csv(status) + ',' + number + ','
                + Csv(value ? QString::fromStdString(value->stage) : QString()) + ','
                + Csv(value ? QString::fromStdString(value->error) : QString()) + ','
                + (record.executed && value ? QString::number(value->time_ms, 'g', CsvPrecision) : QString()) + '\n';
        }
        comparisons += common + ',' + Csv(!record.executed ? "Not run" : record.result.success ? "OK" : "Failed") + ','
            + (record.executed && record.result.success ? QString::number(record.result.score, 'g', CsvPrecision) : QString()) + ','
            + (record.executed ? QString::number(record.result.time_ms, 'g', CsvPrecision) : QString()) + ','
            + Csv(record.executed ? QString::fromStdString(record.result.model_id) : QString()) + ','
            + Csv(record.executed ? summary.join("; ") : QString()) + ',' + QString::number(index) + ','
            + QString::number(threshold, 'g', CsvPrecision) + ',' + Csv(decision) + '\n';
    }
    return WriteText(destination.filePath("models.csv"), models)
        && WriteText(destination.filePath("enrollments.csv"), enrollments)
        && WriteText(destination.filePath("comparisons.csv"), comparisons)
        && WriteText(destination.filePath("model_scores.csv"), scores)
        && WriteText(destination.filePath("settings.txt"),
                     "score_type=" + QString::fromUtf8(ScoreType)
                     + "\nscore_range=[0.5,1]\nscore_formula=(cosine(probability30,probability30)+1)/2\n"
                       "template_format=fingerprint-probability-template-v2\noutput_dim=30\npayload_bytes=120\n"
                       "template_file_extension=.fpt\nserialized_template_includes_weight_and_label_metadata=true\n"
                     + "genuine_threshold=" + QString::number(run.genuine_threshold, 'g', CsvPrecision) + "\n"
                     + "impostor_threshold=" + QString::number(run.impostor_threshold, 'g', CsvPrecision) + "\n"
                     + "aggregation=" + QString::fromUtf8(ScoreAggregation) + "\n"
                     + "decision_rule=Genuine: score >= threshold -> PASS, below -> MISS; "
                       "Impostor: score >= threshold -> FA, below -> REJ; inference failure -> ERR\n"
                     + QString::fromStdString(run.settings));
}

} // namespace engine
