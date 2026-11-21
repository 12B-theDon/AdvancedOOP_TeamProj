#pragma once

#include <QWizard>
#include <QString>
#include <vector>
#include "Engine.h"
#include "kisaDll.h"
#include <functional>
#include <memory>
#include "guiConfig.h"

class QLabel;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QResizeEvent;
class QCheckBox;
class QTabWidget;

namespace gui {

// Seven screens: DB, enrollment, templates, type, rules, run and results.
class Window : public QWizard {
public:
    Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    // Empty database_id selects external-DB defaults.
    void LoadDatabase(const QString& folder, const QString& database_id = QString());
    // Create a new run folder; retain all raw results.
    bool SaveResults(const QString& folder);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void done(int result) override;
    bool validateCurrentPage() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Rule widgets and the corresponding Engine plan/results.
    struct ComparisonControls {
        QWidget* panel = nullptr;
        QComboBox* rule = nullptr;
        QSpinBox* count = nullptr;
        QSpinBox* seed = nullptr;
        QLineEdit* captures = nullptr;
        QLineEdit* file = nullptr;
        QLabel* info = nullptr;
        QWidget* resultPanel = nullptr;
        QDoubleSpinBox* threshold = nullptr; // [0,1] Similarity cutoff; changes do not rerun inference.
        QLabel* resultInfo = nullptr;
        QTableWidget* table = nullptr;
        std::vector<engine::SimilarityResult> results;
        std::vector<engine::ComparisonPair> pairs; // Engine plan in displayed order.
        bool valid = true;
    };
    // Current DB, backend and enrollment cache.
    std::vector<engine::ImageInfo> images_;
    engine::Engine engine_;
    std::unique_ptr<engine::KisaDllAlgorithm> algorithm_;
    QSpinBox* workerCount_ = nullptr;
    QSpinBox* timeout_ = nullptr;
    QString dllHash_;
    bool busy_ = false;
    bool RunWithProgress(const QString& title, const std::function<void()>& work);
    bool AlgorithmUnchanged();
    std::vector<engine::EnrollmentTemplates> enrollmentTemplates_;
    bool templatesReady_ = false;
    QString databasePath_;
    QString databaseId_;
    QPushButton* selectedDatabase_ = nullptr;
    // Qt parents own widgets; these pointers only refer to them.
    QLineEdit* dllPath_ = nullptr;
    QTableWidget* modelTable_ = nullptr;
    QTableWidget* templateTable_ = nullptr;
    QPushButton* createTemplates_ = nullptr;
    QLabel* inferenceInfo_ = nullptr;
    QLabel* calibrationInfo_ = nullptr;
    QLabel* resultCalibrationInfo_ = nullptr;
    QLabel* databaseInfo_ = nullptr;
    QString databaseSummary_ = EmptyDatabase;
    // Enrollment, comparison and report widgets.
    QLabel* preview_ = nullptr;
    QLabel* enrollmentInfo_ = nullptr;
    QListWidget* imageList_ = nullptr;
    ComparisonControls genuine_;
    ComparisonControls impostor_;
    QComboBox* comparisonType_ = nullptr;
    QPushButton* runComparisons_ = nullptr;
    QLabel* executionInfo_ = nullptr;
    QTabWidget* resultTabs_ = nullptr;
    bool comparisonsExecuted_ = false;
    QTableWidget* metrics_ = nullptr;
    std::vector<QCheckBox*> metricChoices_;
    QLabel* stepIndicator_ = nullptr;

    QWizardPage* DatabasePage();
    QWizardPage* EnrollmentPage();
    QWizardPage* TemplatePage();
    QWizardPage* ComparisonTypePage();
    QWizardPage* ComparisonRulesPage();
    QWidget* ComparisonPanel(bool genuine);
    QWizardPage* ExecutionPage();
    QWizardPage* ResultsPage();
    bool ComparisonSelected(bool genuine) const;
    void UpdateWorkflow(); // Gate pages and show only selected comparison types.
    void RunSelectedComparisons();
    void UpdateEnrollment();
    void UpdateComparisons(ComparisonControls& controls, bool genuine);
    bool ReadPairCsv(const QString& path, bool genuine, std::vector<engine::ComparisonPair>& pairs, QString& error);
    engine::EvaluationRun Snapshot() const; // Copy data for Engine export and reporting.
    std::vector<bool> SelectedMetrics() const;
    void UpdateMetrics();
    void ShowImage(int row);
    void ReloadModels();
    void ClearTemplates();
    void CreateEnrollmentTemplates();
    void DisplayComparisons(ComparisonControls& controls);
    void ApplyThreshold(ComparisonControls& controls); // Classify cached best scores.
};

} // namespace gui
