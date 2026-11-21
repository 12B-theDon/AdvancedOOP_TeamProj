#pragma once
#include "../Algorithm/Algorithm.h"
#include "evaluationConfig.h"
namespace training {
// Model loading outcome.
struct ModelState {
    std::string model_id;
    std::string weight_file;
    bool loaded = false;
    std::string error;
    algorithm::AlgorithmModelInfo info;
};

// One image template for one model.
struct ModelTemplate {
    std::string model_id;
    bool success = false;
    std::vector<unsigned char> data; // [bytes] Serialized feature vector.
    double time_ms = 0; // [ms] Preprocessing + encoding.
    std::string stage;
    std::string error;
    int dimensions = 0;
};

// All model templates from one enrollment image.
struct EnrollmentTemplates {
    int revision = 0;
    bool executed = false;
    bool success = false;
    double time_ms = 0; // [ms] Total enrollment time.
    std::vector<ModelTemplate> models;
};

// One model's score or failure.
struct ModelScore {
    std::string model_id;
    bool success = false;
    double score = 0; // [0,1] Normalized similarity; higher is closer.
    double time_ms = 0; // [ms] Attempt encoding + comparison.
    std::string stage;
    std::string error;
};

// Lowest successful matching-DB/combined score, with every model outcome retained.
struct SimilarityResult {
    bool success = false;
    double score = 0; // [0,1] Normalized similarity.
    double time_ms = 0; // [ms] Total comparison time.
    std::string model_id;
    std::vector<ModelScore> model_scores;
};


// Native training audit only; never linked to the evaluator.
class Engine {
public:
 bool LoadSelectedModels(const std::vector<std::string>& ids, const std::string& folder, algorithm::Algorithm* backend);
 const std::vector<ModelState>& Models() const { return models_; }
 static bool IsDecisionModel(const std::string& id, const std::string& db);
 static void SelectDecisionScore(SimilarityResult& result, const std::string& db);
 static algorithm::AlgorithmImage ReadImage(const std::string& path, const std::string& db = "");
 EnrollmentTemplates Enroll(const algorithm::AlgorithmImage& image);
 SimilarityResult Compare(const EnrollmentTemplates& enrollment, const algorithm::AlgorithmImage& attempt);
private:
 algorithm::Algorithm* backend_ = nullptr;
 std::vector<ModelState> models_;
 int revision_ = 0;
 std::string weights_folder_;
 ModelTemplate MakeTemplate(const ModelState& model, const algorithm::AlgorithmImage& image);
};
}
