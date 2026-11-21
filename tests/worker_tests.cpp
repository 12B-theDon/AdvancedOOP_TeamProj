#include "kisaDll.h"
#include "../src/Public/FvcSpec.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
static void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static fingerprint::Image Image(int tag) {
    fingerprint::Image image; image.width = 448; image.height = 478; image.database_id = "FVC2000_DB3_B";
    image.pixels.assign(448 * 478, 0); image.pixels[0] = static_cast<unsigned char>(tag); image.pixels.back() = 201; return image;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        Check(argc == 3, "Expected fault DLL and worker");
        engine::DllOptions options; options.timeout_ms = 100; options.first_call_timeout_ms = 100; options.startup_timeout_ms = 5000;
        engine::KisaDllAlgorithm algorithm(argv[1], argv[2], options);
        std::vector<unsigned char> enrolled, attempted; std::string error; double score = 0;
        Check(algorithm.Extract(Image(0), true, enrolled, error) && enrolled.size() == 4, "Initial enroll actual length");
        const int tags[] = {1, 2, 3, 4, 5, 6, 7, 10, 11};
        const char* categories[] = {"[FAIL]", "[TIMEOUT]", "[CRASH]", "[CRASH]", "[MISMATCH]", "[MISMATCH]", "[MISMATCH]", "[MISMATCH]", "[MISMATCH]"};
        for (int i = 0; i < 9; ++i) {
            Check(!algorithm.Extract(Image(tags[i]), false, attempted, error) && error.find(categories[i]) == 0, categories[i]);
            if (tags[i] == 1) Check(error.find("42") != std::string::npos && error.find("intentional failure") != std::string::npos, "DLL error details");
            Check(algorithm.Extract(Image(0), false, attempted, error), "Worker did not restart");
            Check(algorithm.Compare(enrolled, attempted, score, error) && score == 0, "Opaque template replay after restart");
        }
        for (int tag : {8, 9, 12, 13}) {
            Check(algorithm.Extract(Image(tag), false, attempted, error), "Compare fixture extraction");
            Check(!algorithm.Compare(enrolled, attempted, score, error), "Fault comparison accepted");
            const std::string expected = tag == 12 ? "[TIMEOUT]" : tag == 13 ? "[FAIL]" : "[MISMATCH]";
            Check(error.find(expected) == 0, "Compare category");
        }
        std::future<bool> task = std::async(std::launch::async, [&] { return algorithm.Extract(Image(2), false, attempted, error); });
        std::this_thread::sleep_for(std::chrono::milliseconds(25)); algorithm.Cancel();
        Check(!task.get() && error.find("[CANCELLED]") == 0, "Cancellation category");
        algorithm.ResetCancel(); Check(algorithm.Extract(Image(0), false, attempted, error), "Restart after Stop");
        Check(algorithm.Extract(Image(16), false, attempted, error), "DLL logging corrupted IPC");
        Check(algorithm.Extract(Image(15), false, attempted, error), "Between-call stall setup");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(!algorithm.Extract(Image(0), false, attempted, error) && error.find("[TIMEOUT]") == 0, "IPC write deadline");
        Check(algorithm.Extract(Image(0), false, attempted, error), "Restart after IPC write timeout");
        // An arbitrary directory/Unicode DLL name exercises host and working-dir independence.
        QTemporaryDir folder; Check(folder.isValid(), "Temporary folder");
        const QString renamed = folder.filePath(QString::fromUtf8("외부 알고리즘.dll"));
        Check(QFile::copy(QString::fromUtf8(argv[1]), renamed), "DLL replacement fixture");
        engine::KisaDllAlgorithm renamedAlgorithm(renamed.toStdString(), argv[2], options);
        Check(renamedAlgorithm.Extract(Image(0), true, enrolled, error), "DLL at Unicode arbitrary path");
        std::vector<double> scores[2];
        for (int count : {1, 4}) {
            options.workers = count; engine::KisaDllAlgorithm parallel(argv[1], argv[2], options);
            std::vector<std::future<double>> tasks;
            for (int index = 0; index < 12; ++index) tasks.push_back(std::async(std::launch::async, [&parallel, index] {
                std::vector<unsigned char> a, b; std::string e; double s = 0;
                Check(parallel.Extract(Image(0), true, a, e) && parallel.Extract(Image(20 + index), false, b, e), "Parallel extraction");
                Check(parallel.Compare(a, b, s, e), "Parallel comparison"); return s;
            }));
            for (std::future<double>& result : tasks) scores[count == 1 ? 0 : 1].push_back(result.get());
        }
        Check(scores[0] == scores[1], "Single/multi-worker differences");
        std::cout << "Windows faults, restart, cancellation, opaque lengths, raw pixels, Unicode path and worker consistency passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
