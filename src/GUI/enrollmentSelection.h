#pragma once

#include "Engine.h"
#include <QString>

namespace gui::selection {
enum class Rule { All, RandomCount, PerFinger, Captures, Csv };
struct Options {
    Rule rule = Rule::PerFinger;
    int count = 2;
    unsigned int seed = 42;
    QString captures;
    QString csv;
};
// Select image indices only; no templates, inference or comparison labels change.
bool Plan(const std::vector<engine::ImageInfo>& images, const Options& options,
          std::vector<int>& selected, QString& error);
bool SaveCsv(const QString& path, const std::vector<engine::ImageInfo>& images,
             const std::vector<int>& selected, QString& error);
} // namespace gui::selection
