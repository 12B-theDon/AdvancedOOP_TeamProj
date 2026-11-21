#include "startup.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void File(const QString& path, qint64 size = 1) {
    Check(QDir().mkpath(QFileInfo(path).absolutePath()), "Create fixture directory");
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly) && file.resize(size), "Create fixture file");
}
void Models(const QString& folder) {
    for (const QString& model : QStringList{"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"})
        File(QDir(folder).filePath(model + ".bin"), 2048);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp(QDir::tempPath() + "/startup space_test_XXXXXX");
        Check(temp.isValid(), "Temporary fixture");
        const QDir root(temp.path() + "/project test");
        const QString executable = root.filePath("build/Release");
        Check(QDir().mkpath(executable), "Build fixture");
        File(root.filePath("CMakeLists.txt"));
        File(root.filePath("src/main.cpp"));
        File(root.filePath("tools/setup_windows.ps1"));
        gui::StartupPlan plan = gui::PlanStartup(executable);
        Check(plan.needs_preparation && plan.source_folder == root.absolutePath(), "Find source checkout from executable");
        Check(plan.setup_script == root.filePath("tools/setup_windows.ps1"), "Locate setup script");
        const QString algorithm = root.filePath("dist/FingerprintAlgorithm");
        File(QDir(algorithm).filePath("FingerprintAlgorithm.dll"));
        File(QDir(algorithm).filePath("FingerprintAlgorithmCore.dll"));
        File(QDir(executable).filePath("FingerprintWorker.exe"));
        Models(algorithm);
        Check(!gui::PlanStartup(executable).needs_preparation, "Complete deployment should not trigger setup");
        const QString combined = QDir(algorithm).filePath("combined.bin");
        File(combined, 128);
        Check(gui::PlanStartup(executable).needs_preparation, "LFS pointer must not count as a model");
        File(combined, 0);
        Check(gui::PlanStartup(executable).needs_preparation, "Empty model must trigger setup");
        File(combined, 2048);
        Check(QFile::remove(QDir(executable).filePath("FingerprintWorker.exe")), "Remove worker fixture");
        Check(gui::PlanStartup(executable).needs_preparation, "Missing worker must trigger setup");

        const QDir portable(temp.path() + "/portable");
        const QString evaluator = portable.filePath("FingerprintEvaluator");
        Check(QDir().mkpath(evaluator), "Portable evaluator fixture");
        Check(!gui::PlanStartup(evaluator).needs_preparation, "Standalone external-DLL evaluator must remain usable");
        const QString portableAlgorithm = portable.filePath("FingerprintAlgorithm");
        Models(portableAlgorithm);
        File(QDir(portableAlgorithm).filePath("FingerprintAlgorithm.dll"));
        File(QDir(portableAlgorithm).filePath("FingerprintAlgorithmCore.dll"));
        File(QDir(evaluator).filePath("FingerprintWorker.exe"));
        Check(!gui::PlanStartup(evaluator).needs_preparation, "Relocated complete package");
        Check(QFile::remove(QDir(portableAlgorithm).filePath("combined.bin")), "Remove portable model fixture");
        plan = gui::PlanStartup(evaluator);
        QString error;
        Check(plan.needs_preparation && !gui::StartPreparation(plan, 0, error) && !error.isEmpty(),
            "Missing portable model without sources must give a recovery error");
        std::cout << "Startup paths, complete bundles, missing/LFS weights, worker recovery and external-DLL compatibility passed.\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
