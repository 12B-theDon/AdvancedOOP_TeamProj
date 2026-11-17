#include "report.h"
#include "guiConfig.h"
#include <QDir>
#include <QFile>
#include <cmath>
#include <limits>

static QString Value(double value) {
    return QString::number(value, 'g', GuiConfig::MetricPrecision);
}

struct ScoreGroups {
    std::vector<double> values[2];
    int total[2] = {};
    bool complete[2] = {true, true};
};

static ScoreGroups CollectScores(const EvaluationRun& run) {
    ScoreGroups groups;
    for (const auto& record : run.comparisons) {
        const int group = record.genuine ? 0 : 1;
        ++groups.total[group];
        if (!record.executed) groups.complete[group] = false;
        if (record.executed && record.result.success && std::isfinite(record.result.score))
            groups.values[group].push_back(record.result.score);
    }
    return groups;
}

static void Rates(const ScoreGroups& groups, double genuineThreshold, double impostorThreshold, bool successfulOnly, double& fmr, double& fnmr) {
    int matches = 0, nonmatches = successfulOnly ? 0 : groups.total[0] - int(groups.values[0].size());
    for (double score : groups.values[1]) if (score >= impostorThreshold) ++matches;
    for (double score : groups.values[0]) if (score < genuineThreshold) ++nonmatches;
    const int genuine = successfulOnly ? int(groups.values[0].size()) : groups.total[0];
    const int impostor = successfulOnly ? int(groups.values[1].size()) : groups.total[1];
    fmr = impostor > 0 ? double(matches) / impostor : 0;
    fnmr = genuine > 0 ? double(nonmatches) / genuine : 0;
}

static std::vector<double> Thresholds(const ScoreGroups& groups) {
    std::vector<double> thresholds;
    for (const auto& group : groups.values) {
        for (double score : group) {
            auto position = thresholds.begin();
            while (position != thresholds.end() && *position < score) ++position;
            if (position == thresholds.end() || *position != score) thresholds.insert(position, score);
        }
    }
    if (!thresholds.empty()) thresholds.push_back(std::nextafter(thresholds.back(), std::numeric_limits<double>::infinity()));
    return thresholds;
}

static QString EerBounds(const ScoreGroups& groups, bool successfulOnly, const std::vector<double>& thresholds) {
    if (thresholds.empty() || (successfulOnly && (groups.values[0].empty() || groups.values[1].empty()))) return "N/A";
    double previousFmr = 0, previousFnmr = 0;
    bool previous = false;
    for (double threshold : thresholds) {
        double fmr, fnmr;
        Rates(groups, threshold, threshold, successfulOnly, fmr, fnmr);
        if (std::abs(fmr - fnmr) <= EngineConfig::MetricTolerance) return Value(100 * fmr);
        if (previous && previousFmr > previousFnmr && fmr < fnmr) {
            const double low = previousFnmr > fmr ? previousFnmr : fmr;
            const double high = previousFmr < fnmr ? previousFmr : fnmr;
            return Value(100 * low) + " - " + Value(100 * high);
        }
        previousFmr = fmr; previousFnmr = fnmr; previous = true;
    }
    return "N/A (no crossing)";
}

