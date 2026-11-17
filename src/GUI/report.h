#pragma once

#include "Engine.h"
#include <QStringList>
#include <vector>

// Report selection and display values, independent of widgets.
struct ReportData {
    EvaluationRun run;
    std::vector<bool> selected; // Same order as GuiConfig::MetricNames.
};

// Metrics, vector graphs and HTML; raw data is saved by Engine.
namespace Report {
// Percentages [%], times [ms], sizes [bytes]; EER uses observed-threshold bounds.
QStringList CalculateMetrics(const EvaluationRun& run);
QStringList GraphFiles(const std::vector<bool>& selected);
QString GraphTitle(const QString& file, const std::vector<bool>& selected);
bool WriteGraphs(const ReportData& data, const QString& folder);
bool Save(const ReportData& data, const QString& folder);
}
