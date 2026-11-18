#pragma once

#include <string>
#include <vector>
#include "../Algorithm/Algorithm.h"
#include "engineConfig.h"

namespace engine {

// One DB image; source_id groups identical pixel copies.
struct ImageInfo {
    std::string name;
    int finger = 0; // Finger ID; 0 means unknown.
    int capture = 0; // Capture number; 0 means unknown.
    int source_id = -1; // Same value means identical pixels; -1 means unknown.
    std::string path;
    std::string database_id; // Source DB for the same geometry used during fine-tuning.
};

// Zero-based indices in the DB image list.
struct ComparisonPair {
    int enrollment;
    int attempt;
};

enum class ComparisonRule { All, FirstCapture, CaptureNumbers, RandomCount, FromCsv };

// Automatic selection or exact CSV replay.
struct ComparisonOptions {
    ComparisonRule rule = ComparisonRule::All;
    std::vector<int> captures;
    int count = RandomCount; // [pairs/enrollment]
    unsigned int seed = Seed;
    std::vector<ComparisonPair> csv_pairs;
};

// Model loading outcome.
struct ModelState {
    std::string model_id;
    std::string weight_file;
    bool loaded = false;
    std::string error;
    algorithm::AlgorithmModelInfo info;
};

struct ThresholdRecommendation {
    bool available = false;
    double threshold = GenuineThreshold;
    bool target_met = false;
    bool external_fallback = false;
    std::string note;
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

// Image name and its actual enrollment outcome.
struct EnrollmentRecord {
    int image_index = 0;
    std::string name;
    EnrollmentTemplates result;
};

// Planned pair and its optional execution outcome.
struct ComparisonRecord {
    std::string enrollment;
    std::string attempt;
    bool genuine = false;
    bool executed = false;
    SimilarityResult result;
};

// UI-independent snapshot used by raw export and reporting.
struct EvaluationRun {
    std::string database;
    std::string weights_folder;
    std::string settings;
    std::vector<ModelState> models;
    std::vector<EnrollmentRecord> enrollments;
    std::vector<ComparisonRecord> comparisons;
    double genuine_threshold = GenuineThreshold; // [0,1] Accept score >= cutoff.
    double impostor_threshold = ImpostorThreshold; // [0,1] Accept score >= cutoff.
};

// Comparison planning, Algorithm calls, timing and raw export.
class Engine {
public:
    Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    static bool PlanComparisons(const std::vector<ImageInfo>& images,
                                const std::vector<int>& enrollments, bool genuine,
                                const ComparisonOptions& options,
                                std::vector<ComparisonPair>& pairs, std::string& error);
    static std::vector<ModelState> SelectModels(const std::string& database_id, bool external,
                                                const std::string& weights_folder);
    // The caller owns the backend and keeps it alive while Engine uses it.
    bool LoadSelectedModels(const std::vector<std::string>& model_ids, const std::string& weights_folder,
                            algorithm::Algorithm* backend);
    const std::vector<ModelState>& Models() const { return models_; }
    static bool IsDecisionModel(const std::string& model_id, const std::string& database_id);
    static void SelectDecisionScore(SimilarityResult& result, const std::string& database_id);
    ThresholdRecommendation RecommendedThreshold(const std::string& database_id) const;
    EnrollmentTemplates Enroll(const algorithm::AlgorithmImage& image);
    SimilarityResult Compare(const EnrollmentTemplates& enrollment, const algorithm::AlgorithmImage& attempt);
    static algorithm::AlgorithmImage ReadImage(const std::string& path, const std::string& database_id = "");
    std::vector<SimilarityResult> Execute(const std::vector<ComparisonPair>& pairs,
                                         const std::vector<ImageInfo>& images,
                                         const std::vector<EnrollmentTemplates>& templates);
    // Ground-truth group + best score -> PASS, MISS, REJ, FA or ERR.
    static const char* ClassifyComparison(bool genuine, const SimilarityResult& result, double threshold);
    // Write into the caller's newly created run folder.
    static bool SaveRawResults(const EvaluationRun& run, const std::string& folder);

private:
    algorithm::Algorithm* backend_ = nullptr; // Borrowed; never deleted by Engine.
    std::vector<ModelState> models_;
    int revision_ = 0;
    std::string weights_folder_;
    ModelTemplate MakeTemplate(const ModelState& model, const algorithm::AlgorithmImage& image);
};

} // namespace engine
