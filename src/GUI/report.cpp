#include "report.h"
#include "../Engine/Engine.h"
#include "guiConfig.h"
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>

namespace gui {

static QString Value(double value) {
    return QString::number(value, 'g', MetricPrecision);
}

struct ScoreGroups {
    std::vector<double> values[2];
    int total[2] = {};
    bool complete[2] = {true, true};
};

static ScoreGroups CollectScores(const engine::EvaluationRun& run) {
    ScoreGroups groups;
    for (const engine::ComparisonRecord& record : run.comparisons) {
        const int group = record.genuine ? 0 : 1;
        ++groups.total[group];
        if (!record.executed) groups.complete[group] = false;
        if (record.executed && record.result.success && std::isfinite(record.result.score))
            groups.values[group].push_back(record.result.score);
    }
    for (int group = 0; group < 2; ++group)
        std::sort(groups.values[group].begin(), groups.values[group].end());
    return groups;
}

static void Rates(const ScoreGroups& groups, double genuineThreshold, double impostorThreshold, bool successfulOnly, double& fmr, double& fnmr) {
    const int matches = int(groups.values[1].end() - std::lower_bound(groups.values[1].begin(), groups.values[1].end(), impostorThreshold));
    const int nonmatches = int(std::lower_bound(groups.values[0].begin(), groups.values[0].end(), genuineThreshold) - groups.values[0].begin())
        + (successfulOnly ? 0 : groups.total[0] - int(groups.values[0].size()));
    const int genuine = successfulOnly ? int(groups.values[0].size()) : groups.total[0];
    const int impostor = successfulOnly ? int(groups.values[1].size()) : groups.total[1];
    fmr = impostor > 0 ? double(matches) / impostor : 0;
    fnmr = genuine > 0 ? double(nonmatches) / genuine : 0;
}

static std::vector<double> Thresholds(const ScoreGroups& groups) {
    std::vector<double> thresholds;
    for (int group = 0; group < 2; ++group)
        thresholds.insert(thresholds.end(), groups.values[group].begin(), groups.values[group].end());
    std::sort(thresholds.begin(), thresholds.end());
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end()), thresholds.end());
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
        if (std::abs(fmr - fnmr) <= engine::MetricTolerance) return Value(100 * fmr);
        if (previous && previousFmr > previousFnmr && fmr < fnmr) {
            // Assignment p.8: choose the bracketing threshold with the smaller
            // midpoint, then report its bounds and their estimated midpoint.
            const bool first = previousFnmr + previousFmr <= fmr + fnmr;
            const double low = first ? previousFnmr : fmr;
            const double high = first ? previousFmr : fnmr;
            return Value(50 * (low + high)) + " [" + Value(100 * low) + " - " + Value(100 * high) + "]";
        }
        previousFmr = fmr; previousFnmr = fnmr; previous = true;
    }
    return "N/A (no crossing)";
}

static int FailureIndex(const std::string& error) {
    const char* tags[] = {"FAIL", "TIMEOUT", "CRASH", "MISMATCH", "CANCELLED"};
    for (int i = 0; i < 5; ++i) if (error.find(std::string("[") + tags[i] + "]") != std::string::npos) return i;
    return 0;
}
static QString FailureCounts(const std::array<int, 5>& counts) {
    return QString(" (FAIL=%1, TIMEOUT=%2, CRASH=%3, MISMATCH=%4, CANCELLED=%5)")
        .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3]).arg(counts[4]);
}

