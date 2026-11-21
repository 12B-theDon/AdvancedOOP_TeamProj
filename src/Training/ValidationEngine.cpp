#include "ValidationEngine.h"
#include <chrono>
#include <QDir>
#include <QFileInfo>
#include <QImage>

namespace training {
static double ElapsedMs(const std::chrono::steady_clock::time_point& start) {
 return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
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


}
