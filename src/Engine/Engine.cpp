#include "Engine.h"
#include <random>
#include <thread>
#include <algorithm>
#include <exception>
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

void Engine::Configure(Algorithm* backend, const std::string& path) {
    backend_ = backend;
    ++revision_;
    models_.clear();
    if (backend_) models_.push_back({DllModelId, path, true, ""});
    ResetCancel();
}
void Engine::SetConcurrency(int count) { concurrency_ = std::clamp(count, 1, MaxConcurrency); }
void Engine::Cancel() {
    cancelled_.store(true);
    if (backend_) backend_->Cancel();
}
void Engine::ResetCancel() {
    if (backend_) backend_->ResetCancel();
    cancelled_.store(false);
}
bool Engine::CancellationFinished() const { return !backend_ || backend_->CancellationFinished(); }
void Engine::ParallelFor(int count, const std::function<void(int)>& action) {
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    const int workers = std::min(count, concurrency_);
    for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([this, count, &next, &action] {
            while (!Cancelled()) {
                const int index = next.fetch_add(1);
                if (index >= count) break;
                action(index);
                progress_done_.fetch_add(1);
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
}
ModelTemplate Engine::MakeTemplate(const fingerprint::Image& image, bool enrollment) {
    ModelTemplate result;
    result.model_id = DllModelId;
    result.stage = enrollment ? "enroll" : "attempt";
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (!backend_) result.error = "[FAIL] No selected algorithm.";
    else if (Cancelled()) result.error = "[CANCELLED] Stopped.";
    else if (image.width <= 0 || image.height <= 0 || image.pixels.size() != std::size_t(image.width) * image.height)
        result.error = "[FAIL] Expected tightly packed gray8 image.";
    else {
        try { result.success = backend_->Extract(image, enrollment, result.data, result.error); }
        catch (const std::exception& e) { result.error = std::string("[FAIL] Backend exception: ") + e.what(); }
        if (result.success && (result.data.empty() || result.data.size() >= kisa::TemplateCapacity)) {
            result.success = false; result.error = "[MISMATCH] Template length outside ABI limits.";
        }
        if (!result.success) result.data.clear();
        else { result.stage.clear(); result.error.clear(); }
    }
    result.time_ms = ElapsedMs(start);
    return result;
}
EnrollmentTemplates Engine::Enroll(const fingerprint::Image& image) {
    EnrollmentTemplates result;
    result.executed = true; result.revision = revision_;
    result.models.push_back(MakeTemplate(image, true));
    result.success = result.models[0].success; result.time_ms = result.models[0].time_ms;
    return result;
}
std::vector<EnrollmentTemplates> Engine::EnrollImages(const std::vector<ImageInfo>& images, const std::vector<int>& selected) {
    std::vector<EnrollmentTemplates> results(images.size());
    progress_done_.store(0); progress_total_.store(static_cast<int>(selected.size()));
    // Deduplicate selections before parallel writes into the indexed output.
    std::vector<int> unique;
    for (int index : selected) {
        if (index >= 0 && index < static_cast<int>(images.size())
            && std::find(unique.begin(), unique.end(), index) == unique.end()) unique.push_back(index);
    }
    progress_total_.store(static_cast<int>(unique.size()));
    ParallelFor(static_cast<int>(unique.size()), [this, &images, &unique, &results](int position) {
        const int index = unique[position];
        results[index] = Enroll(ReadImage(images[index].path, images[index].database_id));
    });
    if (Cancelled()) results.clear();
    return results;
}
SimilarityResult Engine::ComparePrepared(const EnrollmentTemplates& enrollment, const ModelTemplate& attempt) {
    SimilarityResult result;
    ModelScore score; score.model_id = DllModelId; score.attempt_time_ms = attempt.time_ms;
    result.attempt_time_ms = attempt.time_ms;
    if (enrollment.revision != revision_ || !enrollment.success || enrollment.models.empty()) {
        result.enrollment_failed = true; score.stage = "enrollment";
        score.error = enrollment.revision != revision_ ? "[MISMATCH] Algorithm changed; regenerate templates."
            : !enrollment.models.empty() ? enrollment.models[0].error : "[FAIL] Enrollment unavailable.";
    } else if (!attempt.success) { score.stage = "attempt"; score.error = attempt.error; }
    else if (Cancelled()) { score.stage = "compare"; score.error = "[CANCELLED] Stopped."; }
    else {
        score.stage = "compare"; result.compare_executed = true;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        score.time_ms = -1;
        try { score.success = backend_->Compare(enrollment.models[0].data, attempt.data, score.score, score.error, &score.time_ms); }
        catch (const std::exception& e) { score.error = std::string("[FAIL] Backend exception: ") + e.what(); }
        if (score.time_ms < 0) score.time_ms = ElapsedMs(start);
        if (score.success && (!std::isfinite(score.score) || score.score < 0 || score.score > 1)) {
            score.success = false; score.error = "[MISMATCH] Score outside [0,1].";
        }
        if (score.success) { score.stage.clear(); score.error.clear(); }
    }
    result.success = score.success; result.score = score.score; result.time_ms = score.time_ms;
    result.model_id = DllModelId; result.model_scores.push_back(score);
    return result;
}
std::vector<SimilarityResult> Engine::Execute(const std::vector<ComparisonPair>& pairs,
    const std::vector<ImageInfo>& images, const std::vector<EnrollmentTemplates>& templates) {
    std::vector<int> attempts;
    for (const ComparisonPair& pair : pairs) {
        if (pair.enrollment < 0 || pair.enrollment >= static_cast<int>(templates.size()) || !templates[pair.enrollment].success) continue;
        if (pair.attempt >= 0 && pair.attempt < static_cast<int>(images.size())
            && std::find(attempts.begin(), attempts.end(), pair.attempt) == attempts.end()) attempts.push_back(pair.attempt);
    }
    std::vector<ModelTemplate> cache(images.size());
    std::vector<SimilarityResult> results(pairs.size());
    progress_done_.store(0); progress_total_.store(static_cast<int>(attempts.size() + pairs.size()));
    ParallelFor(static_cast<int>(attempts.size()), [this, &attempts, &images, &cache](int position) {
        const int index = attempts[position];
        cache[index] = MakeTemplate(ReadImage(images[index].path, images[index].database_id), false);
    });
    ParallelFor(static_cast<int>(pairs.size()), [this, &pairs, &images, &templates, &cache, &results](int index) {
        const ComparisonPair& pair = pairs[index];
        if (pair.enrollment < 0 || pair.attempt < 0 || pair.enrollment >= static_cast<int>(templates.size())
            || pair.enrollment >= static_cast<int>(images.size()) || pair.attempt >= static_cast<int>(images.size())
            || SameImage(images, pair.enrollment, pair.attempt)) {
            ModelScore score; score.model_id = DllModelId; score.stage = "input";
            score.error = "[FAIL] Invalid pair or identical image."; results[index].model_scores.push_back(score);
        } else results[index] = ComparePrepared(templates[pair.enrollment], cache[pair.attempt]);
    });
    if (Cancelled()) results.clear();
    return results;
}
fingerprint::Image Engine::ReadImage(const std::string& path, const std::string& database_id) {
    const QImage image = QImage(QString::fromStdString(path)).convertToFormat(QImage::Format_Grayscale8);
    fingerprint::Image result;
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
    QString models = "algorithm_id,dll_file,status,error\n";
    for (const ModelState& model : run.models)
        models += Csv(QString::fromStdString(model.model_id)) + ',' + Csv(QString::fromStdString(model.dll_file))
            + ',' + Csv(model.loaded ? "Ready" : "Failed") + ',' + Csv(QString::fromStdString(model.error)) + '\n';
    QString enrollments = "image,model_id,status,template_bytes,template_file,stage,error,time_ms\n";
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
                + (record.result.executed && value ? QString::number(value->time_ms, 'g', CsvPrecision) : QString()) + '\n';
        }
    }
    QString comparisons = "type,enrollment,attempt,status,score,time_ms,model_id,model_scores,index,threshold,result\n";
    QString scores = "type,enrollment,attempt,model_id,status,score,stage,error,time_ms,attempt_time_ms\n";
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
                + (record.executed && value ? QString::number(value->time_ms, 'g', CsvPrecision) : QString()) + ','
                + (record.executed && value ? QString::number(value->attempt_time_ms, 'g', CsvPrecision) : QString()) + '\n';
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
                     "score_type=selected DLL similarity\nscore_range=[0,1]\ntemplate_format=opaque bytes\n"
                     "template_length=actual bytes returned by selected DLL\n"
                     "genuine_threshold=" + QString::number(run.genuine_threshold, 'g', CsvPrecision) + "\n"
                     + "impostor_threshold=" + QString::number(run.impostor_threshold, 'g', CsvPrecision) + "\n"
                     + "decision_rule=Genuine: score >= threshold -> PASS, below -> MISS; "
                       "Impostor: score >= threshold -> FA, below -> REJ; inference failure -> ERR\n"
                     + QString::fromStdString(run.settings));
}

} // namespace engine