QStringList CalculateMetrics(const engine::EvaluationRun& run) {
    QStringList values;
    for (int index = 0; index < MetricNames.size(); ++index) values.append("N/A (not run)");
    int enrolled = 0, failed = 0, templates = 0;
    std::array<int, 5> enrollmentFailures = {};
    double enrollmentTime = 0, templateSize = 0, maximumSize = 0;
    for (const engine::EnrollmentRecord& record : run.enrollments) {
        if (!record.result.executed) continue;
        ++enrolled;
        if (!record.result.success) {
            ++failed;
            const std::string error = record.result.models.empty() ? "" : record.result.models[0].error;
            ++enrollmentFailures[FailureIndex(error)];
        }
        enrollmentTime += record.result.time_ms;
        for (const engine::ModelTemplate& item : record.result.models) {
            if (!item.success) continue;
            ++templates;
            templateSize += item.data.size();
            if (item.data.size() > maximumSize) maximumSize = item.data.size();
        }
    }
    if (enrolled > 0) values[11] = Value(enrollmentTime / enrolled);
    if (enrolled > 0 && enrolled == int(run.enrollments.size())) values[0] = Value(100.0 * failed / enrolled) + FailureCounts(enrollmentFailures);
    if (templates > 0) { values[15] = Value(templateSize / templates); values[16] = Value(maximumSize); }
    const ScoreGroups groups = CollectScores(run);
    double times[2] = {};
    int compared[2] = {}, attempted[2] = {}, rejected[2] = {};
    std::array<int, 5> matchingFailures[2] = {};
    for (const engine::ComparisonRecord& record : run.comparisons) {
        if (!record.executed) continue;
        const int group = record.genuine ? 0 : 1;
        if (record.result.enrollment_failed) continue;
        ++attempted[group];
        if (!record.result.success) {
            ++rejected[group];
            const std::string error = record.result.model_scores.empty() ? "" : record.result.model_scores[0].error;
            ++matchingFailures[group][FailureIndex(error)];
        }
        if (record.result.compare_executed) {
            times[group] += record.result.time_ms; ++compared[group];
        }
    }
    for (int group = 0; group < 2; ++group) {
        if (compared[group] > 0) values[13 + group] = Value(times[group] / compared[group]);
        if (groups.complete[group] && attempted[group] > 0)
            values[1 + group] = Value(100.0 * rejected[group] / attempted[group]) + FailureCounts(matchingFailures[group]);
    }
    if (compared[0] + compared[1] > 0) values[12] = Value((times[0] + times[1]) / (compared[0] + compared[1]));
    double fmr, fnmr;
    Rates(groups, run.genuine_threshold, run.impostor_threshold, false, fmr, fnmr);
    if (groups.complete[1] && groups.total[1] > 0) values[3] = Value(100 * fmr);
    if (groups.complete[0] && groups.total[0] > 0) values[4] = Value(100 * fnmr);
    if (groups.complete[0] && groups.complete[1] && groups.total[0] > 0 && groups.total[1] > 0) {
        const std::vector<double> thresholds = Thresholds(groups);
        values[5] = EerBounds(groups, false, thresholds);
        values[6] = EerBounds(groups, true, thresholds);
        double best[4] = {2, 2, 2, 2};
        for (double threshold : thresholds) {
            Rates(groups, threshold, threshold, false, fmr, fnmr);
            if (fmr <= engine::MetricTolerance && fnmr < best[0]) best[0] = fnmr;
            if (fnmr <= engine::MetricTolerance && fmr < best[1]) best[1] = fmr;
            if (fmr <= 0.01 + engine::MetricTolerance && fnmr < best[2]) best[2] = fnmr;
            if (fmr <= 0.001 + engine::MetricTolerance && fnmr < best[3]) best[3] = fnmr;
        }
        for (int index = 0; index < 4; ++index) values[7 + index] = best[index] <= 1 ? Value(100 * best[index]) : "N/A";
    }
    return values;
}

static QString Number(double value) {
    return QString::number(value, 'g', GraphPrecision);
}

static QString Plot(const QString& title, const QString& xLabel, const QString& yLabel,
                    const QString& content) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + QString::number(GraphWidth)
        + "\" height=\"" + QString::number(GraphHeight) + "\" viewBox=\"0 0 840 320\">"
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

QStringList GraphFiles(const std::vector<bool>& selected) {
    const bool derived = DerivedMetricSelected(selected);
    const bool fmr = selected.size() > 3 && selected[3], fnmr = selected.size() > 4 && selected[4];
    QStringList files;
    if (derived || fmr || fnmr)
        files << "score_distribution.svg" << "error_rates.svg";
    if (derived || (fmr && fnmr))
        files << "error_tradeoff.svg";
    return files;
}

QString GraphTitle(const QString& file, const std::vector<bool>& selected) {
    if (file == "score_distribution.svg") return "Score distribution";
    if (file == "error_tradeoff.svg") return "FMR vs FNMR (linear axes)";
    if (DerivedMetricSelected(selected) || (selected.size() > 3 && selected[3] && selected.size() > 4 && selected[4]))
        return "FMR / FNMR by threshold";
    return (selected.size() > 3 && selected[3]) ? "FMR by threshold" : "FNMR by threshold";
}

static bool WriteReportFile(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) return false;
    file.close();
    return file.error() == QFileDevice::NoError;
}


