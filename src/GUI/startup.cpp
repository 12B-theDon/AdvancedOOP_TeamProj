#include "startup.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <windows.h>

namespace gui {
namespace {
bool Present(const QString& path, qint64 minimumSize = 1) {
    const QFileInfo file(path);
    return file.isFile() && file.size() >= minimumSize;
}

QString SourceFolder(const QString& executableFolder) {
    QDir folder(executableFolder);
    do {
        if (QFileInfo::exists(folder.filePath("CMakeLists.txt"))
            && QFileInfo::exists(folder.filePath("src/main.cpp"))
            && QFileInfo::exists(folder.filePath("tools/setup_windows.ps1"))) return folder.absolutePath();
    } while (folder.cdUp());
    return QString();
}
} // namespace

StartupPlan PlanStartup(const QString& executableFolder) {
    StartupPlan plan;
    const QDir executable(executableFolder);
    plan.source_folder = SourceFolder(executableFolder);
    const QString repair = executable.filePath("install.exe");
    if (Present(repair)) plan.repair_installer = repair;
    plan.algorithm_folder = QDir::cleanPath(executable.filePath("../FingerprintAlgorithm"));
    if (!QDir(plan.algorithm_folder).exists() && !plan.source_folder.isEmpty())
        plan.algorithm_folder = QDir(plan.source_folder).filePath("dist/FingerprintAlgorithm");
    // A standalone external-DLL evaluator has no bundled algorithm to prepare.
    if (plan.source_folder.isEmpty() && !QDir(plan.algorithm_folder).exists() && plan.repair_installer.isEmpty()) return plan;
    if (!plan.source_folder.isEmpty())
        plan.setup_script = QDir(plan.source_folder).filePath("tools/setup_windows.ps1");

    const QDir algorithm(plan.algorithm_folder);
    bool ready = Present(algorithm.filePath("FingerprintAlgorithm.dll"))
        && Present(algorithm.filePath("FingerprintAlgorithmCore.dll"))
        && Present(executable.filePath("FingerprintWorker.exe"));
    for (const QString& model : QStringList{"fvc2000_db3", "fvc2002_db3", "fvc2004_db1", "combined"})
        ready = Present(algorithm.filePath(model + ".bin"), 1024) && ready;
    plan.needs_preparation = !ready;
    return plan;
}

bool StartPreparation(const StartupPlan& plan, qint64 previousProcess, QString& error) {
    if (plan.setup_script.isEmpty() && !plan.repair_installer.isEmpty()) {
        QProcess installer;
        installer.setProgram(plan.repair_installer);
        installer.setArguments({"/DIR=" + QDir::cleanPath(QDir(QFileInfo(plan.repair_installer).absolutePath()).absoluteFilePath("..")), "/NORESTART"});
        if (installer.startDetached()) return true;
        error = "Could not open the repair installer: " + installer.errorString();
        return false;
    }
    if (plan.setup_script.isEmpty()) {
        error = "The algorithm deployment is incomplete. Restore the complete FingerprintAlgorithm folder, "
                "or run the GUI from the source checkout so missing weights can be downloaded or trained.";
        return false;
    }
    QProcess setup;
    setup.setProgram(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
        .filePath("System32/WindowsPowerShell/v1.0/powershell.exe"));
    setup.setArguments({"-NoProfile", "-NoExit", "-ExecutionPolicy", "Bypass", "-File", plan.setup_script,
        "-WaitForProcessId", QString::number(previousProcess), "-LaunchGui"});
    setup.setWorkingDirectory(plan.source_folder);
    setup.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags &= ~DETACHED_PROCESS;
        arguments->flags |= CREATE_NEW_CONSOLE;
        arguments->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
    });
    if (setup.startDetached()) return true;
    error = "Could not start automatic model preparation: " + setup.errorString();
    return false;
}
} // namespace gui
