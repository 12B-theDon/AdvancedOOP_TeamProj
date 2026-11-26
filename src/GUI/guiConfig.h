#pragma once

#include "../Engine/engineConfig.h"
#include <QStringList>
#include <vector>

// UI text, default selections and display sizes.
namespace gui {
inline constexpr int WindowWidth = 900; // [px]
inline constexpr int WindowHeight = 720; // [px]
inline constexpr int PreviewSize = 340; // [px] Square image preview.
inline constexpr int StepWidth = 90; // [px]
inline constexpr int StepHeight = 20; // [px]
inline constexpr int StepRight = 20; // [px]
inline constexpr int StepTop = 14; // [px]
inline constexpr int ModelTableHeight = 165; // [px] Four model rows.
inline constexpr int OptionsHeight = 36; // [px]
inline constexpr int MetricColumns = 3;
inline constexpr int MaxRandomCount = 1000000; // [pairs/enrollment]
inline constexpr int ScorePrecision = 6; // [decimal places] GUI only; raw CSV keeps full precision.
inline constexpr int TimePrecision = 3; // [decimal places]
inline constexpr int ThresholdPrecision = 4; // [decimal places]
inline constexpr double ThresholdMinimum = 0.0; // [normalized score]
inline constexpr double ThresholdMaximum = 1.0; // [normalized score]
inline constexpr double ThresholdStep = 0.01; // [normalized score]
inline constexpr const char* PassColor = "#e5f3e8";
inline constexpr const char* MissColor = "#fbe5e5";
inline constexpr const char* RejectColor = "#e5effa";
inline constexpr const char* FalseAcceptColor = "#fff0d9";
inline constexpr const char* ErrorColor = "#f2f2f2";
inline constexpr const char* OtherDatabaseScoreColor = "#888888";
inline constexpr int GraphWidth = 840; // [px]
inline constexpr int GraphHeight = 480; // [px] DET uses a square canvas.
inline constexpr int HistogramBins = 100;
inline constexpr double DetMinimumRate = 1e-5; // [fraction] Log-axis floor; expands for larger evaluations.
inline constexpr int GraphPrecision = 5; // Significant digits in SVG.
inline constexpr int MetricPrecision = 6; // Significant digits in metric values.
inline constexpr const char* WindowTitle = "Fingerprint Evaluator";
inline constexpr const char* EmptyDatabase = "No DB selected.";
inline constexpr const char* DefaultCaptures = "1";
inline constexpr const char* StepStyle = "font-size:11px;color:#666;background:transparent;";
// Action buttons turn blue only on hover; checkable DB buttons keep their own style.
inline constexpr const char* ActionButtonStyle =
    "QPushButton[checkable=\"false\"] { background:#ececec; color:#111; border:1px solid #d8d8d8; border-radius:6px; padding:6px 12px; }"
    "QPushButton[checkable=\"false\"]:hover:enabled { background:#1677df; color:white; border-color:#1677df; }"
    "QPushButton[checkable=\"false\"]:disabled { background:#ececec; color:#999; border-color:#dedede; }";
// Only the checked DB button uses the selection color.
inline constexpr const char* DatabaseButtonStyle =
    "QPushButton { background:#ececec; color:#111; border:1px solid #d8d8d8; border-radius:6px; padding:6px 12px; }"
    "QPushButton:hover { background:#dfe7ef; }"
    "QPushButton:checked { background:#1677df; color:white; border-color:#1677df; }";
inline constexpr const char* ResultTabStyle =
    "QTabBar::tab { background:#ececec; color:#111; border:1px solid #d8d8d8; padding:6px 12px; }"
    "QTabBar::tab:selected { background:white; }";
// Default action labels and screen guidance.
inline constexpr const char* Next = "Next";
inline constexpr const char* Back = "Back";
inline constexpr const char* Cancel = "Cancel";
inline constexpr const char* Close = "Close";
inline constexpr const char* SelectDatabaseHint = "Click a DB to select it.";
inline constexpr const char* ExternalDatabase = "Load external DB...";
inline constexpr const char* ExternalDatabaseInfo = "External DB: load TIFFs with one of the three mapped FVC resolutions.";
inline constexpr const char* ExternalFolderDialog = "Select an external DB folder";
inline constexpr const char* ImagePrompt = "Select an image.";
inline constexpr const char* EnrollmentInitial = "Enrolled images: 0";
inline constexpr const char* EnrollmentHint = "Each selected image will produce a separate enrollment template.";
inline constexpr const char* BrowseWeights = "Select K-NBTC algorithm DLL...";
inline constexpr const char* WeightsFolderDialog = "Select K-NBTC algorithm DLL";
inline constexpr const char* CreateTemplates = "Create enrollment templates";
inline constexpr const char* ModelHint = "Select a K-NBTC DLL, set workers and timeout, then click Load selected DLL with these settings. First inference per worker allows at least 120 seconds.";
inline constexpr const char* GenuineHint = "Compare each enrollment with other images of the same finger.";
inline constexpr const char* ImpostorHint = "Compare each enrollment with images of other fingers.";
inline constexpr const char* RuleLabel = "Comparison selection:";
inline constexpr const char* AllGenuineHint = "Use every other image of the same finger. The enrollment image itself is excluded.";
inline constexpr const char* AllImpostorHint = "Use every image of other fingers. Choose 'One image per finger' to compare fewer images.";
inline constexpr const char* FirstCaptureHint = "Use the lowest available capture number per finger, excluding the enrollment image.";
inline constexpr const char* CapturesHint = "Capture numbers, separated by commas (for example: 1, 3, 5)";
inline constexpr const char* CountLabel = "Up to this many pairs per enrolled image:";
inline constexpr const char* SeedLabel = "Seed:";
inline constexpr const char* CsvHint = "CSV columns: enrollment,attempt (optional: type)";
inline constexpr const char* BrowsePairs = "Load pairs CSV...";
inline constexpr const char* PairsInitial = "0 pairs.";
inline constexpr const char* ComparisonTypeHint = "Choose which comparisons to run.";
inline constexpr const char* RulesHint = "Choose a rule for each selected comparison type. Results are shown after execution.";
inline constexpr const char* RunComparisons = "Run selected comparisons";
inline constexpr const char* ExecutionHint = "Review the selected comparisons, then click Run.";
inline constexpr const char* ExecutionComplete = "Comparisons finished. Results are ready.";
inline constexpr const char* NoComparisonType = "Select a comparison type.";
inline constexpr const char* NoComparisonPairs = "No eligible pairs. Change the rule or enrollment images.";
inline constexpr const char* ResultsHint = "Select a tab to view comparison results or metrics.";
inline constexpr const char* ThresholdLabel = "Similarity threshold (0-1):";
inline constexpr const char* GenuineThresholdHint = "Same finger: DLL score >= threshold = PASS; otherwise MISS.";
inline constexpr const char* ImpostorThresholdHint = "Different fingers: DLL score < threshold = REJ; otherwise FA.";
inline constexpr const char* ScoreHint = "DLL similarities use the current threshold. Errors retain their failure category and DLL error message.";
inline constexpr const char* PairDialog = "Load comparison pairs";
inline constexpr const char* PairFilter = "CSV files (*.csv)";
inline constexpr const char* SaveAll = "Save all (CSV + HTML + Word)";
inline constexpr const char* OutputFolderDialog = "Select an output folder";
inline constexpr const char* SavedTitle = "Saved";
inline constexpr const char* SavedMessage = "All results were saved in a new run folder.";
inline constexpr const char* SaveFailureTitle = "Save failed";
inline constexpr const char* SaveFailureMessage = "Select enrollment images, correct any rule errors, and choose a writable output folder.";
inline constexpr const char* MetricHint = "Checked metrics appear in the report. Unavailable metrics are disabled; hover for details. CSV files keep all metrics and raw results.";
inline constexpr const char* ExitTitle = "Exit application";
inline constexpr const char* ExitQuestion = "Are you sure you want to exit?";
inline constexpr const char* StopAndExitQuestion = "Stop all workers and exit? Unfinished results will be discarded.";
inline const QStringList PageTitles = {"1. Select a fingerprint DB", "2. Select enrollment images",
    "3. Create enrollment templates", "4. Select comparison type", "5. Select comparison rules",
    "6. Run comparisons", "7. Results and report"};
inline const QStringList ComparisonTypes = {"Select...", "Genuine", "Impostor", "Both"};
inline const QStringList ResultTabs = {"Genuine", "Impostor", "Metrics and report"};
inline const QStringList Resolutions = {"448 × 478 px", "300 × 300 px", "640 × 480 px"};
inline const QStringList EnrollmentModes = {"Single enrollment", "Multiple enrollment (Ctrl / Shift to select)"};
inline const QStringList EnrollmentRules = {"All images", "Random N images", "Random images per finger",
    "Selected capture numbers", "Image list from CSV"};
inline const QStringList ComparisonRules = {"All eligible images", "One image per finger (first capture)", "Selected capture numbers",
    "Random images per enrollment", "Comparison pairs from CSV"};
inline const QStringList ModelHeaders = {"Algorithm", "DLL file", "Initialization"};
inline const QStringList TemplateHeaders = {"Enrollment image", "Algorithm", "Status", "Size (bytes)", "Error", "Load time (ms)"};
inline const QStringList ComparisonHeaders = {"#", "Enrollment", "Comparison", "Result", "Time (ms)"};
inline const QStringList MetricHeaders = {"Metric", "Value"};
inline const QStringList MetricNames = {"FTE (%)", "REJ genuine (%)", "REJ impostor (%)", "FMR (%)", "FNMR (%)",
    "EER (%)", "EER* (%)", "ZeroFMR (%)", "ZeroFNMR (%)", "FMR100 (%)", "FMR1000 (%)",
    "Enrollment time (ms)", "Matching time (ms)", "Genuine time (ms)", "Impostor time (ms)",
    "Mean template size (bytes)", "Max template size (bytes)"};
inline const std::vector<int> DefaultMetrics = {0, 5, 12}; // FTE, EER, matching time.
} // namespace gui
