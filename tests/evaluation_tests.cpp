#include "Engine.h"
#include "report.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class FakeAlgorithm final : public engine::Algorithm {
public:
    std::atomic<int> attempts{0};
    std::atomic<int> compares{0};
    std::atomic<bool> cancelled{false};
    bool slow = false;
    bool bad_score = false;
    bool Extract(const fingerprint::Image& image, bool enrollment, std::vector<unsigned char>& bytes, std::string& error) override {
        Check(image.width == 3 && image.height == 2 && image.pixels.size() == 6, "Raw image geometry/packing changed");
        Check(image.pixels[3] == image.pixels[0] + 3, "Qt row padding reached algorithm");
        if (!enrollment) ++attempts;
        if (cancelled.load()) { error = "[CANCELLED] stopped"; return false; }
        bytes = {image.pixels[0], static_cast<unsigned char>(enrollment ? 1 : 2), 0xab};
        return true;
    }
    bool Compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, double& score, std::string& error, double* time_ms = nullptr) override {
        if (time_ms) *time_ms = .25;
        ++compares;
        Check(a.size() == 3 && b.size() == 3 && a[1] == 1 && b[1] == 2 && a[2] == 0xab, "Opaque bytes or Enroll/Attempt routing changed");
        for (int i = 0; slow && i < 100; ++i) {
            if (cancelled.load()) { error = "[CANCELLED] stopped"; return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        score = bad_score ? 1.1 : (a[0] + b[0]) / 100.0; return true;
    }
    void Cancel() override { cancelled.store(true); }
    void ResetCancel() override { cancelled.store(false); }
};
static double Metric(const QString& text) { return text.section(' ', 0, 0).toDouble(); }
static engine::ComparisonRecord Record(bool genuine, bool success, double score, bool blocked = false, const char* error = "") {
    engine::ComparisonRecord r; r.genuine = genuine; r.executed = true;
    r.enrollment = "gallery"; r.attempt = "probe"; r.result.success = success;
    r.result.score = score; r.result.enrollment_failed = blocked; r.result.compare_executed = !blocked;
    r.result.time_ms = blocked ? 0 : 2;
    engine::ModelScore s; s.model_id = engine::DllModelId; s.success = success; s.score = score; s.error = error;
    r.result.model_scores.push_back(s); return r;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        Check(argc == 3, "Expected source and output folders");
        const QDir output(QString::fromUtf8(argv[2])); QDir().mkpath(output.absolutePath());
        std::vector<engine::ImageInfo> images;
        for (int i = 0; i < 4; ++i) {
            QImage image(3, 2, QImage::Format_Grayscale8);
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) image.scanLine(y)[x] = static_cast<unsigned char>(i * 10 + y * 3 + x);
            const QString file = output.filePath(QString("image%1.png").arg(i)); Check(image.save(file), "Cannot save fixture");
            images.push_back({std::to_string(i), i / 2 + 1, i % 2 + 1, i, file.toStdString(), ""});
        }
        engine::ComparisonOptions options; std::vector<engine::ComparisonPair> pairs; std::string error;
        Check(engine::Engine::PlanComparisons(images, {0}, true, options, pairs, error) && pairs.size() == 1 && pairs[0].attempt == 1, "Genuine planning");
        Check(engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error) && pairs.size() == 2, "Impostor planning");
        options.rule = engine::ComparisonRule::FirstCapture;
        Check(engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error) && pairs.size() == 1 && pairs[0].attempt == 2, "First capture");
        options.rule = engine::ComparisonRule::CaptureNumbers; options.captures = {2};
        Check(engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error) && pairs.size() == 1 && pairs[0].attempt == 3, "Capture selection");
        options.rule = engine::ComparisonRule::RandomCount; options.count = 1; options.seed = 42;
        Check(engine::Engine::PlanComparisons(images, {0, 1}, false, options, pairs, error), "Random plan");
        const std::vector<engine::ComparisonPair> first = pairs;
        Check(engine::Engine::PlanComparisons(images, {0, 1}, false, options, pairs, error) && pairs.size() == first.size(), "Random replay");
        for (std::size_t i = 0; i < pairs.size(); ++i) Check(pairs[i].attempt == first[i].attempt && pairs[i].enrollment == first[i].enrollment, "Seed order");
        options.rule = engine::ComparisonRule::FromCsv; options.csv_pairs = {{0, 3}, {0, 2}};
        Check(engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error) && pairs[0].attempt == 3, "Partial CSV order");
        options.csv_pairs = {{0, 0}}; Check(!engine::Engine::PlanComparisons(images, {0}, true, options, pairs, error), "Self exclusion");
        images[1].source_id = 0; options.csv_pairs = {{0, 1}};
        Check(!engine::Engine::PlanComparisons(images, {0}, true, options, pairs, error), "Identical copy exclusion"); images[1].source_id = 1;
        options.csv_pairs = {{0, 2}, {0, 2}}; Check(!engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error), "Duplicate CSV rejected");
        options.csv_pairs = {{0, 1}}; Check(!engine::Engine::PlanComparisons(images, {0}, false, options, pairs, error), "CSV labels respected");
        FakeAlgorithm algorithm; engine::Engine engine; engine.Configure(&algorithm, "fake DLL");
        std::vector<engine::EnrollmentTemplates> templates = engine.EnrollImages(images, {0, 1, 0});
        Check(templates.size() == 4 && engine.ProgressDone() == 2, "Enrollment selection deduplicated");
        pairs = {{0, 2}, {1, 3}, {0, 3}, {1, 2}};
        const std::vector<engine::SimilarityResult> single = engine.Execute(pairs, images, templates);
        Check(algorithm.attempts.load() == 2 && single.size() == pairs.size(), "Attempt cache");
        engine.SetConcurrency(4);
        const std::vector<engine::SimilarityResult> multi = engine.Execute(pairs, images, templates);
        Check(algorithm.attempts.load() == 4 && engine.ProgressDone() == engine.ProgressTotal(), "Progress and per-run cache");
        for (std::size_t i = 0; i < pairs.size(); ++i) Check(single[i].success && multi[i].score == single[i].score && single[i].time_ms == .25, "Single/multi-worker score/order");
        algorithm.bad_score = true;
        const std::vector<engine::SimilarityResult> invalidScores = engine.Execute(pairs, images, templates);
        Check(!invalidScores[0].success && invalidScores[0].model_scores[0].error.find("[MISMATCH]") == 0, "Generic score validation");
        algorithm.bad_score = false;
        engine.Configure(&algorithm, "replacement DLL");
        const std::vector<engine::SimilarityResult> stale = engine.Execute(pairs, images, templates);
        Check(!stale[0].success && stale[0].enrollment_failed && stale[0].model_scores[0].error.find("[MISMATCH]") == 0, "Replacement invalidates templates");
        templates = engine.EnrollImages(images, {0, 1}); algorithm.slow = true;
        std::future<std::vector<engine::SimilarityResult>> task = std::async(std::launch::async, [&] { return engine.Execute(pairs, images, templates); });
        std::this_thread::sleep_for(std::chrono::milliseconds(15)); engine.Cancel();
        Check(task.get().empty() && engine.Cancelled(), "Cancelled run results discarded");
        engine.ResetCancel(); algorithm.slow = false;
        Check(engine.Execute(pairs, images, templates).size() == 4, "Restart after cancellation");
        engine::EvaluationRun run; run.models = engine.Models(); run.settings = "algorithm=opaque fixture";
        engine::EnrollmentRecord enrolled; enrolled.name = "gallery"; enrolled.image_index = 0; enrolled.result = templates[0]; run.enrollments.push_back(enrolled);
        engine::EnrollmentRecord failed = enrolled; failed.name = "failed"; failed.image_index = 1;
        failed.result.success = false; failed.result.models[0].success = false; failed.result.models[0].data.clear(); failed.result.models[0].error = "[TIMEOUT] test"; run.enrollments.push_back(failed);
        run.comparisons = {Record(true, false, 0, true, "[TIMEOUT] enrolled"), Record(true, false, 0, false, "[CRASH] test"), Record(true, true, .2), Record(true, true, .9), Record(false, true, .8), Record(false, false, 0, true, "[TIMEOUT] enrolled")};
        const QStringList metrics = gui::CalculateMetrics(run);
        Check(std::abs(Metric(metrics[0]) - 50) < .001 && metrics[0].contains("TIMEOUT=1"), "FTE failures");
        Check(std::abs(Metric(metrics[1]) - 100.0 / 3) < .001 && metrics[1].contains("CRASH=1"), "REJ attempted denominator");
        Check(Metric(metrics[3]) == 50 && Metric(metrics[4]) == 75 && Metric(metrics[12]) == 2, "Assignment FMR/FNMR/time");
        std::vector<bool> selected(17, true);
        Check(engine::Engine::SaveRawResults(run, output.absolutePath().toStdString()) && gui::Save(run, selected, output.absolutePath()), "Report/raw export");
        QFile opaque(output.filePath("templates/0_kisa_dll.fpt")); Check(opaque.open(QIODevice::ReadOnly) && opaque.size() == 3, "Saved actual opaque length");
        Check(QFile::exists(output.filePath("report.rtf")) && QFile::exists(output.filePath("error_rates.svg")), "Word/vector exports");
        const QString selectedFolder = output.filePath("selected_report"); QDir().mkpath(selectedFolder);
        std::vector<bool> onlyFte(17, false); onlyFte[0] = true;
        Check(gui::Save(run, onlyFte, selectedFolder), "Selected report export");
        QFile selectedHtml(QDir(selectedFolder).filePath("report.html")); Check(selectedHtml.open(QIODevice::ReadOnly), "Selected HTML");
        const QByteArray html = selectedHtml.readAll();
        Check(html.contains("FTE (%)") && !html.contains("FMR (%)") && gui::GraphFiles(onlyFte).isEmpty(), "Unchecked report items excluded");
        QFile allMetrics(QDir(selectedFolder).filePath("metrics.csv")); Check(allMetrics.open(QIODevice::ReadOnly) && allMetrics.readAll().contains("FMR (%)"), "CSV keeps all metrics");
        run.comparisons.push_back(Record(true, true, .5)); run.comparisons.back().executed = false;
        Check(gui::CalculateMetrics(run)[4].startsWith("N/A"), "Incomplete evaluation cannot claim final FNMR");
        std::cout << "Planning/replay, raw pixels, opaque templates, cache, cancellation, replacement, parallel order, metrics and exports passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
