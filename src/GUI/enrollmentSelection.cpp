#include "enrollmentSelection.h"
#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <utility>

namespace gui::selection {
namespace {
// Explicit rejection sampling keeps selection stable across standard libraries.
void Shuffle(std::vector<int>& values, std::mt19937& random) {
    for (int n = int(values.size()); n > 1; --n) {
        const auto bound = static_cast<std::uint32_t>(n);
        const auto threshold = static_cast<std::uint32_t>((std::uint64_t{1} << 32) % bound);
        std::uint32_t value;
        do { value = static_cast<std::uint32_t>(random()); } while (value < threshold);
        std::swap(values[n - 1], values[value % bound]);
    }
}

bool ReadCsv(const QString& path, const std::vector<engine::ImageInfo>& images,
             std::vector<int>& result, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "Cannot open selection CSV: " + file.errorString(); return false; }
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    QMap<QString, int> names;
    for (int i = 0; i < int(images.size()); ++i) names.insert(QString::fromStdString(images[i].name), i);
    bool header = false;
    QSet<int> seen;
    for (QString line : text.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.trimmed().isEmpty()) continue;
        QString name = line.trimmed();
        if (name.startsWith('"')) {
            if (!name.endsWith('"') || name.size() < 2) { error = "Invalid quoted CSV image name."; return false; }
            name = name.mid(1, name.size() - 2);
            QString decoded;
            for (int i = 0; i < name.size(); ++i) {
                if (name[i] == '"' && (i + 1 >= name.size() || name[++i] != '"')) {
                    error = "Invalid quoted CSV image name."; return false;
                }
                decoded += name[i];
            }
            name = decoded;
        } else if (name.contains(',') || name.contains('"')) {
            error = "Selection CSV must have one column: image. Quote names containing commas."; return false;
        }
        if (!header) {
            if (name != "image") { error = "Selection CSV needs the header: image"; return false; }
            header = true;
            continue;
        }
        if (!names.contains(name)) { error = "CSV image is not in this DB: " + name; return false; }
        const int index = names.value(name);
        if (seen.contains(index)) { error = "Duplicate CSV image: " + name; return false; }
        seen.insert(index);
        result.push_back(index);
    }
    if (!header || result.empty()) { error = "Selection CSV must contain at least one image."; return false; }
    return true;
}
} // namespace

bool Plan(const std::vector<engine::ImageInfo>& images, const Options& options,
          std::vector<int>& selected, QString& error) {
    error.clear();
    std::vector<int> planned;
    if (images.empty()) { error = "Load a DB with readable images first."; return false; }
    std::mt19937 random(options.seed);
    if (options.rule == Rule::Csv) {
        if (!ReadCsv(options.csv, images, planned, error)) return false;
    } else if (options.rule == Rule::PerFinger || options.rule == Rule::Captures) {
        QMap<int, std::vector<int>> groups;
        for (int i = 0; i < int(images.size()); ++i) {
            if (images[i].finger <= 0 || images[i].capture <= 0) {
                error = "This rule needs finger/capture IDs for every image. Use All, Random or CSV for this DB.";
                return false;
            }
            groups[images[i].finger].push_back(i);
        }
        if (options.rule == Rule::PerFinger) {
            if (options.count < 1) { error = "Enter a positive image count."; return false; }
            for (auto it = groups.begin(); it != groups.end(); ++it) {
                Shuffle(it.value(), random);
                const int count = std::min(options.count, int(it.value().size()));
                planned.insert(planned.end(), it.value().begin(), it.value().begin() + count);
            }
        } else {
            QSet<int> captures;
            for (const QString& token : options.captures.split(',')) {
                bool ok = false;
                const int capture = token.trimmed().toInt(&ok);
                if (!ok || capture <= 0) { error = "Enter positive capture numbers, for example: 1, 3."; return false; }
                captures.insert(capture);
            }
            for (int i = 0; i < int(images.size()); ++i)
                if (captures.contains(images[i].capture)) planned.push_back(i);
        }
    } else if (options.rule == Rule::All || options.rule == Rule::RandomCount) {
        planned.resize(images.size());
        std::iota(planned.begin(), planned.end(), 0);
        if (options.rule == Rule::RandomCount) {
            if (options.count < 1) { error = "Enter a positive image count."; return false; }
            Shuffle(planned, random);
            planned.resize(std::min(options.count, int(planned.size())));
        }
    } else { error = "Unknown enrollment selection rule."; return false; }
    if (planned.empty()) { error = "No images match this rule. The current selection was kept."; return false; }
    std::sort(planned.begin(), planned.end());
    selected = std::move(planned);
    return true;
}

bool SaveCsv(const QString& path, const std::vector<engine::ImageInfo>& images,
             const std::vector<int>& selected, QString& error) {
    error.clear();
    if (selected.empty()) { error = "Select at least one image first."; return false; }
    QByteArray bytes("image\n");
    QSet<int> seen;
    for (int index : selected) {
        if (index < 0 || index >= int(images.size()) || seen.contains(index)) {
            error = "Invalid or duplicate selected image."; return false;
        }
        seen.insert(index);
        QString name = QString::fromStdString(images[index].name);
        name.replace("\"", "\"\"");
        bytes += '"' + name.toUtf8() + "\"\n";
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot save selection CSV: " + file.errorString(); return false;
    }
    return true;
}
} // namespace gui::selection
