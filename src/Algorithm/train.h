#pragma once
#include <ATen/core/Tensor.h>
#include "augment.h"
#include <QJsonObject>

namespace algorithm {

struct TrainingData {
    QVector<TrainingImage> samples;
    at::Tensor features; // The same tensor type exposed by torch::Tensor.
    QString checkpoint;
    QJsonObject cache_info;
};
// Shares the native GUI preprocessing. An existing cache must match all provenance checks.
TrainingData PrepareTrainingData(const QString& dataFolder, const QString& checkpoint, const QString& cacheFolder);

// Validate saved models and calibrate every GUI model subset.
void EvaluateTrainingRun(const QString& folder, const TrainingData& data, const QString& baselineFolder);

// CLI implementation; the global main delegates here.
int RunTraining(int argc, char** argv);

} // namespace algorithm
