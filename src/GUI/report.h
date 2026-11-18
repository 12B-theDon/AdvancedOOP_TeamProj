#pragma once

#include <QStringList>
#include <vector>

namespace engine { struct EvaluationRun; }

namespace gui {

// Metrics, vector graphs and HTML; raw data is saved by Engine.
// Percentages [%], times [ms], sizes [bytes]; EER uses observed-threshold bounds.
QStringList CalculateMetrics(const engine::EvaluationRun& run);
QStringList GraphFiles(const std::vector<bool>& selected);
QString GraphTitle(const QString& file, const std::vector<bool>& selected);
bool WriteGraphs(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder);
bool Save(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder);
} // namespace gui
