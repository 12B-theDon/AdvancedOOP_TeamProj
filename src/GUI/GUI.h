#pragma once

#include <QVector>
#include <QWizard>
#include "Engine.h"
#include "report.h"
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

// Seven screens: DB, enrollment, templates, type, rules, run and results.
class Window : public QWizard {
public:
    Window();
    // Empty database_id selects external-DB defaults.
    void LoadDatabase(const QString& folder, const QString& database_id = QString());
    // Create a new run folder; retain all raw results.
    bool SaveResults(const QString& folder);
    // Borrow the backend; caller owns its lifetime.
    void SetAlgorithm(Algorithm* backend);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void done(int result) override;
    bool validateCurrentPage() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Decoded image identity and source file.
    struct Image {
        QString path;
        int finger = 0;
        int capture = 0;
        int source_id = -1;
    };
    // Rule widgets and the corresponding Engine plan/results.
    struct ComparisonControls {
        QWidget* panel;
        QComboBox* rule;
        QSpinBox* count;
        QSpinBox* seed;
        QLineEdit* captures;
        QLineEdit* file;
        QLabel* info;
        QWidget* resultPanel;
        QDoubleSpinBox* threshold; // [0,1] Similarity cutoff; changes do not rerun inference.
        QLabel* resultInfo;
        QTableWidget* table;
        std::vector<SimilarityResult> results;
        std::vector<ComparisonPair> pairs; // Engine plan in displayed order.
        bool valid = true;
    };
    // Current DB, backend and enrollment cache.
    QVector<Image> images_;
    Engine engine_;
    Algorithm* algorithm_ = nullptr;
    std::vector<EnrollmentTemplates> enrollmentTemplates_;
    bool templatesReady_ = false;
    QString databasePath_;
    QString databaseId_;
    QPushButton* selectedDatabase_ = nullptr;
    // Model and template widgets.
    QLineEdit* weightPath_;
    std::vector<QCheckBox*> modelChoices_;
    bool changingModels_ = false;
    QTableWidget* modelTable_;
    QTableWidget* templateTable_;
    QPushButton* createTemplates_;
    QLabel* inferenceInfo_;
    QLabel* databaseInfo_;
    QString databaseSummary_ = GuiConfig::EmptyDatabase;
    // Enrollment, comparison and report widgets.
    QLabel* preview_;
    QLabel* enrollmentInfo_;
    QListWidget* imageList_;
    ComparisonControls genuine_;
    ComparisonControls impostor_;
    QComboBox* comparisonType_;
    QPushButton* runComparisons_;
    QLabel* executionInfo_;
    QTabWidget* resultTabs_;
    bool comparisonsExecuted_ = false;
    QTableWidget* metrics_;
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
    bool ReadPairCsv(const QString& path, bool genuine, std::vector<ComparisonPair>& pairs, QString& error);
    EvaluationRun Snapshot() const; // Copy data for Engine export and reporting.
    std::vector<bool> SelectedMetrics() const;
    void UpdateMetrics();
    void ShowImage(int row);
    void ReloadModels();
    void ClearTemplates();
    void CreateEnrollmentTemplates();
    void RunComparisons(ComparisonControls& controls);
    void ApplyThreshold(ComparisonControls& controls); // Classify cached best scores.
};