QStringList Report::CalculateMetrics(const EvaluationRun& run) {
    QStringList values;
    for (int index = 0; index < GuiConfig::MetricNames.size(); ++index) values.append("N/A (not run)");
    int enrolled = 0, failed = 0, templates = 0;
    double enrollmentTime = 0, templateSize = 0, maximumSize = 0;
    for (const auto& record : run.enrollments) {
        if (!record.executed) continue;
        ++enrolled;
        if (!record.result.success) ++failed;
        enrollmentTime += record.result.time_ms;
        for (const auto& item : record.result.models) {
            if (!item.success) continue;
            ++templates;
            templateSize += item.data.size();
            if (item.data.size() > maximumSize) maximumSize = item.data.size();
        }
    }
    if (enrolled > 0) values[11] = Value(enrollmentTime / enrolled);
    if (enrolled > 0 && enrolled == int(run.enrollments.size())) values[0] = Value(100.0 * failed / enrolled);
    if (templates > 0) { values[15] = Value(templateSize / templates); values[16] = Value(maximumSize); }
    const auto groups = CollectScores(run);
    double times[2] = {};
    int executed[2] = {};
    for (const auto& record : run.comparisons) {
        if (!record.executed) continue;
        const int group = record.genuine ? 0 : 1;
        times[group] += record.result.time_ms;
        ++executed[group];
    }
    for (int group = 0; group < 2; ++group) {
        if (executed[group] > 0) values[13 + group] = Value(times[group] / executed[group]);
        if (groups.complete[group] && groups.total[group] > 0)
            values[1 + group] = Value(100.0 * (groups.total[group] - groups.values[group].size()) / groups.total[group]);
    }
    if (executed[0] + executed[1] > 0) values[12] = Value((times[0] + times[1]) / (executed[0] + executed[1]));
    double fmr, fnmr;
    Rates(groups, run.genuine_threshold, run.impostor_threshold, false, fmr, fnmr);
    if (groups.complete[1] && groups.total[1] > 0) values[3] = Value(100 * fmr);
    if (groups.complete[0] && groups.total[0] > 0) values[4] = Value(100 * fnmr);
    if (groups.complete[0] && groups.complete[1] && groups.total[0] > 0 && groups.total[1] > 0) {
        const auto thresholds = Thresholds(groups);
        values[5] = EerBounds(groups, false, thresholds);
        values[6] = EerBounds(groups, true, thresholds);
        double best[4] = {2, 2, 2, 2};
        for (double threshold : thresholds) {
            Rates(groups, threshold, threshold, false, fmr, fnmr);
            if (fmr <= EngineConfig::MetricTolerance && fnmr < best[0]) best[0] = fnmr;
            if (fnmr <= EngineConfig::MetricTolerance && fmr < best[1]) best[1] = fmr;
            if (fmr <= 0.01 + EngineConfig::MetricTolerance && fnmr < best[2]) best[2] = fnmr;
            if (fmr <= 0.001 + EngineConfig::MetricTolerance && fnmr < best[3]) best[3] = fnmr;
        }
        for (int index = 0; index < 4; ++index) values[7 + index] = best[index] <= 1 ? Value(100 * best[index]) : "N/A";
    }
    return values;
}

static QString Number(double value) {
    return QString::number(value, 'g', GuiConfig::GraphPrecision);
}

static QString Plot(const QString& title, const QString& xLabel, const QString& yLabel,
                    const QString& content) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + QString::number(GuiConfig::GraphWidth)
        + "\" height=\"" + QString::number(GuiConfig::GraphHeight) + "\" viewBox=\"0 0 840 320\">"
        "<rect width=\"840\" height=\"320\" fill=\"white\"/>"
        "<g font-family=\"Arial\" fill=\"#222\">"
        "<text x=\"28\" y=\"28\" font-size=\"22\">" + title + "</text>"
        "<path d=\"M70 60V245H810\" fill=\"none\" stroke=\"#555\"/>"
        "<text x=\"440\" y=\"302\" text-anchor=\"middle\" font-size=\"14\">" + xLabel + "</text>"
        "<text transform=\"translate(20 155) rotate(-90)\" text-anchor=\"middle\" font-size=\"14\">"
        + yLabel + "</text>" + content + "</g></svg>";
}

static QString EmptyPlot(const QString& message) {
    return "<text x=\"440\" y=\"145\" text-anchor=\"middle\" font-size=\"17\">" + message
        + "</text><text x=\"440\" y=\"174\" text-anchor=\"middle\" font-size=\"14\">"
          "Results will appear after evaluation.</text>";
}

static QString Ticks(double low, double high, double maximum) {
    return "<g font-size=\"12\"><text x=\"70\" y=\"267\">" + Number(low)
        + "</text><text x=\"810\" y=\"267\" text-anchor=\"end\">" + Number(high)
        + "</text><text x=\"62\" y=\"249\" text-anchor=\"end\">0</text>"
          "<text x=\"62\" y=\"66\" text-anchor=\"end\">" + Number(maximum) + "</text></g>";
}

