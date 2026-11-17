#include "Engine.h"
#include <random>
#include <chrono>
#include <QDir>
#include <QFile>
#include <QImage>

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
    auto validIndex = [&](int index) { return index >= 0 && index < int(images.size()); };
    for (int index : enrollments) {
        if (!validIndex(index)) {
            error = "Enrollment image is not in this DB.";
            return false;
        }
    }
    if (options.rule == ComparisonRule::FromCsv) {
        for (const auto& pair : options.csv_pairs) {
            if (!validIndex(pair.enrollment) || !validIndex(pair.attempt)
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
            for (const auto& previous : planned) {
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
                    const auto& saved = images[first[position]];
                    const auto& current = images[candidate];
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
    const auto& ids = EngineConfig::ModelIds;
    const auto& databases = EngineConfig::DatabaseIds;
    std::string folder = weights_folder;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
        folder += '/';
    std::vector<ModelState> selected;
    for (int index = 0; index < int(ids.size()); ++index) {
        if (external || (index < 3 && (database_id == databases[index] || database_id + "_B" == databases[index])))
            selected.push_back({ids[index], folder + ids[index] + EngineConfig::WeightExtension, false, ""});
    }
    return selected;
}

bool Engine::LoadModels(const std::string& database_id, bool external, const std::string& weights_folder,
                        Algorithm* backend) {
    std::vector<std::string> ids;
    for (const auto& model : SelectModels(database_id, external, weights_folder)) ids.push_back(model.model_id);
    return LoadSelectedModels(ids, weights_folder, backend);
}

bool Engine::LoadSelectedModels(const std::vector<std::string>& ids, const std::string& weights_folder,
                                Algorithm* backend) {
    backend_ = backend;
    ++revision_;
    models_.clear();
    std::string folder = weights_folder;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
    for (const auto& id : ids) {
        bool duplicate = false;
        for (const auto& model : models_) if (model.model_id == id) duplicate = true;
        if (duplicate) continue;
        bool known = false;
        for (const auto& candidate : EngineConfig::ModelIds) if (id == candidate) known = true;
        if (known)
            models_.push_back({id, folder + id + EngineConfig::WeightExtension, false, ""});
        else models_.push_back({id, "", false, "Unknown model ID."});
    }
    bool anyLoaded = false;
    for (auto& model : models_) {
        if (!model.error.empty()) continue;
        if (!backend_)
            model.error = "Algorithm backend is not configured.";
        else {
            model.loaded = backend_->LoadModel(model.model_id, model.weight_file, model.error);
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

ModelTemplate Engine::MakeTemplate(const ModelState& model, const AlgorithmImage& image) {
    ModelTemplate result;
    const auto start = std::chrono::steady_clock::now();
    auto finish = [&] { result.time_ms = ElapsedMs(start); return result; };
    result.model_id = model.model_id;
    if (!model.loaded || !backend_) {
        result.stage = "load";
        result.error = model.error;
        return finish();
    }
    if (image.width <= 0 || image.height <= 0
        || image.pixels.size() != std::size_t(image.width) * std::size_t(image.height)) {
        result.stage = "input";
        result.error = "Expected tightly packed 8-bit grayscale pixels.";
        return finish();
    }
    PreparedImage prepared;
    result.stage = "preprocess";
    if (!backend_->Preprocess(model.model_id, image, prepared, result.error)
        || prepared.width <= 0 || prepared.height <= 0 || prepared.values.empty()) {
        if (result.error.empty()) result.error = "Preprocessing failed.";
        return finish();
    }
    result.stage = "template";
    if (!backend_->CreateTemplate(model.model_id, prepared, result.data, result.error) || result.data.empty()) {
        result.data.clear();
        if (result.error.empty()) result.error = "Template generation failed.";
        return finish();
    }
    result.success = true;
    result.stage.clear();
    result.error.clear();
    return finish();
}

EnrollmentTemplates Engine::Enroll(const AlgorithmImage& image) {
    EnrollmentTemplates result;
    const auto start = std::chrono::steady_clock::now();
    result.executed = true;
    result.revision = revision_;
    for (const auto& model : models_) {
        result.models.push_back(MakeTemplate(model, image));
        if (result.models.back().success)
            result.success = true;
    }
    result.time_ms = ElapsedMs(start);
    return result;
}

SimilarityResult Engine::Compare(const EnrollmentTemplates& enrollment, const AlgorithmImage& attempt) {
    SimilarityResult result;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& model : models_) {
        const auto modelStart = std::chrono::steady_clock::now();
        ModelScore score;
        score.model_id = model.model_id;
        const ModelTemplate* enrolled = nullptr;
        for (const auto& item : enrollment.models) {
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
                    if (!result.success || score.score > result.score) {
                        result.success = true;
                        result.score = score.score;
                        result.model_id = score.model_id;
                    }
                } else if (score.error.empty())
                    score.error = "Comparison failed.";
            }
        }
        score.time_ms = ElapsedMs(modelStart);
        result.model_scores.push_back(score);
    }
    result.time_ms = ElapsedMs(start);
    return result;
}

AlgorithmImage Engine::ReadImage(const std::string& path) {
    const QImage image = QImage(QString::fromStdString(path)).convertToFormat(QImage::Format_Grayscale8);
    AlgorithmImage result;
    result.width = image.width();
    result.height = image.height();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) result.pixels.push_back(image.constScanLine(y)[x]);
    return result;
}

std::vector<SimilarityResult> Engine::Execute(const std::vector<ComparisonPair>& pairs,
                                             const std::vector<ImageInfo>& images,
                                             const std::vector<EnrollmentTemplates>& templates) {
    std::vector<SimilarityResult> results;
    for (const auto& pair : pairs) {
        if (EngineConfig::TimeoutMs != 0 || EngineConfig::Concurrency != 1) {
            SimilarityResult result;
            for (const auto& model : models_) {
                ModelScore failure;
                failure.model_id = model.model_id;
                failure.stage = "execution";
                failure.error = "This runner supports timeout=0 and concurrency=1.";
                result.model_scores.push_back(failure);
            }
            results.push_back(result);
        } else if (pair.enrollment < 0 || pair.attempt < 0 || pair.enrollment >= int(templates.size())
                   || pair.enrollment >= int(images.size()) || pair.attempt >= int(images.size())
                   || SameImage(images, pair.enrollment, pair.attempt)) {
            SimilarityResult result;
            for (const auto& model : models_) {
                ModelScore failure;
                failure.model_id = model.model_id;
                failure.stage = "input";
                failure.error = "Invalid comparison pair or identical image.";
                result.model_scores.push_back(failure);
            }
            results.push_back(result);
        } else
            results.push_back(Compare(templates[pair.enrollment], ReadImage(images[pair.attempt].path)));
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
    const auto bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}

bool Engine::SaveRawResults(const EvaluationRun& run, const std::string& folder) {
    const QDir destination(QString::fromStdString(folder));
    QString models = "model_id,weight_file,status,error\n";
    for (const auto& model : run.models)
        models += Csv(QString::fromStdString(model.model_id)) + ',' + Csv(QString::fromStdString(model.weight_file))
            + ',' + Csv(model.loaded ? "Ready" : "Failed") + ',' + Csv(QString::fromStdString(model.error)) + '\n';
    QString enrollments = "image,model_id,status,template_bytes,template_file,stage,error,time_ms\n";
    for (const auto& record : run.enrollments) {
        for (const auto& model : run.models) {
            const ModelTemplate* value = nullptr;
            for (const auto& item : record.result.models) if (item.model_id == model.model_id) value = &item;
            QString path;
            if (record.executed && value && value->success) {
                if (!QDir().mkpath(destination.filePath("templates"))) return false;
                path = "templates/" + QString::number(record.image_index) + "_" + QString::fromStdString(model.model_id) + EngineConfig::WeightExtension;
                QFile file(destination.filePath(path));
                if (!file.open(QIODevice::WriteOnly)
                    || file.write(reinterpret_cast<const char*>(value->data.data()), qint64(value->data.size())) != qint64(value->data.size())) return false;
                file.close();
                if (file.error() != QFileDevice::NoError) return false;
            }
            enrollments += Csv(QString::fromStdString(record.name)) + ',' + Csv(QString::fromStdString(model.model_id)) + ','
                + Csv(!record.executed ? "Not run" : value && value->success ? "OK" : "Failed") + ','
                + (value && value->success ? QString::number(value->data.size()) : QString()) + ',' + Csv(path) + ','
                + Csv(value ? QString::fromStdString(value->stage) : QString()) + ','
                + Csv(value ? QString::fromStdString(value->error) : QString()) + ','
                + (record.executed && value ? QString::number(value->time_ms, 'g', EngineConfig::CsvPrecision) : QString()) + '\n';
        }
    }
    QString comparisons = "type,enrollment,attempt,status,score,time_ms,model_id,model_scores,index,threshold,result\n";
    QString scores = "type,enrollment,attempt,model_id,status,score,stage,error,time_ms\n";
    int indices[2] = {};
    for (const auto& record : run.comparisons) {
        const int index = ++indices[record.genuine ? 0 : 1];
        const double threshold = record.genuine ? run.genuine_threshold : run.impostor_threshold;
        const QString decision = !record.executed ? "Not run"
            : ClassifyComparison(record.genuine, record.result, threshold);
        const QString type = record.genuine ? "Genuine" : "Impostor";
        const QString common = type + ',' + Csv(QString::fromStdString(record.enrollment)) + ',' + Csv(QString::fromStdString(record.attempt));
        QStringList summary;
        for (const auto& model : run.models) {
            const ModelScore* value = nullptr;
            for (const auto& item : record.result.model_scores) if (item.model_id == model.model_id) value = &item;
            const QString status = !record.executed ? "Not run" : value && value->success ? "OK" : "Failed";
            const QString number = value && value->success ? QString::number(value->score, 'g', EngineConfig::CsvPrecision) : QString();
            summary.append(QString::fromStdString(model.model_id) + "=" + (value && value->success ? number : status));
            scores += common + ',' + Csv(QString::fromStdString(model.model_id)) + ',' + Csv(status) + ',' + number + ','
                + Csv(value ? QString::fromStdString(value->stage) : QString()) + ','
                + Csv(value ? QString::fromStdString(value->error) : QString()) + ','
                + (record.executed && value ? QString::number(value->time_ms, 'g', EngineConfig::CsvPrecision) : QString()) + '\n';
        }
        comparisons += common + ',' + Csv(!record.executed ? "Not run" : record.result.success ? "OK" : "Failed") + ','
            + (record.executed && record.result.success ? QString::number(record.result.score, 'g', EngineConfig::CsvPrecision) : QString()) + ','
            + (record.executed ? QString::number(record.result.time_ms, 'g', EngineConfig::CsvPrecision) : QString()) + ','
            + Csv(record.executed ? QString::fromStdString(record.result.model_id) : QString()) + ','
            + Csv(record.executed ? summary.join("; ") : QString()) + ',' + QString::number(index) + ','
            + QString::number(threshold, 'g', EngineConfig::CsvPrecision) + ',' + Csv(decision) + '\n';
    }
    return WriteText(destination.filePath("models.csv"), models)
        && WriteText(destination.filePath("enrollments.csv"), enrollments)
        && WriteText(destination.filePath("comparisons.csv"), comparisons)
        && WriteText(destination.filePath("model_scores.csv"), scores)
        && WriteText(destination.filePath("settings.txt"),
                     "score_type=" + QString::fromUtf8(EngineConfig::ScoreType)
                     + "\nscore_range=[0,1]\nscore_formula=(cosine+1)/2\n"
                     + "genuine_threshold=" + QString::number(run.genuine_threshold, 'g', EngineConfig::CsvPrecision) + "\n"
                     + "impostor_threshold=" + QString::number(run.impostor_threshold, 'g', EngineConfig::CsvPrecision) + "\n"
                     + "decision_rule=Genuine: score >= threshold -> PASS, below -> MISS; "
                       "Impostor: score >= threshold -> FA, below -> REJ; inference failure -> ERR\n"
                     + QString::fromStdString(run.settings));
}
