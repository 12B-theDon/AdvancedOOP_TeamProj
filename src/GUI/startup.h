#pragma once

#include <QString>
#include <QtGlobal>

namespace gui {
struct StartupPlan {
    QString source_folder;
    QString algorithm_folder;
    QString setup_script;
    bool needs_preparation = false;
};

StartupPlan PlanStartup(const QString& executableFolder);
bool StartPreparation(const StartupPlan& plan, qint64 previousProcess, QString& error);
} // namespace gui