static QString Line(const QString& points, const QString& color) {
    return "<polyline fill=\"none\" stroke=\"" + color + "\" stroke-width=\"2\" points=\"" + points + "\"/>";
}

static bool DerivedMetricSelected(const std::vector<bool>& selected) {
    for (int row = 5; row <= 10; ++row)
        if (row < int(selected.size()) && selected[row]) return true;
    return false;
}

QStringList Report::GraphFiles(const std::vector<bool>& selected) {
    const bool derived = DerivedMetricSelected(selected);
    const bool fmr = selected.size() > 3 && selected[3], fnmr = selected.size() > 4 && selected[4];
    QStringList files;
    if (derived || fmr || fnmr)
        files << "score_distribution.svg" << "error_rates.svg";
    if (derived || (fmr && fnmr))
        files << "error_tradeoff.svg";
    return files;
}

QString Report::GraphTitle(const QString& file, const std::vector<bool>& selected) {
    if (file == "score_distribution.svg") return "Score distribution";
    if (file == "error_tradeoff.svg") return "FMR vs FNMR (linear axes)";
    if (DerivedMetricSelected(selected) || (selected.size() > 3 && selected[3] && selected.size() > 4 && selected[4]))
        return "FMR / FNMR by threshold";
    return (selected.size() > 3 && selected[3]) ? "FMR by threshold" : "FNMR by threshold";
}

