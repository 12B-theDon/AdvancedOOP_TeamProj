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
    return QString::number(value, 'f', MetricPrecision);
}

struct ScoreGroups {
    std::vector<double> values[2];
    int total[2] = {};
    bool complete[2] = {true, true};
};

static void SortScores(std::vector<double>& values, const engine::ExportControl& control) {
    std::size_t comparisons = 0;
    std::sort(values.begin(), values.end(), [&control, &comparisons](double a, double b) {
        if ((++comparisons % 4096) == 0) control.Check();
        return a < b;
    });
    control.Check();
}

static ScoreGroups CollectScores(const engine::EvaluationRun& run, const engine::ExportControl& control) {
    ScoreGroups groups;
    for (const engine::ComparisonRecord& record : run.comparisons) {
        control.Check();
        const int group = record.genuine ? 0 : 1;
        ++groups.total[group];
        if (!record.executed) groups.complete[group] = false;
        if (record.executed && record.result.success && std::isfinite(record.result.score))
            groups.values[group].push_back(record.result.score);
    }
    for (int group = 0; group < 2; ++group)
        SortScores(groups.values[group], control);
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

static std::vector<double> Thresholds(const ScoreGroups& groups, const engine::ExportControl& control) {
    std::vector<double> thresholds;
    for (int group = 0; group < 2; ++group)
        thresholds.insert(thresholds.end(), groups.values[group].begin(), groups.values[group].end());
    SortScores(thresholds, control);
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end()), thresholds.end());
    if (!thresholds.empty()) thresholds.push_back(std::nextafter(thresholds.back(), std::numeric_limits<double>::infinity()));
    return thresholds;
}

struct OperatingPoint {
    double threshold = 0, fmr = 0, fnmr = 0;
    bool available = false;
};

static OperatingPoint EerPoint(const ScoreGroups& groups, bool successfulOnly, const std::vector<double>& thresholds,
                                const engine::ExportControl& control) {
    OperatingPoint previous;
    for (double threshold : thresholds) {
        control.Check();
        double fmr, fnmr;
        Rates(groups, threshold, threshold, successfulOnly, fmr, fnmr);
        const OperatingPoint current{threshold, fmr, fnmr, true};
        if (std::abs(fmr - fnmr) <= engine::MetricTolerance) return current;
        if (previous.available && previous.fmr > previous.fnmr && fmr < fnmr) {
            // Assignment p.8: choose the bracketing threshold with the smaller
            // midpoint, then report its bounds and their estimated midpoint.
            return previous.fnmr + previous.fmr <= fmr + fnmr ? previous : current;
        }
        previous = current;
    }
    return {};
}

