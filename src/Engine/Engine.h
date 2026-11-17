#pragma once

#include <string>
#include <vector>
#include "../Algorithm/Algorithm.h"
#include "engineConfig.h"

// One DB image; source_id groups identical pixel copies.
struct ImageInfo {
    std::string name;
    int finger = 0; // Finger ID; 0 means unknown.
    int capture = 0; // Capture number; 0 means unknown.
    int source_id = -1; // Same value means identical pixels; -1 means unknown.
    std::string path;
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
    int count = EngineConfig::RandomCount; // [pairs/enrollment]
    unsigned int seed = EngineConfig::Seed;
    std::vector<ComparisonPair> csv_pairs;
};

// Model loading outcome.
struct ModelState {
    std::string model_id;
    std::string weight_file;
    bool loaded = false;
    std::string error;
};

// One image template for one model.
struct ModelTemplate {
    std::string model_id;
    bool success = false;
    std::vector<unsigned char> data; // [bytes] Serialized feature vector.
    double time_ms = 0; // [ms] Preprocessing + encoding.
    std::string stage;
    std::string error;
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

// Highest successful score, with every model outcome retained.
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
    bool executed = false;
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
    double genuine_threshold = EngineConfig::DecisionThreshold; // [0,1] Accept score >= cutoff.
    double impostor_threshold = EngineConfig::DecisionThreshold; // [0,1] Accept score >= cutoff.
};

// Comparison planning, Algorithm calls, timing and raw export.
class Engine {
public:
    static bool PlanComparisons(const std::vector<ImageInfo>& images,
                                const std::vector<int>& enrollments, bool genuine,
                                const ComparisonOptions& options,
                                std::vector<ComparisonPair>& pairs, std::string& error);
    static std::vector<ModelState> SelectModels(const std::string& database_id, bool external,
                                                const std::string& weights_folder);
    // The caller owns the backend and keeps it alive while Engine uses it.
    bool LoadModels(const std::string& database_id, bool external, const std::string& weights_folder,
                    Algorithm* backend);
    bool LoadSelectedModels(const std::vector<std::string>& model_ids, const std::string& weights_folder,
                            Algorithm* backend);
    const std::vector<ModelState>& Models() const { return models_; }
    EnrollmentTemplates Enroll(const AlgorithmImage& image);
    SimilarityResult Compare(const EnrollmentTemplates& enrollment, const AlgorithmImage& attempt);
    static AlgorithmImage ReadImage(const std::string& path);
    std::vector<SimilarityResult> Execute(const std::vector<ComparisonPair>& pairs,
                                         const std::vector<ImageInfo>& images,
                                         const std::vector<EnrollmentTemplates>& templates);
    // Ground-truth group + best score -> PASS, MISS, REJ, FA or ERR.
    static const char* ClassifyComparison(bool genuine, const SimilarityResult& result, double threshold);
    // Write into the caller's newly created run folder.
    static bool SaveRawResults(const EvaluationRun& run, const std::string& folder);

private:
    Algorithm* backend_ = nullptr;
    std::vector<ModelState> models_;
    int revision_ = 0;
    ModelTemplate MakeTemplate(const ModelState& model, const AlgorithmImage& image);
};