bool Report::WriteGraphs(const ReportData& data, const QString& folder) {
    const auto& selected = data.selected;
    const QStringList files = GraphFiles(selected);
    if (files.isEmpty()) return true;
    const bool derived = DerivedMetricSelected(selected);
    const bool showFmr = derived || (selected.size() > 3 && selected[3]);
    const bool showFnmr = derived || (selected.size() > 4 && selected[4]);
    std::vector<double> scores[2];
    int totals[2] = {};
    bool complete[2] = {true, true};
    double low = GuiConfig::EmptyScoreLow, high = GuiConfig::EmptyScoreHigh;
    bool first = true;
    for (const auto& record : data.run.comparisons) {
        const int group = record.genuine ? 0 : 1;
        ++totals[group];
        if (!record.executed) complete[group] = false;
        if (!record.executed || !record.result.success) continue;
        const double score = record.result.score;
        if (!std::isfinite(score)) { complete[group] = false; continue; }
        scores[group].push_back(score);
        if (first) { low = high = score; first = false; }
        if (score < low) low = score;
        if (score > high) high = score;
    }
    if (low == high) {
        low = std::fmax(0.0, low - GuiConfig::CurvePadding);
        high = std::fmin(1.0, high + GuiConfig::CurvePadding);
    }
    QString distribution, errors, tradeoff;
    if (first)
        distribution = EmptyPlot("No scored comparison results available.");
    else {
        std::vector<int> bins[2] = {std::vector<int>(GuiConfig::HistogramBins), std::vector<int>(GuiConfig::HistogramBins)};
        int maximum = 1;
        for (int group = 0; group < 2; ++group) {
            for (double score : scores[group]) {
                int bin = int((score - low) / (high - low) * GuiConfig::HistogramBins);
                if (bin >= GuiConfig::HistogramBins) bin = GuiConfig::HistogramBins - 1;
                ++bins[group][bin];
                if (bins[group][bin] > maximum) maximum = bins[group][bin];
            }
        }
        for (int group = 0; group < 2; ++group) {
            const QString color = group == 0 ? "#2468a0" : "#c15b18";
            for (int bin = 0; bin < GuiConfig::HistogramBins; ++bin) {
                const double height = 185.0 * bins[group][bin] / maximum;
                distribution += QString("<rect x=\"%1\" y=\"%2\" width=\"%5\" height=\"%3\" fill=\"%4\"/>")
                    .arg(72 + bin * (740.0 / GuiConfig::HistogramBins) + group * (340.0 / GuiConfig::HistogramBins))
                    .arg(Number(245 - height)).arg(Number(height)).arg(color).arg(Number(320.0 / GuiConfig::HistogramBins));
            }
        }
        distribution += Ticks(low, high, maximum);
    }
    distribution += "<text x=\"570\" y=\"48\" fill=\"#2468a0\">Genuine</text>"
                    "<text x=\"670\" y=\"48\" fill=\"#c15b18\">Impostor</text>";
    if (first || (showFmr && (!complete[1] || totals[1] == 0)) || (showFnmr && (!complete[0] || totals[0] == 0))) {
        errors = tradeoff = EmptyPlot("Complete the required comparison groups to plot error rates.");
    } else {
        QString fmrPoints, fnmrPoints, tradeoffPoints;
        const double start = std::fmax(0.0, low - (high - low) * GuiConfig::CurvePadding);
        const double end = std::fmin(1.0, high + (high - low) * GuiConfig::CurvePadding);
        for (int step = 0; step <= GuiConfig::CurveSteps; ++step) {
            const double threshold = start + (end - start) * step / GuiConfig::CurveSteps;
            int falseMatches = 0, falseNonMatches = totals[0] - int(scores[0].size());
            for (double score : scores[1]) if (score >= threshold) ++falseMatches;
            for (double score : scores[0]) if (score < threshold) ++falseNonMatches;
            const double fmr = totals[1] > 0 ? double(falseMatches) / totals[1] : 0;
            const double fnmr = totals[0] > 0 ? double(falseNonMatches) / totals[0] : 0;
            const QString x = Number(70 + 740.0 * step / GuiConfig::CurveSteps);
            fmrPoints += x + "," + Number(245 - 185 * fmr) + " ";
            fnmrPoints += x + "," + Number(245 - 185 * fnmr) + " ";
            tradeoffPoints += Number(70 + 740 * fmr) + "," + Number(245 - 185 * fnmr) + " ";
        }
        if (showFmr)
            errors += Line(fmrPoints, "#2468a0") + "<text x=\"620\" y=\"48\" fill=\"#2468a0\">FMR</text>";
        if (showFnmr)
            errors += Line(fnmrPoints, "#c15b18") + "<text x=\"700\" y=\"48\" fill=\"#c15b18\">FNMR</text>";
        errors += Ticks(start, end, 100);
        tradeoff = Line(tradeoffPoints, "#2468a0") + Ticks(0, 100, 100);
    }
    auto write = [&](const QString& name, const QString& svg) {
        QFile file(folder + "/" + name);
        if (!file.open(QIODevice::WriteOnly)) return false;
        const QByteArray bytes = svg.toUtf8();
        const bool written = file.write(bytes) == bytes.size();
        file.close();
        return written && file.error() == QFileDevice::NoError;
    };
    for (const auto& file : files) {
        QString svg;
        if (file == "score_distribution.svg")
            svg = Plot(GraphTitle(file, selected), "Normalized similarity (0-1)", "Successful comparisons", distribution);
        else if (file == "error_rates.svg")
            svg = Plot(GraphTitle(file, selected), "Normalized threshold (0-1)", "Error rate (%)", errors);
        else
            svg = Plot(GraphTitle(file, selected), "FMR (%)", "FNMR (%)", tradeoff);
        if (!write(file, svg)) return false;
    }
    return true;
}

static bool WriteReportFile(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}