static QString EerBounds(const ScoreGroups& groups, bool successfulOnly, const std::vector<double>& thresholds,
                         const engine::ExportControl& control) {
    if (thresholds.empty() || (successfulOnly && (groups.values[0].empty() || groups.values[1].empty()))) return "N/A";
    const OperatingPoint point = EerPoint(groups, successfulOnly, thresholds, control);
    if (!point.available) return "N/A (no crossing)";
    if (std::abs(point.fmr - point.fnmr) <= engine::MetricTolerance) return Value(100 * point.fmr);
    return Value(50 * (point.fmr + point.fnmr)) + " [" + Value(100 * std::min(point.fmr, point.fnmr))
        + " - " + Value(100 * std::max(point.fmr, point.fnmr)) + "]";
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

QStringList CalculateMetrics(const engine::EvaluationRun& run, const engine::ExportControl& control) {
    control.Check();
    QStringList values;
    for (int index = 0; index < MetricNames.size(); ++index) values.append("N/A (not run)");
    int enrolled = 0, failed = 0, templates = 0;
    std::array<int, 5> enrollmentFailures = {};
    double enrollmentTime = 0, templateSize = 0, maximumSize = 0;
    for (const engine::EnrollmentRecord& record : run.enrollments) {
        control.Check();
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
    const ScoreGroups groups = CollectScores(run, control);
    double times[2] = {};
    int compared[2] = {}, attempted[2] = {}, rejected[2] = {};
    std::array<int, 5> matchingFailures[2] = {};
    for (const engine::ComparisonRecord& record : run.comparisons) {
        control.Check();
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
        const std::vector<double> thresholds = Thresholds(groups, control);
        values[5] = EerBounds(groups, false, thresholds, control);
        values[6] = EerBounds(groups, true, thresholds, control);
        double best[4] = {2, 2, 2, 2};
        for (double threshold : thresholds) {
            control.Check();
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

static constexpr double Left = 90, Right = 800, Top = 60, Bottom = 390, DetBottom = 770;
static constexpr double PlotWidth = Right - Left, PlotHeight = Bottom - Top;
static const char* GenuineColor = "#4caf50";
static const char* ImpostorColor = "#ff5252";

static QString Plot(const QString& title, const QString& xLabel, const QString& yLabel,
                    const QString& content, bool square = false) {
    const QString width = QString::number(GraphWidth), height = QString::number(square ? GraphWidth : GraphHeight);
    const double bottom = square ? DetBottom : Bottom;
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + width
        + "\" height=\"" + height + "\" viewBox=\"0 0 " + width + ' ' + height + "\">"
        "<rect width=\"" + width + "\" height=\"" + height + "\" fill=\"white\"/>"
        "<g font-family=\"Arial\" fill=\"#222\">"
        "<text x=\"445\" y=\"28\" text-anchor=\"middle\" font-size=\"22\">" + title.toHtmlEscaped() + "</text>"
        "<text x=\"445\" y=\"" + Number(bottom + 64) + "\" text-anchor=\"middle\" font-size=\"15\">" + xLabel + "</text>"
        "<text transform=\"translate(23 " + Number((Top + bottom) / 2)
        + ") rotate(-90)\" text-anchor=\"middle\" font-size=\"15\">" + yLabel + "</text>" + content
        + "<rect x=\"90\" y=\"60\" width=\"710\" height=\"" + Number(bottom - Top)
        + "\" fill=\"none\" stroke=\"#222\" stroke-width=\"1.2\"/></g></svg>";
}

static QString EmptyPlot(const QString& message) {
    return "<text x=\"440\" y=\"145\" text-anchor=\"middle\" font-size=\"17\">" + message
        + "</text><text x=\"440\" y=\"174\" text-anchor=\"middle\" font-size=\"14\">"
          "Results will appear after evaluation.</text>";
}

static QString Ticks(double maximum, bool percentages = false) {
    QString svg = "<g font-size=\"12\">";
    for (int i = 0; i <= 5; ++i) {
        const QString x = Number(Left + PlotWidth * i / 5), y = Number(Bottom - PlotHeight * i / 5);
        svg += "<path d=\"M" + x + " 60V390M90 " + y + "H800\" stroke=\"#dedede\" fill=\"none\"/>"
            "<text x=\"" + x + "\" y=\"415\" text-anchor=\"middle\">" + QString::number(i / 5.0, 'f', 1) + "</text>"
            "<text x=\"80\" y=\"" + Number(Bottom - PlotHeight * i / 5 + 4) + "\" text-anchor=\"end\">"
            + Number(maximum * i / 5) + (percentages ? "%" : "") + "</text>";
    }
    return svg + "</g>";
}

static QString Line(const QString& points, const QString& color, const QString& id) {
    return "<polyline id=\"" + id + "\" fill=\"none\" stroke=\"" + color
        + "\" stroke-width=\"2\" points=\"" + points + "\"/>";
}

static QString ErrorRateCurves(const ScoreGroups& groups, bool showFmr, bool showFnmr, const std::vector<bool>& selected,
                               const engine::ExportControl& control) {
    const auto raw = [](double value) { return QString::number(value, 'g', engine::CsvPrecision); };
    const auto x = [](double threshold) { return Number(Left + PlotWidth * std::clamp(threshold, 0.0, 1.0)); };
    const auto point = [&x](double threshold, double rate) { return x(threshold) + ',' + Number(Bottom - PlotHeight * rate) + ' '; };
    double fmr, fnmr;
    Rates(groups, 0, 0, false, fmr, fnmr);
    QString fmrPoints = point(0, fmr), fnmrPoints = point(0, fnmr);
    QString samples = "<g display=\"none\">";
    const auto sample = [&raw, &samples](double threshold, const char* side, double fmr, double fnmr) {
        samples += "<g class=\"error-rate-point\" data-threshold=\"" + raw(threshold)
            + "\" data-side=\"" + side + "\" data-fmr=\"" + raw(fmr) + "\" data-fnmr=\"" + raw(fnmr) + "\"/>";
    };
    const std::vector<double> thresholds = Thresholds(groups, control);
    for (std::size_t i = 0; i + 1 < thresholds.size(); ++i) {
        control.Check();
        const double threshold = thresholds[i];
        Rates(groups, threshold, threshold, false, fmr, fnmr);
        fmrPoints += point(threshold, fmr); fnmrPoints += point(threshold, fnmr);
        sample(threshold, "at-score", fmr, fnmr);
        // score >= threshold accepts ties. The jump occurs immediately AFTER
        // the score, not at the next observed score; group all tied scores.
        const double after = std::nextafter(threshold, std::numeric_limits<double>::infinity());
        Rates(groups, after, after, false, fmr, fnmr);
        fmrPoints += point(threshold, fmr); fnmrPoints += point(threshold, fnmr);
        sample(after, "after-score", fmr, fnmr);
    }
    fmrPoints += point(1, fmr); fnmrPoints += point(1, fnmr);
    samples += "</g>";
    QString svg = Ticks(100, true) + "<g class=\"error-rate-axes\" data-threshold-min=\"0\" data-threshold-max=\"1\">" + samples;
    if (showFmr)
        svg += Line(fmrPoints, ImpostorColor, "fmr-curve");
    if (showFnmr)
        svg += Line(fnmrPoints, GenuineColor, "fnmr-curve");
    QString eerLabel;
    if (selected.size() > 5 && selected[5]) {
        const OperatingPoint eer = EerPoint(groups, false, thresholds, control);
        if (eer.available) {
            const double estimate = (eer.fmr + eer.fnmr) / 2;
            svg += "<path d=\"M" + x(eer.threshold) + " 60V390\" stroke=\"#777\" stroke-dasharray=\"5 4\"/>"
                "<circle class=\"eer-threshold-marker\" data-threshold=\"" + raw(eer.threshold)
                + "\" data-eer=\"" + raw(estimate) + "\" cx=\"" + x(eer.threshold) + "\" cy=\""
                + Number(Bottom - PlotHeight * estimate) + "\" r=\"4.5\" fill=\"#111\"/>";
            eerLabel = "Estimated EER = " + Value(100 * estimate) + "% @ t = " + Value(eer.threshold);
        }
    }
    svg += "</g><g font-size=\"12\">";
    if (showFmr) svg += "<path d=\"M100 45H125\" stroke=\"" + QString(ImpostorColor) + "\" stroke-width=\"2\"/><text x=\"135\" y=\"49\">FMR(t)</text>";
    if (showFnmr) svg += "<path d=\"M220 45H245\" stroke=\"" + QString(GenuineColor) + "\" stroke-width=\"2\"/><text x=\"255\" y=\"49\">FNMR(t)</text>";
    if (!eerLabel.isEmpty()) svg += "<circle cx=\"350\" cy=\"45\" r=\"4\" fill=\"#111\"/><text x=\"365\" y=\"49\">" + eerLabel + "</text>";
    svg += "</g><text x=\"90\" y=\"473\" font-size=\"10\">All observed cutoffs; score = threshold is accepted. EER marker shows the midpoint estimate.</text>";
    return svg;
}

static QString ScoreDistribution(const ScoreGroups& groups, const engine::ExportControl& control) {
    std::array<std::vector<int>, 2> bins = {std::vector<int>(HistogramBins), std::vector<int>(HistogramBins)};
    double maximum = 0;
    for (int group = 0; group < 2; ++group) {
        for (double score : groups.values[group]) {
            control.Check();
            ++bins[group][std::clamp(int(score * HistogramBins), 0, HistogramBins - 1)];
        }
        if (!groups.values[group].empty())
            for (int count : bins[group]) maximum = std::max(maximum, double(count) * HistogramBins / groups.values[group].size());
    }
    if (maximum == 0) return EmptyPlot("No scored comparison results available.");
    QString svg = Ticks(maximum);
    for (int group = 0; group < 2; ++group) {
        if (groups.values[group].empty()) continue;
        for (int bin = 0; bin < HistogramBins; ++bin) {
            control.Check();
            const int count = bins[group][bin];
            if (!count) continue;
            const double density = double(count) * HistogramBins / groups.values[group].size();
            const double height = PlotHeight * density / maximum;
            svg += "<rect class=\"score-bin\" data-group=\"" + QString(group == 0 ? "genuine" : "impostor")
                + "\" data-count=\"" + QString::number(count) + "\" data-bin=\"" + QString::number(bin)
                + "\" data-density=\"" + QString::number(density, 'g', engine::CsvPrecision)
                + "\" x=\"" + Number(Left + PlotWidth * bin / HistogramBins) + "\" y=\"" + Number(Bottom - height)
                + "\" width=\"" + Number(PlotWidth / HistogramBins) + "\" height=\"" + Number(height)
                + "\" fill=\"" + (group == 0 ? GenuineColor : ImpostorColor) + "\" fill-opacity=\"0.65\"/>";
        }
    }
    svg += "<g font-size=\"14\"><rect x=\"105\" y=\"74\" width=\"268\" height=\"64\" rx=\"4\" fill=\"white\" fill-opacity=\"0.93\" stroke=\"#ccc\"/>";
    for (int group = 0; group < 2; ++group) {
        const int y = 96 + 26 * group;
        svg += "<rect x=\"117\" y=\"" + QString::number(y - 12) + "\" width=\"25\" height=\"14\" fill=\""
            + (group == 0 ? GenuineColor : ImpostorColor) + "\" fill-opacity=\"0.65\"/><text x=\"153\" y=\"" + QString::number(y)
            + "\">" + (group == 0 ? "Genuine" : "Impostor") + " (n=" + QString::number(groups.values[group].size()) + ")</text>";
    }
    return svg + "</g><text x=\"90\" y=\"473\" font-size=\"10\">Successful scores only; each group is normalized to unit area.</text>";
}

// Match the assignment's logarithmic FMR/FNMR plot. Zero stays zero in the data;
// only its drawing coordinate is clipped to the explicitly labelled lower limit.
static QString DetCurve(const ScoreGroups& groups, const std::vector<bool>& selected,
                        const engine::ExportControl& control) {
    if (!groups.complete[0] || !groups.complete[1] || groups.total[0] == 0 || groups.total[1] == 0)
        return EmptyPlot("Complete both comparison groups to plot the DET curve.");
    const double smallest = std::min(DetMinimumRate, 1.0 / std::max(groups.total[0], groups.total[1]));
    const int lowestPower = int(std::floor(std::log10(smallest)));
    const double floor = std::pow(10.0, lowestPower);
    const auto position = [floor, lowestPower](double rate) {
        return (std::log10(std::max(floor, rate)) - lowestPower) / -lowestPower;
    };
    const auto x = [&position](double rate) { return Number(Left + PlotWidth * position(rate)); };
    const auto y = [&position](double rate) { return Number(DetBottom - PlotWidth * position(rate)); };
    const auto raw = [](double value) { return QString::number(value, 'g', engine::CsvPrecision); };
    QString svg = "<g class=\"det-log-axes\" data-min-rate=\"" + raw(floor) + "\" font-size=\"11\">";
    for (int power = lowestPower; power <= 0; ++power) {
        control.Check();
        const double rate = std::pow(10.0, power);
        svg += "<path d=\"M" + x(rate) + " 60V770M90 " + y(rate) + "H800\" stroke=\"#bbb\" stroke-dasharray=\"2 2\" fill=\"none\"/>"
            "<text x=\"" + Number(x(rate).toDouble() - 1) + "\" y=\"797\" text-anchor=\"end\">10</text>"
            "<text x=\"" + x(rate) + "\" y=\"792\" font-size=\"9\">" + QString::number(power) + "</text>"
            "<text x=\"68\" y=\"" + Number(y(rate).toDouble() + 4) + "\" text-anchor=\"end\">10</text>"
            "<text x=\"69\" y=\"" + Number(y(rate).toDouble() - 1) + "\" font-size=\"9\">" + QString::number(power) + "</text>";
        if (power < 0) for (int minor = 2; minor < 10; ++minor) {
            const double value = minor * rate;
            svg += "<path d=\"M" + x(value) + " 60V770M90 " + y(value)
                + "H800\" stroke=\"#e5e5e5\" stroke-dasharray=\"1 3\" fill=\"none\"/>";
        }
    }
    svg += "</g><text x=\"90\" y=\"46\" font-size=\"10\">Rates are fractions; zero rates are drawn at the axis floor (" + Number(floor) + ").</text>"
        "<path id=\"eer-equality-line\" d=\"M90 770L800 60\" stroke=\"#777\" stroke-width=\"1.2\" fill=\"none\"/>"
        "<text transform=\"translate(451 405) rotate(-45)\" font-size=\"11\" fill=\"#666\">FMR = FNMR</text>";
    for (int denominator : {10000, 1000, 100}) {
        const QString xx = x(1.0 / denominator);
        svg += "<path class=\"fmr-reference\" data-target=\"" + raw(1.0 / denominator) + "\" d=\"M" + xx
            + " 60V770\" stroke=\"#555\" stroke-width=\"1\" fill=\"none\"/>"
            "<text transform=\"translate(" + Number(xx.toDouble() - 6) + " 142) rotate(-90)\" font-size=\"10\">FMR"
            + QString::number(denominator) + "</text>";
    }
    const std::vector<double> thresholds = Thresholds(groups, control);
    std::vector<OperatingPoint> curve;
    QString points, samples;
    for (double threshold : thresholds) {
        control.Check();
        double fmr, fnmr;
        Rates(groups, threshold, threshold, false, fmr, fnmr);
        curve.push_back({threshold, fmr, fnmr, true});
        points += x(fmr) + ',' + y(fnmr) + ' ';
        samples += "<circle class=\"det-point\" data-threshold=\"" + raw(threshold)
            + "\" data-fmr=\"" + raw(fmr) + "\" data-fnmr=\"" + raw(fnmr) + "\" cx=\"" + x(fmr)
            + "\" cy=\"" + y(fnmr) + "\" r=\"0.8\" fill=\"#1148c8\"/>";
    }
    svg += "<polyline id=\"det-curve\" data-threshold-count=\"" + QString::number(curve.size())
        + "\" fill=\"none\" stroke=\"#1148c8\" stroke-width=\"2\" points=\"" + points + "\"/>" + samples;
    std::vector<std::pair<double, double>> labels;
    const auto marker = [&](const OperatingPoint& point, const QString& label, bool estimated = false) {
        if (!point.available) return;
        const double xx = x(point.fmr).toDouble(), yy = y(point.fnmr).toDouble();
        const double labelX = std::clamp(xx + 12, Left + 12, Right - 245);
        double labelY = std::clamp(yy - 12, Top + 20, DetBottom - 12);
        for (const auto& previous : labels)
            if (std::abs(labelX - previous.first) < 245 && std::abs(labelY - previous.second) < 19)
                labelY = previous.second + (yy < Top + 100 ? 21 : -21);
        labels.push_back({labelX, labelY});
        svg += "<rect class=\"det-marker\" data-estimated=\"" + QString(estimated ? "true" : "false")
            + "\" data-fmr=\"" + raw(point.fmr) + "\" data-fnmr=\"" + raw(point.fnmr)
            + "\" x=\"" + Number(xx - 4) + "\" y=\"" + Number(yy - 4)
            + "\" width=\"8\" height=\"8\" fill=\"#1148c8\" stroke=\"#111\"/>"
            "<text x=\"" + Number(labelX) + "\" y=\"" + Number(labelY) + "\" font-size=\"11\">" + label + "</text>";
    };
    if (selected.size() > 5 && selected[5]) {
        const OperatingPoint point = EerPoint(groups, false, thresholds, control);
        const double estimate = (point.fmr + point.fnmr) / 2;
        marker({point.threshold, estimate, estimate, point.available}, "EER est. = " + Value(100 * estimate) + "%", true);
    }
    for (int denominator : {10000, 1000, 100}) {
        const double target = 1.0 / denominator;
        OperatingPoint best;
        for (const OperatingPoint& point : curve) {
            control.Check();
            if (point.fmr <= target + engine::MetricTolerance && (!best.available || point.fnmr < best.fnmr)) best = point;
        }
        marker(best, "FMR" + QString::number(denominator) + ": FNMR = " + Value(100 * best.fnmr) + "%");
    }
    return svg;
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
    if (file == "score_distribution.svg") return "Score Distribution";
    if (file == "error_tradeoff.svg") return "DET Curve";
    if (DerivedMetricSelected(selected) || (selected.size() > 3 && selected[3] && selected.size() > 4 && selected[4]))
        return "FMR(t) and FNMR(t)";
    return (selected.size() > 3 && selected[3]) ? "FMR(t)" : "FNMR(t)";
}

static bool WriteReportFile(const QString& path, const QString& text, const engine::ExportControl& control) {
    control.Check();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = text.toUtf8();
    for (qint64 offset = 0; offset < bytes.size();) {
        control.Check();
        const qint64 size = std::min<qint64>(256 * 1024, bytes.size() - offset);
        if (file.write(bytes.constData() + offset, size) != size) return false;
        offset += size;
    }
    file.close();
    if (file.error() != QFileDevice::NoError) return false;
    control.Step();
    return true;
}


bool WriteGraphs(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder,
                 const engine::ExportControl& control) {
    control.Check();
    const QStringList files = GraphFiles(selected);
    if (files.isEmpty()) return true;
    const bool derived = DerivedMetricSelected(selected);
    const bool showFmr = derived || (selected.size() > 3 && selected[3]);
    const bool showFnmr = derived || (selected.size() > 4 && selected[4]);
    ScoreGroups groups = CollectScores(run, control);
    bool first = true;
    for (const engine::ComparisonRecord& record : run.comparisons) {
        control.Check();
        if (!record.executed || !record.result.success) continue;
        const double score = record.result.score;
        if (!std::isfinite(score)) {
            groups.complete[record.genuine ? 0 : 1] = false;
            continue;
        }
        first = false;
    }
    QString distribution, errors, tradeoff;
    if (first)
        distribution = EmptyPlot("No scored comparison results available.");
    else distribution = ScoreDistribution(groups, control);
    if (first || (showFmr && (!groups.complete[1] || groups.total[1] == 0)) || (showFnmr && (!groups.complete[0] || groups.total[0] == 0))) {
        errors = tradeoff = EmptyPlot("Complete the required comparison groups to plot error rates.");
    } else {
        errors = ErrorRateCurves(groups, showFmr, showFnmr, selected, control);
        tradeoff = DetCurve(groups, selected, control);
    }
    for (const QString& file : files) {
        QString svg;
        QString title = GraphTitle(file, selected);
        const QString database = QDir(QString::fromStdString(run.database)).dirName();
        if (!run.database.empty() && !database.isEmpty()) title += " - " + database;
        if (file == "score_distribution.svg")
            svg = Plot(title, "Similarity score", "Probability density", distribution);
        else if (file == "error_rates.svg")
            svg = Plot(title, "Threshold", "Error rate", errors);
        else
            svg = Plot(title, "FMR", "FNMR", tradeoff, true);
        if (!WriteReportFile(folder + "/" + file, svg, control)) return false;
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

int ReportSteps(const std::vector<bool>& selected) {
    return 4 + GraphFiles(selected).size(); // Metrics, three report files, selected SVGs.
}

bool Save(const engine::EvaluationRun& run, const std::vector<bool>& selected, const QString& folder,
          const engine::ExportControl& control) {
    const QStringList values = CalculateMetrics(run, control);
    control.Step();
    QString csv = "metric,value\n";
    QString html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Fingerprint performance report</title><style>"
        "body{font:16px Arial,sans-serif;max-width:900px;margin:32px auto;padding:0 20px;color:#222}"
        "table{border-collapse:collapse;width:100%}th,td{border:1px solid #bbb;padding:7px;text-align:left}"
        "pre{white-space:pre-wrap;overflow-wrap:anywhere}img{max-width:100%;height:auto}"
        "</style></head><body><h1>Fingerprint performance report</h1>";
    bool executed = false;
    for (const engine::ComparisonRecord& item : run.comparisons) {
        control.Check();
        if (item.executed) { executed = true; break; }
    }
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
        control.Check();
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
            + QString::number(GraphWidth) + "\" height=\"" + QString::number(file == "error_tradeoff.svg" ? GraphWidth : GraphHeight) + "\" alt=\""
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
    return WriteReportFile(destination.filePath("metrics.csv"), csv, control)
        && WriteReportFile(destination.filePath("report.html"), html, control)
        && WriteReportFile(destination.filePath("report.rtf"), word, control) && WriteGraphs(run, selected, folder, control);
}

} // namespace gui
