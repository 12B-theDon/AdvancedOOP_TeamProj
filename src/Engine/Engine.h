#pragma once

#include <string>
#include <vector>
#include "Algorithm.h"
#include <atomic>
#include <functional>
#include "engineConfig.h"
#include "ExportControl.h"

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

struct ModelState {
    std::string model_id;
    std::string dll_file; // Selected DLL path.
    bool loaded = false;
    std::string error;
};

// One image template for one model.
struct ModelTemplate {
    std::string model_id;
    bool success = false;
    std::vector<unsigned char> data; // [bytes] Opaque DLL output; preserve actual length.
    double time_ms = 0; // [ms] Extract wall time, including first-call worker/model initialization.
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
    double time_ms = 0; // [ms] Compare call only.
    double attempt_time_ms = 0; // [ms] Cached attempt generation, reported separately.
    std::string stage;
    std::string error;
};

// Outcome from the selected DLL, independent of its scoring implementation.
struct SimilarityResult {
    bool success = false;
    double score = 0; // [0,1] Normalized similarity.
    double time_ms = 0; // [ms] Compare call only.
    double attempt_time_ms = 0;
    bool enrollment_failed = false;
    bool compare_executed = false;
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
    std::string algorithm_path;
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
    // Caller owns backend; reconfiguration invalidates every prior template.
    void Configure(Algorithm* backend, const std::string& dll_path);
    const std::vector<ModelState>& Models() const { return models_; }
    void SetConcurrency(int count);
    void Cancel();
    bool CancellationFinished() const;
    void ResetCancel();
    bool Cancelled() const { return cancelled_.load(); }
    int ProgressDone() const { return progress_done_.load(); }
    int ProgressTotal() const { return progress_total_.load(); }
    EnrollmentTemplates Enroll(const fingerprint::Image& image);
    std::vector<EnrollmentTemplates> EnrollImages(const std::vector<ImageInfo>& images, const std::vector<int>& selected);
    static fingerprint::Image ReadImage(const std::string& path, const std::string& database_id = "");
    std::vector<SimilarityResult> Execute(const std::vector<ComparisonPair>& pairs,
                                         const std::vector<ImageInfo>& images,
                                         const std::vector<EnrollmentTemplates>& templates);
    // Ground-truth group + best score -> PASS, MISS, REJ, FA or ERR.
    static const char* ClassifyComparison(bool genuine, const SimilarityResult& result, double threshold);
    // Write into the caller's newly created run folder.
    static int RawExportSteps(const EvaluationRun& run);
    static bool SaveRawResults(const EvaluationRun& run, const std::string& folder,
                               const ExportControl& control = {});

private:
    Algorithm* backend_ = nullptr; // Borrowed; caller keeps it alive through execution.
    std::vector<ModelState> models_;
    int revision_ = 0;
    int concurrency_ = DefaultConcurrency;
    std::atomic<bool> cancelled_{false};
    std::atomic<int> progress_done_{0};
    std::atomic<int> progress_total_{0};
    void ParallelFor(int count, const std::function<void(int)>& action);
    ModelTemplate MakeTemplate(const fingerprint::Image& image, bool enrollment);
    SimilarityResult ComparePrepared(const EnrollmentTemplates& enrollment, const ModelTemplate& attempt);
};

} // namespace engine