bool WriteGraphs(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder) {
    const QStringList files = GraphFiles(selected);
    if (files.isEmpty()) return true;
    const bool derived = DerivedMetricSelected(selected);
    const bool showFmr = derived || (selected.size() > 3 && selected[3]);
    const bool showFnmr = derived || (selected.size() > 4 && selected[4]);
    ScoreGroups groups = CollectScores(run);
    double low = EmptyScoreLow, high = EmptyScoreHigh;
    bool first = true;
    for (const engine::ComparisonRecord& record : run.comparisons) {
        if (!record.executed || !record.result.success) continue;
        const double score = record.result.score;
        if (!std::isfinite(score)) {
            groups.complete[record.genuine ? 0 : 1] = false;
            continue;
        }
        if (first) { low = high = score; first = false; }
        if (score < low) low = score;
        if (score > high) high = score;
    }
    if (low == high) {
        low = std::fmax(0.0, low - CurvePadding);
        high = std::fmin(1.0, high + CurvePadding);
    }
    QString distribution, errors, tradeoff;
    if (first)
        distribution = EmptyPlot("No scored comparison results available.");
    else {
        std::vector<int> bins[2] = {std::vector<int>(HistogramBins), std::vector<int>(HistogramBins)};
        int maximum = 1;
        for (int group = 0; group < 2; ++group) {
            for (double score : groups.values[group]) {
                int bin = int((score - low) / (high - low) * HistogramBins);
                if (bin >= HistogramBins) bin = HistogramBins - 1;
                ++bins[group][bin];
                if (bins[group][bin] > maximum) maximum = bins[group][bin];
            }
        }
        for (int group = 0; group < 2; ++group) {
            const QString color = group == 0 ? "#2468a0" : "#c15b18";
            for (int bin = 0; bin < HistogramBins; ++bin) {
                const double height = 185.0 * bins[group][bin] / maximum;
                distribution += QString("<rect x=\"%1\" y=\"%2\" width=\"%5\" height=\"%3\" fill=\"%4\"/>")
                    .arg(72 + bin * (740.0 / HistogramBins) + group * (340.0 / HistogramBins))
                    .arg(Number(245 - height)).arg(Number(height)).arg(color).arg(Number(320.0 / HistogramBins));
            }
        }
        distribution += Ticks(low, high, maximum);
    }
    distribution += "<text x=\"570\" y=\"48\" fill=\"#2468a0\">Genuine</text>"
                    "<text x=\"670\" y=\"48\" fill=\"#c15b18\">Impostor</text>";
    if (first || (showFmr && (!groups.complete[1] || groups.total[1] == 0)) || (showFnmr && (!groups.complete[0] || groups.total[0] == 0))) {
        errors = tradeoff = EmptyPlot("Complete the required comparison groups to plot error rates.");
    } else {
        QString fmrPoints, fnmrPoints, tradeoffPoints;
        const double start = std::fmax(0.0, low - (high - low) * CurvePadding);
        const double end = std::fmin(1.0, high + (high - low) * CurvePadding);
        for (int step = 0; step <= CurveSteps; ++step) {
            const double threshold = start + (end - start) * step / CurveSteps;
            double fmr, fnmr;
            Rates(groups, threshold, threshold, false, fmr, fnmr);
            const QString x = Number(70 + 740.0 * step / CurveSteps);
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
    for (const QString& file : files) {
        QString svg;
        if (file == "score_distribution.svg")
            svg = Plot(GraphTitle(file, selected), "Normalized similarity (0-1)", "Successful comparisons", distribution);
        else if (file == "error_rates.svg")
            svg = Plot(GraphTitle(file, selected), "Normalized threshold (0-1)", "Error rate (%)", errors);
        else
            svg = Plot(GraphTitle(file, selected), "FMR (%)", "FNMR (%)", tradeoff);
        if (!WriteReportFile(folder + "/" + file, svg)) return false;
    }
    return true;
}


static QString RtfText(const QString& text) {
    QString result;
    for (QChar c : text) {
        if (c == '\\' || c == '{' || c == '}') result += '\\';
        if (c == '\n') result += "\\par ";
        else if (c.unicode() > 127) result += "\\u" + QString::number(static_cast<short>(c.unicode())) + '?';
        else result += c;
    }
    return result;
}

bool Save(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder) {
    const QStringList values = CalculateMetrics(run);
    QString csv = "metric,value\n";
    QString html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Fingerprint performance report</title><style>"
        "body{font:16px Arial,sans-serif;max-width:900px;margin:32px auto;padding:0 20px;color:#222}"
        "table{border-collapse:collapse;width:100%}th,td{border:1px solid #bbb;padding:7px;text-align:left}"
        "pre{white-space:pre-wrap;overflow-wrap:anywhere}img{max-width:100%}"
        "</style></head><body><h1>Fingerprint performance report</h1>";
    bool executed = false;
    for (const engine::ComparisonRecord& item : run.comparisons) if (item.executed) executed = true;
    html += executed ? "<p>Status: Results available.</p>" : "<p>Status: Not run. No comparisons were executed.</p>";
    html += "<h2>Evaluation setup</h2>"
        "<p>Each enrolled image is saved as a template. Higher scores mean more similar fingerprints. The selected DLL supplies the similarity; its templates are opaque. Scores are not match probabilities.</p>"
        "<details><summary>Run settings</summary><pre>" + QString::fromStdString(run.settings).toHtmlEscaped() + "</pre></details>"
        "<p>Genuine (same finger): score &gt;= threshold = PASS, below = MISS (false rejection). "
        "Impostor (different fingers): score &lt; threshold = REJ (correct rejection), otherwise = FA (false acceptance). "
        "Genuine threshold: " + Value(run.genuine_threshold) + "; Impostor threshold: " + Value(run.impostor_threshold)
        + ". ERR means inference failed without a score.</p>"
        "<table><tr><th>Algorithm</th><th>DLL file</th><th>Status</th></tr>";
    for (const engine::ModelState& model : run.models)
        html += "<tr><td>" + QString::fromStdString(model.model_id).toHtmlEscaped() + "</td><td>"
            + QString::fromStdString(model.dll_file).toHtmlEscaped() + "</td><td>"
            + (model.loaded ? QString("Ready") : QString::fromStdString(model.error).toHtmlEscaped()) + "</td></tr>";
    html += "</table><h2>Performance metrics</h2><table><tr><th>Metric</th><th>Value</th></tr>";
    int count = 0;
    for (int row = 0; row < MetricNames.size(); ++row) {
        csv += '"' + MetricNames[row] + "\",\"" + values[row] + "\"\n";
        if (row < int(selected.size()) && selected[row]) {
            ++count;
            html += "<tr><td>" + MetricNames[row].toHtmlEscaped() + "</td><td>" + values[row].toHtmlEscaped() + "</td></tr>";
        }
    }
    if (count == 0) html += "<tr><td colspan=\"2\">No metrics selected.</td></tr>";
    html += "</table>";
    if ((selected.size() > 3 && selected[3]) || (selected.size() > 4 && selected[4]) || DerivedMetricSelected(selected))
        html += "<p>FMR uses the Impostor threshold; FNMR uses the Genuine threshold. "
              "FMR/FNMR use planned-pair denominators; failed genuine attempts count as nonmatches. "
              "REJ excludes pairs blocked by enrollment failure. Matching time measures KISA_Compare only; attempt generation is separate in model_scores.csv. EER shows the assignment midpoint estimate and bounds; EER* uses successful pairs only. "
              "EER and curves sweep a common threshold independently of the entered thresholds.</p>";
    const QStringList files = GraphFiles(selected);
    if (!files.isEmpty()) html += "<h2>Graphs</h2>";
    for (const QString& file : files)
        html += "<h3>" + GraphTitle(file, selected) + "</h3><img src=\"" + file + "\" width=\""
            + QString::number(GraphWidth) + "\" height=\"" + QString::number(GraphHeight) + "\" alt=\""
            + GraphTitle(file, selected) + "\"><p><a href=\"" + file + "\">Open vector image (SVG)</a></p>";
    html += "<h2>Saved data</h2><p><a href=\"enrollments.csv\">Enrollment images</a> | "
        "<a href=\"comparisons.csv\">Comparison pairs in saved order</a> | <a href=\"metrics.csv\">All metrics</a> | "
        "<a href=\"settings.txt\">Run settings</a> | <a href=\"models.csv\">Selected models</a> | "
        "<a href=\"model_scores.csv\">Per-model scores and failures</a></p>"
        "<p>Only selected metrics appear in this report. CSV files keep all metrics and raw results. Self-image comparisons are excluded.</p></body></html>";
    // RTF is directly editable in Word and needs no Office automation/runtime.
    // Graphs are exported separately as vector SVGs beside both reports.
    QString word = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\f0\\fs24 "
        "\\b Fingerprint performance report\\b0\\par ";
    word += RtfText(QString::fromStdString(run.settings)) + "\\par ";
    for (int row = 0; row < MetricNames.size(); ++row)
        if (row < static_cast<int>(selected.size()) && selected[row])
            word += RtfText(MetricNames[row] + ": " + values[row]) + "\\par ";
    for (const QString& file : files) word += RtfText("Vector graph: " + file) + "\\par ";
    word += "}";
    const QDir destination(folder);
    return WriteReportFile(destination.filePath("metrics.csv"), csv)
        && WriteReportFile(destination.filePath("report.html"), html)
        && WriteReportFile(destination.filePath("report.rtf"), word) && WriteGraphs(run, selected, folder);
}

} // namespace gui