bool Report::Save(const ReportData& data, const QString& folder) {
    const auto values = CalculateMetrics(data.run);
    QString csv = "metric,value\n";
    QString html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Fingerprint performance report</title><style>"
        "body{font:16px Arial,sans-serif;max-width:900px;margin:32px auto;padding:0 20px;color:#222}"
        "table{border-collapse:collapse;width:100%}th,td{border:1px solid #bbb;padding:7px;text-align:left}"
        "pre{white-space:pre-wrap;overflow-wrap:anywhere}img{max-width:100%}"
        "</style></head><body><h1>Fingerprint performance report</h1>";
    bool executed = false;
    for (const auto& item : data.run.comparisons) if (item.executed) executed = true;
    html += executed ? "<p>Status: Results available.</p>" : "<p>Status: Not run. No comparisons were executed.</p>";
    html += "<h2>Evaluation setup</h2><pre>" + QString::fromStdString(data.run.settings).toHtmlEscaped() + "</pre>"
        "<p>Each selected image creates an independent enrollment template. Each pair uses the highest successful model normalized similarity: (cosine + 1) / 2, from 0 to 1. This is not a calibrated probability.</p>"
        "<p>Genuine (same finger): score &gt;= threshold = PASS, below = MISS (false rejection). "
        "Impostor (different fingers): score &lt; threshold = REJ (correct rejection), otherwise = FA (false acceptance). "
        "Genuine threshold: " + Value(data.run.genuine_threshold) + "; Impostor threshold: " + Value(data.run.impostor_threshold)
        + ". ERR means inference failed without a score.</p>"
        "<table><tr><th>Model ID</th><th>Weight file</th><th>Load status</th></tr>";
    for (const auto& model : data.run.models)
        html += "<tr><td>" + QString::fromStdString(model.model_id).toHtmlEscaped() + "</td><td>"
            + QString::fromStdString(model.weight_file).toHtmlEscaped() + "</td><td>"
            + (model.loaded ? QString("Ready") : QString::fromStdString(model.error).toHtmlEscaped()) + "</td></tr>";
    html += "</table><h2>Performance metrics</h2><table><tr><th>Metric</th><th>Value</th></tr>";
    int count = 0;
    for (int row = 0; row < GuiConfig::MetricNames.size(); ++row) {
        csv += '"' + GuiConfig::MetricNames[row] + "\",\"" + values[row] + "\"\n";
        if (row < int(data.selected.size()) && data.selected[row]) {
            ++count;
            html += "<tr><td>" + GuiConfig::MetricNames[row].toHtmlEscaped() + "</td><td>" + values[row].toHtmlEscaped() + "</td></tr>";
        }
    }
    if (count == 0) html += "<tr><td colspan=\"2\">No metrics selected.</td></tr>";
    html += "</table>";
    if ((data.selected.size() > 3 && data.selected[3]) || (data.selected.size() > 4 && data.selected[4]) || DerivedMetricSelected(data.selected))
        html += "<p>FMR uses the Impostor threshold; FNMR uses the Genuine threshold. "
              "FMR/FNMR use planned-pair denominators; failed genuine attempts count as nonmatches. "
              "EER shows observed-threshold bounds; EER* uses successful pairs only. "
              "EER and curves sweep a common threshold independently of the entered thresholds.</p>";
    const auto files = GraphFiles(data.selected);
    if (!files.isEmpty()) html += "<h2>Graphs</h2>";
    for (const auto& file : files)
        html += "<h3>" + GraphTitle(file, data.selected) + "</h3><img src=\"" + file + "\" width=\""
            + QString::number(GuiConfig::GraphWidth) + "\" height=\"" + QString::number(GuiConfig::GraphHeight) + "\" alt=\""
            + GraphTitle(file, data.selected) + "\"><p><a href=\"" + file + "\">Open vector image (SVG)</a></p>";
    html += "<h2>Saved data</h2><p><a href=\"enrollments.csv\">Enrollment images</a> | "
        "<a href=\"comparisons.csv\">Comparison pairs in saved order</a> | <a href=\"metrics.csv\">All metrics</a> | "
        "<a href=\"settings.txt\">Run settings</a> | <a href=\"models.csv\">Selected models</a> | "
        "<a href=\"model_scores.csv\">Per-model scores and failures</a></p>"
        "<p>Only selected metrics appear in this report. CSV files keep all metrics and raw results. Self-image comparisons are excluded.</p></body></html>";
    const QDir destination(folder);
    return WriteReportFile(destination.filePath("metrics.csv"), csv)
        && WriteReportFile(destination.filePath("report.html"), html) && WriteGraphs(data, folder);
}
