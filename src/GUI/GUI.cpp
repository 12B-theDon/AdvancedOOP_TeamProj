#include "GUI.h"
#include "report.h"
#include <QButtonGroup>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <limits>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QProgressDialog>
#include <QTimer>
#include <QTemporaryDir>
#include <exception>
#include <future>
#include <chrono>
#include <utility>

namespace gui {

// QWizard enables Next only when the current step is ready.
class WorkflowPage : public QWizardPage {
public:
    explicit WorkflowPage(QWidget* parent) : QWizardPage(parent) {}
    bool isComplete() const override { return ready_; }
    void SetReady(bool ready) {
        if (ready_ == ready) return;
        ready_ = ready;
        emit completeChanged();
    }
private:
    bool ready_ = false;
};

static QTableWidget* MakeTable(const QStringList& headers) {
    QTableWidget* table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    return table;
}

static void AddRow(QTableWidget* table, const QStringList& values) {
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < values.size(); ++column) {
        QTableWidgetItem* item = new QTableWidgetItem(values[column]);
        item->setToolTip(values[column]);
        item->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, column, item);
    }
}

static QString InitialAlgorithmDirectory(const QString& executableFolder) {
    QDir folder(executableFolder);
    const QString sibling = folder.filePath("../FingerprintAlgorithm");
    if (QDir(sibling).exists()) return QDir::cleanPath(sibling);
    do {
        if (QFileInfo::exists(folder.filePath("CMakeLists.txt"))
            && QFileInfo::exists(folder.filePath("src/main.cpp")))
            return folder.filePath("dist/FingerprintAlgorithm");
    } while (folder.cdUp());
    return executableFolder;
}

static const char* DecisionColor(const QString& decision) {
    if (decision == "PASS") return PassColor;
    if (decision == "MISS") return MissColor;
    if (decision == "REJ") return RejectColor;
    if (decision == "FA") return FalseAcceptColor;
    return ErrorColor;
}

// One CSV record per line; doubled quotes escape a quote.
static bool CsvFields(const QString& line, QStringList& fields) {
    fields.clear();
    QString field;
    bool quoted = false, closed = false, quotedField = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += c;
                    ++i;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else
                field += c;
        } else if (c == ',') {
            fields.append(quotedField ? field : field.trimmed());
            field.clear();
            closed = quotedField = false;
        } else if (closed) {
            if (!c.isSpace())
                return false;
        } else if (c == '"') {
            if (!field.trimmed().isEmpty())
                return false;
            field.clear();
            quoted = quotedField = true;
        } else
            field += c;
    }
    if (quoted)
        return false;
    fields.append(quotedField ? field : field.trimmed());
    return true;
}

Window::Window() {
    setWindowTitle(WindowTitle);
    setStyleSheet(ActionButtonStyle);
    stepIndicator_ = new QLabel(QString("Step 1 / %1").arg(PageTitles.size()), this);
    stepIndicator_->setObjectName("stepProgress");
    stepIndicator_->setFixedSize(StepWidth, StepHeight);
    stepIndicator_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stepIndicator_->setStyleSheet(StepStyle);
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setButtonText(QWizard::NextButton, Next);
    setButtonText(QWizard::BackButton, Back);
    setButtonText(QWizard::CancelButton, Cancel);
    setOption(QWizard::NoCancelButtonOnLastPage);
    setButtonText(QWizard::FinishButton, Close);
    addPage(DatabasePage());
    addPage(EnrollmentPage());
    addPage(TemplatePage());
    addPage(ComparisonTypePage());
    addPage(ComparisonRulesPage());
    addPage(ExecutionPage());
    addPage(ResultsPage());
    for (int id : pageIds()) {
        for (QPushButton* button : page(id)->findChildren<QPushButton*>()) {
            button->setAutoDefault(false);
            button->setDefault(false);
        }
    }
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id >= 0)
            stepIndicator_->setText(QString("Step %1 / %2").arg(id + 1).arg(pageIds().size()));
        stepIndicator_->raise();
    });
    connect(imageList_, &QListWidget::itemSelectionChanged, this, [this] {
        ClearTemplates();
        UpdateEnrollment();
    });
    connect(imageList_, &QListWidget::currentRowChanged, this, &Window::ShowImage);
    connect(comparisonType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { UpdateEnrollment(); });
    UpdateEnrollment();
    resize(WindowWidth, WindowHeight);
}

void Window::resizeEvent(QResizeEvent* event) {
    QWizard::resizeEvent(event);
    if (stepIndicator_) {
        stepIndicator_->move(width() - stepIndicator_->width() - StepRight, StepTop);
        stepIndicator_->raise();
    }
}

void Window::done(int result) {
    if (exitQuestionOpen_) return;
    if (closeRequested_) {
        if (!busy_) QWizard::done(closeResult_);
        return;
    }
    exitQuestionOpen_ = true;
    const QMessageBox::StandardButton answer = QMessageBox::question(this, ExitTitle, busy_ ? StopAndExitQuestion : ExitQuestion,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    exitQuestionOpen_ = false;
    if (answer != QMessageBox::Yes) return;
    closeRequested_ = true;
    closeResult_ = result;
    if (busy_) engine_.Cancel();
    else QWizard::done(result);
}

bool Window::validateCurrentPage() {
    if ((currentId() == 0 && images_.empty())
        || (currentId() == 1 && imageList_->selectedItems().isEmpty())) {
        QMessageBox::information(this, "Selection required", "Select a DB and at least one enrollment image.");
        return false;
    }
    if (currentId() == 2 && !templatesReady_) return false;
    if (currentId() == 3 && comparisonType_->currentIndex() == 0) return false;
    if (currentId() == 4 && !runComparisons_->isEnabled()) return false;
    if (currentId() == 5 && !comparisonsExecuted_) return false;
    return true;
}

bool Window::eventFilter(QObject* watched, QEvent* event) {
    if (!watched->property("info").isValid())
        return QWizard::eventFilter(watched, event);
    if (event->type() == QEvent::Enter)
        databaseInfo_->setText(watched->property("info").toString());
    else if (event->type() == QEvent::Leave)
        databaseInfo_->setText(databaseSummary_);
    return QWizard::eventFilter(watched, event);
}

QWizardPage* Window::DatabasePage() {
    QWizardPage* page = new QWizardPage(this);
    page->setTitle(PageTitles[0]);
    page->setStyleSheet(DatabaseButtonStyle);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(SelectDatabaseHint));
    QButtonGroup* group = new QButtonGroup(page);
    group->setExclusive(true);
    for (int i = 0; i < int(engine::DatabaseIds.size()); ++i) {
        const QString name = QString::fromStdString(engine::DatabaseIds[i]);
        QPushButton* button = new QPushButton(name);
        button->setObjectName(name);
        button->setProperty("info", name + ": " + Resolutions[i] + " (width x height)");
        button->installEventFilter(this);
        button->setCheckable(true);
        group->addButton(button);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name, button] {
            selectedDatabase_ = button;
            const QString relative = QString::fromUtf8(engine::ImagesFolder) + "/" + name;
            const QString app = QCoreApplication::applicationDirPath();
            const QStringList roots = {QDir::currentPath(), app, QDir(app).filePath(".."), QDir(app).filePath("../..")};
            QString folder;
            for (const QString& root : roots) {
                const QString candidate = QDir(root).filePath(relative);
                if (QDir(candidate).exists()) { folder = candidate; break; }
            }
            if (folder.isEmpty()) folder = QFileDialog::getExistingDirectory(this, "Locate " + name);
            if (!folder.isEmpty()) LoadDatabase(folder, name);
        });
    }
    QPushButton* external = new QPushButton(ExternalDatabase);
    external->setObjectName("externalDatabase");
    external->setProperty("info", ExternalDatabaseInfo);
    external->installEventFilter(this);
    external->setCheckable(true);
    group->addButton(external);
    layout->addWidget(external);
    connect(external, &QPushButton::clicked, this, [this, group, external] {
        const QString folder = QFileDialog::getExistingDirectory(this, ExternalFolderDialog);
        if (folder.isEmpty()) {
            if (selectedDatabase_)
                selectedDatabase_->setChecked(true);
            else {
                group->setExclusive(false);
                external->setChecked(false);
                group->setExclusive(true);
            }
            return;
        }
        selectedDatabase_ = external;
        LoadDatabase(folder);
    });
    databaseInfo_ = new QLabel(databaseSummary_);
    databaseInfo_->setObjectName("databaseInfo");
    databaseInfo_->setWordWrap(true);
    layout->addWidget(databaseInfo_);
    layout->addStretch();
    return page;
}

QWizardPage* Window::EnrollmentPage() {
    QWizardPage* page = new QWizardPage(this);
    page->setTitle(PageTitles[1]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    QComboBox* mode = new QComboBox;
    mode->setObjectName("enrollmentMode");
    mode->addItems(EnrollmentModes);
    imageList_ = new QListWidget;
    imageList_->setObjectName("enrollmentImages");
    preview_ = new QLabel(ImagePrompt);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFixedSize(PreviewSize, PreviewSize);
    enrollmentInfo_ = new QLabel(EnrollmentInitial);
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(imageList_);
    row->addWidget(preview_);
    layout->addWidget(mode);
    layout->addLayout(row);
    layout->addWidget(enrollmentInfo_);
    layout->addWidget(new QLabel(EnrollmentHint));
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const int row = imageList_->currentRow();
        imageList_->clearSelection();
        imageList_->setSelectionMode(index == 0 ? QAbstractItemView::SingleSelection
                                                : QAbstractItemView::ExtendedSelection);
        if (row >= 0)
            imageList_->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
    });
    return page;
}

QWizardPage* Window::TemplatePage() {
    WorkflowPage* page = new WorkflowPage(this);
    page->setTitle(PageTitles[2]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    dllPath_ = new QLineEdit;
    dllPath_->setObjectName("algorithmDll");
    dllPath_->setReadOnly(true);
    dllPath_->setPlaceholderText("Select K-NBTC algorithm DLL...");
    QPushButton* browse = new QPushButton(BrowseWeights);
    browse->setObjectName("browseAlgorithm");
    createTemplates_ = new QPushButton(CreateTemplates);
    createTemplates_->setObjectName("createTemplates");
    createTemplates_->setEnabled(false);
    modelTable_ = MakeTable(ModelHeaders);
    modelTable_->setObjectName("selectedModels");
    modelTable_->setMaximumHeight(ModelTableHeight);
    templateTable_ = MakeTable(TemplateHeaders);
    templateTable_->setObjectName("enrollmentTemplates");
    templateTable_->horizontalHeaderItem(5)->setToolTip(
        "Template generation elapsed time, including internal model initialization and any worker restart during the request.");
    inferenceInfo_ = new QLabel(ModelHint);
    inferenceInfo_->setObjectName("inferenceInfo");
    inferenceInfo_->setTextFormat(Qt::PlainText);
    inferenceInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inferenceInfo_->setWordWrap(true);
    calibrationInfo_ = new QLabel("Threshold starts at 0.5; adjust in Results for the selected DLL.");
    calibrationInfo_->setObjectName("calibrationInfo");
    calibrationInfo_->setWordWrap(true);
    layout->addWidget(dllPath_);
    layout->addWidget(browse);
    QLabel* dllHelp = new QLabel("Select a K-NBTC algorithm DLL (any filename), configure workers and timeout, then click Load.");
    dllHelp->setWordWrap(true);
    layout->addWidget(dllHelp);
    workerCount_ = new QSpinBox;
    workerCount_->setObjectName("workerCount"); workerCount_->setRange(1, engine::MaxConcurrency);
    workerCount_->setValue(engine::DefaultConcurrency);
    timeout_ = new QSpinBox;
    timeout_->setObjectName("callTimeoutMs"); timeout_->setRange(1, 3600000);
    timeout_->setValue(engine::DefaultTimeoutMs); timeout_->setSuffix(" ms");
    QHBoxLayout* options = new QHBoxLayout;
    options->addWidget(new QLabel("Parallel workers:")); options->addWidget(workerCount_);
    options->addWidget(new QLabel("Timeout per call:")); options->addWidget(timeout_);
    layout->addLayout(options);
    loadDll_ = new QPushButton("Load selected DLL with these settings");
    loadDll_->setObjectName("loadAlgorithm");
    loadDll_->setEnabled(false);
    layout->addWidget(loadDll_);
    connect(loadDll_, &QPushButton::clicked, this, &Window::LoadSelectedDll);
    connect(dllPath_, &QLineEdit::textChanged, this, [this] { InvalidateAlgorithm(); });
    connect(workerCount_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { InvalidateAlgorithm(); });
    connect(timeout_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { InvalidateAlgorithm(); });
    layout->addWidget(modelTable_);
    layout->addWidget(createTemplates_);
    layout->addWidget(templateTable_);
    layout->addWidget(inferenceInfo_);
    layout->addWidget(calibrationInfo_);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString initial = dllPath_->text().isEmpty()
            ? InitialAlgorithmDirectory(QCoreApplication::applicationDirPath()) : dllPath_->text();
        const QFileInfo location(initial);
        QFileDialog dialog(this, WeightsFolderDialog, location.isDir() ? initial : location.absolutePath());
        dialog.setObjectName("algorithmDllDialog");
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setAcceptMode(QFileDialog::AcceptOpen);
        dialog.setNameFilter("K-NBTC algorithm DLL (*.dll)");
        if (!location.isDir()) dialog.selectFile(location.fileName());
        if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
            dllPath_->setText(dialog.selectedFiles().front());
        }
    });
    connect(createTemplates_, &QPushButton::clicked, this, &Window::CreateEnrollmentTemplates);
    return page;
}

QWizardPage* Window::ComparisonTypePage() {
    WorkflowPage* page = new WorkflowPage(this);
    page->setTitle(PageTitles[3]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(ComparisonTypeHint));
    comparisonType_ = new QComboBox;
    comparisonType_->setObjectName("comparisonType");
    comparisonType_->addItems(ComparisonTypes);
    layout->addWidget(comparisonType_);
    layout->addStretch();
    return page;
}

QWizardPage* Window::ComparisonRulesPage() {
    WorkflowPage* page = new WorkflowPage(this);
    page->setTitle(PageTitles[4]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(RulesHint));
    layout->addWidget(ComparisonPanel(true));
    layout->addWidget(ComparisonPanel(false));
    layout->addStretch();
    return page;
}

QWidget* Window::ComparisonPanel(bool genuine) {
    ComparisonControls& controls = genuine ? genuine_ : impostor_;
    const QString prefix = genuine ? "genuine" : "impostor";
    QGroupBox* panel = new QGroupBox(genuine ? "Genuine" : "Impostor");
    panel->setObjectName(prefix + "Options");
    controls.panel = panel;
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(genuine ? GenuineHint : ImpostorHint));
    controls.rule = new QComboBox;
    controls.rule->setObjectName(prefix + "Rule");
    controls.rule->addItems(ComparisonRules);
    controls.seed = new QSpinBox;
    controls.seed->setObjectName(prefix + "Seed");
    controls.seed->setRange(0, std::numeric_limits<int>::max());
    controls.seed->setValue(int(engine::Seed));
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(new QLabel(RuleLabel));
    row->addWidget(controls.rule, 1);
    layout->addLayout(row);

    QStackedWidget* options = new QStackedWidget;
    options->setFixedHeight(OptionsHeight);
    QLabel* allHint = new QLabel(genuine ? AllGenuineHint : AllImpostorHint);
    allHint->setWordWrap(true);
    options->addWidget(allHint);
    options->addWidget(new QLabel(FirstCaptureHint));
    controls.captures = new QLineEdit(DefaultCaptures);
    controls.captures->setObjectName(prefix + "Captures");
    controls.captures->setPlaceholderText(CapturesHint);
    options->addWidget(controls.captures);
    QWidget* randomPage = new QWidget;
    QHBoxLayout* randomRow = new QHBoxLayout(randomPage);
    randomRow->setContentsMargins(0, 0, 0, 0);
    controls.count = new QSpinBox;
    controls.count->setObjectName(prefix + "Count");
    controls.count->setRange(1, MaxRandomCount);
    controls.count->setValue(engine::RandomCount);
    randomRow->addWidget(new QLabel(CountLabel));
    randomRow->addWidget(controls.count);
    randomRow->addStretch();
    randomRow->addWidget(new QLabel(SeedLabel));
    randomRow->addWidget(controls.seed);
    options->addWidget(randomPage);
    QWidget* csvPage = new QWidget;
    QHBoxLayout* csvRow = new QHBoxLayout(csvPage);
    csvRow->setContentsMargins(0, 0, 0, 0);
    controls.file = new QLineEdit;
    controls.file->setObjectName(prefix + "Csv");
    controls.file->setReadOnly(true);
    controls.file->setPlaceholderText(CsvHint);
    QPushButton* browse = new QPushButton(BrowsePairs);
    browse->setObjectName(prefix + "LoadCsv");
    csvRow->addWidget(controls.file);
    csvRow->addWidget(browse);
    options->addWidget(csvPage);
    layout->addWidget(options);
    controls.info = new QLabel(PairsInitial);
    controls.info->setObjectName(prefix + "Info");
    controls.info->setWordWrap(true);
    layout->addWidget(controls.info);
    connect(controls.rule, QOverload<int>::of(&QComboBox::currentIndexChanged), options, &QStackedWidget::setCurrentIndex);
    connect(controls.rule, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { UpdateEnrollment(); });
    connect(controls.seed, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { UpdateEnrollment(); });
    connect(controls.count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { UpdateEnrollment(); });
    connect(controls.captures, &QLineEdit::textChanged, this, [this] { UpdateEnrollment(); });
    connect(controls.file, &QLineEdit::textChanged, this, [this] { UpdateEnrollment(); });
    connect(browse, &QPushButton::clicked, this, [this, file = controls.file] {
        const QString path = QFileDialog::getOpenFileName(this, PairDialog, {}, PairFilter);
        if (!path.isEmpty())
            file->setText(path);
    });
    return panel;
}

QWizardPage* Window::ExecutionPage() {
    WorkflowPage* page = new WorkflowPage(this);
    page->setTitle(PageTitles[5]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(ExecutionHint));
    executionInfo_ = new QLabel;
    executionInfo_->setObjectName("executionInfo");
    executionInfo_->setWordWrap(true);
    runComparisons_ = new QPushButton(gui::RunComparisons);
    runComparisons_->setObjectName("runComparisons");
    runComparisons_->setEnabled(false);
    layout->addWidget(executionInfo_);
    layout->addWidget(runComparisons_);
    layout->addStretch();
    connect(runComparisons_, &QPushButton::clicked, this, &Window::RunSelectedComparisons);
    return page;
}

QWizardPage* Window::ResultsPage() {
    QWizardPage* page = new QWizardPage(this);
    page->setTitle(PageTitles[6]);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(ResultsHint));
    resultCalibrationInfo_ = new QLabel;
    resultCalibrationInfo_->setObjectName("resultCalibrationInfo");
    resultCalibrationInfo_->setWordWrap(true);
    layout->addWidget(resultCalibrationInfo_);
    resultTabs_ = new QTabWidget;
    resultTabs_->setObjectName("resultTabs");
    resultTabs_->setStyleSheet(ResultTabStyle);
    for (bool genuine : {true, false}) {
        ComparisonControls& controls = genuine ? genuine_ : impostor_;
        QWidget* tab = new QWidget;
        controls.resultPanel = tab;
        QVBoxLayout* results = new QVBoxLayout(tab);
        controls.resultInfo = new QLabel;
        controls.resultInfo->setWordWrap(true);
        results->addWidget(controls.resultInfo);
        QHBoxLayout* thresholdRow = new QHBoxLayout;
        controls.threshold = new QDoubleSpinBox;
        controls.threshold->setObjectName(genuine ? "genuineThreshold" : "impostorThreshold");
        controls.threshold->setDecimals(ThresholdPrecision);
        controls.threshold->setRange(ThresholdMinimum, ThresholdMaximum);
        controls.threshold->setSingleStep(ThresholdStep);
        controls.threshold->setValue(genuine ? engine::GenuineThreshold : engine::ImpostorThreshold);
        thresholdRow->addWidget(new QLabel(ThresholdLabel));
        thresholdRow->addWidget(controls.threshold);
        thresholdRow->addStretch();
        results->addLayout(thresholdRow);
        QLabel* thresholdHint = new QLabel(genuine ? GenuineThresholdHint : ImpostorThresholdHint);
        thresholdHint->setWordWrap(true);
        results->addWidget(thresholdHint);
        connect(controls.threshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, genuine] { ApplyThreshold(genuine ? genuine_ : impostor_); });
        controls.table = MakeTable(ComparisonHeaders);
        controls.table->setObjectName(genuine ? "genuinePairs" : "impostorPairs");
        controls.table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        for (int column = 3; column < controls.table->columnCount(); ++column)
            controls.table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        results->addWidget(controls.table);
        QLabel* hint = new QLabel(ScoreHint);
        hint->setWordWrap(true);
        results->addWidget(hint);
        resultTabs_->addTab(tab, ResultTabs[genuine ? 0 : 1]);
    }
    QWidget* report = new QWidget;
    QVBoxLayout* reportLayout = new QVBoxLayout(report);
    reportLayout->addWidget(new QLabel(MetricHint));
    QGridLayout* checks = new QGridLayout;
    metrics_ = MakeTable(MetricHeaders);
    metrics_->setObjectName("metrics");
    for (int i = 0; i < MetricNames.size(); ++i) {
        AddRow(metrics_, {MetricNames[i], "N/A (not run)"});
        QCheckBox* check = new QCheckBox(MetricNames[i]);
        check->setObjectName("metric_" + QString::number(i));
        metricChoices_.push_back(check);
        bool selected = false;
        for (int index : DefaultMetrics) if (index == i) selected = true;
        check->setChecked(selected);
        metrics_->setRowHidden(i, !check->isChecked());
        checks->addWidget(check, i / MetricColumns, i % MetricColumns);
        connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
            metrics_->setRowHidden(i, !checked || !metricChoices_[i]->isEnabled());
        });
    }
    QPushButton* save = new QPushButton(SaveAll);
    save->setObjectName("saveResults");
    save->setEnabled(false);
    reportLayout->addLayout(checks);
    reportLayout->addWidget(metrics_);
    resultTabs_->addTab(report, ResultTabs[2]);
    layout->addWidget(resultTabs_);
    layout->addWidget(save);
    const auto updateSave = [this, save, report](int) -> void {
        save->setEnabled(comparisonsExecuted_ && currentId() == 6 && resultTabs_->currentWidget() == report);
    };
    connect(resultTabs_, &QTabWidget::currentChanged, this, updateSave);
    connect(this, &QWizard::currentIdChanged, this, updateSave);
    connect(save, &QPushButton::clicked, this, [this, report] {
        if (!comparisonsExecuted_ || currentId() != 6 || resultTabs_->currentWidget() != report) return;
        const QString folder = QFileDialog::getExistingDirectory(this, OutputFolderDialog);
        if (folder.isEmpty())
            return;
        const bool saved = SaveResults(folder);
        if (closeRequested_) return;
        if (saved)
            QMessageBox::information(this, SavedTitle, SavedMessage);
        else if (engine_.Cancelled())
            QMessageBox::information(this, "Report stopped", "Unfinished report files were discarded. Previous results are preserved.");
        else
            QMessageBox::warning(this, SaveFailureTitle, SaveFailureMessage);
    });
    return page;
}

void Window::LoadDatabase(const QString& folder, const QString& database_id) {
    if (busy_) return;
    // Unknown names can enroll, but do not provide comparison labels.
    imageList_->clear();
    images_.clear();
    databaseId_ = database_id;
    comparisonType_->setCurrentIndex(0);
    for (ComparisonControls* controls : {&genuine_, &impostor_}) {
        controls->rule->setCurrentIndex(0);
        controls->file->clear();
    }
    databasePath_ = QDir(folder).absolutePath();
    QMap<QString, int> resolutions;
    std::vector<QImage> decodedImages;
    const QFileInfoList files = QDir(folder).entryInfoList({"*.tif", "*.tiff"}, QDir::Files, QDir::Name);
    for (const QFileInfo& file : files) {
        QImageReader reader(file.absoluteFilePath());
        const QSize size = reader.size();
        const QString resolution = size.isValid()
            ? QString("%1 × %2 px (width x height)").arg(size.width()).arg(size.height())
            : "Resolution unavailable";
        ++resolutions[resolution];
        const QImage decoded = reader.read().convertToFormat(QImage::Format_Grayscale8);
        if (decoded.isNull())
            continue;
        int sourceId = int(images_.size());
        for (int i = 0; i < int(decodedImages.size()); ++i) {
            if (decoded == decodedImages[i]) {
                sourceId = images_[i].source_id;
                break;
            }
        }
        const QStringList parts = file.completeBaseName().split('_');
        bool fingerOk = false;
        bool captureOk = false;
        const int finger = parts.value(0).toInt(&fingerOk);
        const int capture = parts.value(1).toInt(&captureOk);
        const bool known = parts.size() == 2 && fingerOk && captureOk && finger > 0 && capture > 0;
        images_.push_back({file.fileName().toStdString(), known ? finger : 0, known ? capture : 0, sourceId,
                           file.absoluteFilePath().toStdString(), databaseId_.toStdString()});
        decodedImages.push_back(decoded);
        imageList_->addItem(file.fileName());
        imageList_->item(imageList_->count() - 1)->setToolTip(file.absoluteFilePath());
    }
    databaseSummary_ = QString("%1: %2 TIFF files").arg(QFileInfo(folder).fileName()).arg(files.size());
    QStringList sizes;
    for (QMap<QString, int>::const_iterator it = resolutions.cbegin(); it != resolutions.cend(); ++it)
        sizes.append(resolutions.size() == 1 ? it.key() : QString("%1: %2 files").arg(it.key()).arg(it.value()));
    if (!sizes.isEmpty())
        databaseSummary_ += " / " + sizes.join(", ");
    databaseInfo_->setText(databaseSummary_);
    databaseInfo_->setToolTip(databasePath_);
    if (!images_.empty())
        imageList_->setCurrentRow(0);
    else {
        preview_->setText("No readable TIFF images. Check the folder and Qt TIFF image plugin.");
        UpdateEnrollment();
    }
    ClearTemplates(); UpdateEnrollment();
    if (!dllHash_.isEmpty())
        inferenceInfo_->setText("Database changed. DLL remains loaded; create new enrollment templates.");
}

void Window::ShowImage(int row) {
    if (row < 0 || row >= int(images_.size())) {
        preview_->setText(ImagePrompt);
        return;
    }
    const QImage image(QString::fromStdString(images_[row].path));
    if (image.isNull())
        preview_->setText("Cannot read this image.");
    else
        preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Window::ClearTemplates() {
    enrollmentTemplates_.clear();
    enrollmentTemplates_.resize(images_.size());
    templatesReady_ = false;
    templateTable_->setRowCount(0);
    bool loaded = false;
    for (const engine::ModelState& model : engine_.Models())
        if (model.loaded) loaded = true;
    createTemplates_->setEnabled(loaded && !imageList_->selectedItems().isEmpty());
    comparisonsExecuted_ = false;
    genuine_.results.clear(); impostor_.results.clear();
    for (ComparisonControls* controls : {&genuine_, &impostor_}) {
        if (controls->table) controls->table->setRowCount(0);
        if (controls->resultInfo) controls->resultInfo->clear();
    }
}

static QString DllHash(const QString& path, QString* error = nullptr) {
    if (error) error->clear();
    if (path.isEmpty()) return QString();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return QString();
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail("Cannot read algorithm DLL: " + QDir::toNativeSeparators(path) + "\n" + file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return fail("Cannot hash algorithm DLL: " + QDir::toNativeSeparators(path));
    // A deployment manifest also identifies auxiliary files/weights as a bundle.
    const QDir folder = QFileInfo(path).dir();
    QFile manifest(folder.filePath("SHA256SUMS.txt"));
    if (manifest.exists()) {
        if (!manifest.open(QIODevice::ReadOnly))
            return fail("Cannot read deployment manifest: " + QDir::toNativeSeparators(manifest.fileName()));
        QByteArray contents = manifest.readAll();
        if (manifest.error() != QFile::NoError) return fail("Cannot read deployment manifest: " + manifest.errorString());
        // CMake file(WRITE) uses native CRLF on Windows. Strip only line endings,
        // preserving spaces and Unicode characters in relative file names.
        contents.replace("\r\n", "\n");
        hash.addData(contents);
        QSet<QString> seen;
        const QString prefix = folder.absolutePath().endsWith('/') ? folder.absolutePath() : folder.absolutePath() + '/';
        int lineNumber = 0;
        for (const QByteArray& line : contents.split('\n')) {
            ++lineNumber;
            if (line.isEmpty()) continue;
            const QByteArray expected = line.left(64);
            if (line.size() < 67 || line.mid(64, 2) != "  " || line.contains('\0')
                || QByteArray::fromHex(expected).toHex() != expected)
                return fail(QString("Invalid deployment manifest at line %1.").arg(lineNumber));
            const QByteArray nameBytes = line.mid(66);
            const QString name = QString::fromUtf8(nameBytes);
            const QString resolved = QDir::cleanPath(folder.absoluteFilePath(name));
            const QString key = resolved.toCaseFolded();
            if (name.toUtf8() != nameBytes || !resolved.startsWith(prefix)
                || key == QFileInfo(manifest).absoluteFilePath().toCaseFolded() || seen.contains(key))
                return fail("Invalid deployment manifest path: " + name);
            seen.insert(key);
            QFile auxiliary(resolved);
            if (!auxiliary.open(QIODevice::ReadOnly))
                return fail("Cannot read deployment file: " + name + "\n" + auxiliary.errorString());
            QCryptographicHash auxiliaryHash(QCryptographicHash::Sha256);
            if (!auxiliaryHash.addData(&auxiliary)) return fail("Cannot hash deployment file: " + name);
            const QByteArray actual = auxiliaryHash.result();
            if (actual.toHex() != expected) return fail("Deployment SHA-256 mismatch: " + name);
            hash.addData(nameBytes); hash.addData(actual);
        }
        if (seen.isEmpty()) return fail("Deployment manifest contains no files.");
    }
    return QString::fromLatin1(hash.result().toHex());
}
bool Window::AlgorithmUnchanged() {
    if (dllHash_.isEmpty()) {
        inferenceInfo_->setText("Select a DLL and click Load selected DLL with these settings first.");
        return false;
    }
    if (dllHash_ != DllHash(dllPath_->text())) {
        InvalidateAlgorithm();
        inferenceInfo_->setText("Algorithm files changed. Click Load selected DLL with these settings again.");
        return false;
    }
    return true;
}
void Window::InvalidateAlgorithm() {
    if (busy_) return;
    engine_.Configure(nullptr, "");
    algorithm_.reset();
    dllHash_.clear();
    modelTable_->setRowCount(0);
    loadDll_->setEnabled(!dllPath_->text().isEmpty());
    inferenceInfo_->setText(dllPath_->text().isEmpty() ? ModelHint
        : "DLL or settings changed. Click Load selected DLL with these settings.");
    ClearTemplates(); UpdateEnrollment();
}

void Window::LoadSelectedDll() {
    if (busy_ || dllPath_->text().isEmpty()) return;
    InvalidateAlgorithm();
    const QString path = dllPath_->text();
    engine::DllOptions options;
    options.workers = workerCount_->value(); options.timeout_ms = timeout_->value();
    const QString worker = QDir(QCoreApplication::applicationDirPath()).filePath("FingerprintWorker.exe");
    algorithm_.reset(new engine::KisaDllAlgorithm(path.toStdString(), worker.toStdString(), options));
    engine_.SetConcurrency(options.workers);
    engine_.Configure(algorithm_.get(), path.toStdString());
    QString signature, error;
    std::string loadError;
    std::atomic<int> ready{0};
    bool loaded = false;
    const bool completed = RunWithProgress("Loading selected DLL", [&] {
        signature = DllHash(path, &error);
        if (signature.isEmpty() || engine_.Cancelled()) return;
        loaded = algorithm_->Load(loadError, [&] { ++ready; });
        if (!loaded) error = QString::fromStdString(loadError);
    }, [&] { return std::make_pair(ready.load(), options.workers); });
    if (!completed || !loaded) {
        engine_.Configure(nullptr, "");
        algorithm_.reset();
        AddRow(modelTable_, {"K-NBTC DLL", QFileInfo(path).fileName(), completed ? "Error" : "Stopped"});
        inferenceInfo_->setText(completed ? (error.isEmpty() ? "Cannot load selected DLL." : error)
            + "\nCheck the DLL and its dependencies, then click Load again."
            : "Stopped; DLL workers released. Click Load to retry.");
        ClearTemplates(); UpdateEnrollment();
        return;
    }
    dllHash_ = signature;
    AddRow(modelTable_, {"K-NBTC DLL", QFileInfo(path).fileName(),
        QString("Loaded (%1 worker%2)").arg(options.workers).arg(options.workers == 1 ? "" : "s")});
    modelTable_->item(0, 2)->setToolTip(QString("KISA exports loaded in %1 workers; timeout per call: %2 ms. "
        "Internal model initialization may occur on the first inference call.").arg(options.workers).arg(options.timeout_ms));
    genuine_.threshold->setValue(engine::GenuineThreshold);
    impostor_.threshold->setValue(engine::ImpostorThreshold);
    const QString hint = "Threshold starts at 0.5; adjust it in Results for this DLL.";
    calibrationInfo_->setText(hint); resultCalibrationInfo_->setText(hint);
    inferenceInfo_->setText("DLL loaded with the selected settings. Click Create enrollment templates. "
                           "The DLL may initialize internal models on its first inference call.");
    ClearTemplates(); UpdateEnrollment();
}
bool Window::RunWithProgress(const QString& title, const std::function<void()>& work,
                             const std::function<std::pair<int, int>()>& progressCounts) {
    engine_.ResetCancel();
    busy_ = true;
    std::vector<std::pair<QWidget*, bool>> disabled;
    for (int id : pageIds()) disabled.push_back({page(id), page(id)->isEnabled()});
    for (QWizard::WizardButton which : {QWizard::BackButton, QWizard::NextButton, QWizard::CancelButton, QWizard::FinishButton})
        disabled.push_back({button(which), button(which)->isEnabled()});
    for (const std::pair<QWidget*, bool>& item : disabled) item.first->setEnabled(false);
    QProgressDialog progress(title, "Stop", 0, 0, this);
    progress.setObjectName("workProgress");
    // Pages and navigation are disabled; keep the main window's close action usable.
    progress.setWindowModality(Qt::NonModal); progress.setMinimumDuration(0);
    progress.setAutoClose(false); progress.setAutoReset(false);
    QEventLoop loop;
    QTimer timer;
    std::future<void> task;
    std::exception_ptr launchError;
    try {
        task = std::async(std::launch::async, [this, &work] {
            try { work(); }
            catch (...) { engine_.Cancel(); throw; }
        });
    } catch (...) { launchError = std::current_exception(); engine_.Cancel(); }
    bool stopping = false;
    const QMetaObject::Connection stopConnection = connect(&progress, &QProgressDialog::canceled, this, [this] { engine_.Cancel(); });
    connect(&timer, &QTimer::timeout, &loop, [this, title, &progress, &task, &loop, &stopping, &progressCounts] {
        const std::pair<int, int> counts = progressCounts ? progressCounts()
            : std::make_pair(engine_.ProgressDone(), engine_.ProgressTotal());
        const int total = counts.second;
        if (engine_.Cancelled()) {
            if (!stopping) { stopping = true; progress.setCancelButton(nullptr); progress.setRange(0, 0); }
            progress.setLabelText("Stopping and releasing resources...");
            if (!progress.isVisible()) progress.show();
        } else if (total > 0) {
            progress.setRange(0, total); progress.setValue(counts.first);
            progress.setLabelText(title + QString("\n%1 / %2 completed (%3%)")
                .arg(counts.first).arg(total).arg(100.0 * counts.first / total, 0, 'f', 1));
        }
        if ((!task.valid() || task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            && engine_.CancellationFinished() && !exitQuestionOpen_) loop.quit();
    });
    timer.start(50); progress.show(); loop.exec(); timer.stop(); disconnect(stopConnection); progress.close();
    try {
        if (launchError) std::rethrow_exception(launchError);
        task.get();
    } catch (const std::exception& e) {
        if (!closeRequested_) QMessageBox::warning(this, "Execution failed", QString::fromUtf8(e.what()));
    } catch (...) {
        if (!closeRequested_) QMessageBox::warning(this, "Execution failed", "Unknown execution error.");
    }
    busy_ = false;
    if (closeRequested_) {
        // Let the caller discard unfinished results before delivering the close.
        QTimer::singleShot(0, this, [this] { if (closeRequested_ && !busy_) QWizard::done(closeResult_); });
    } else {
        for (const std::pair<QWidget*, bool>& item : disabled) item.first->setEnabled(item.second);
    }
    return !engine_.Cancelled();
}

void Window::CreateEnrollmentTemplates() {
    if (busy_ || !AlgorithmUnchanged()) return;
    ClearTemplates();
    std::vector<int> selected;
    for (const QListWidgetItem* item : imageList_->selectedItems()) selected.push_back(imageList_->row(item));
    std::vector<engine::EnrollmentTemplates> results;
    const bool completed = RunWithProgress("Generating enrollment templates", [this, &selected, &results] {
        results = engine_.EnrollImages(images_, selected);
    });
    if (!completed) { ClearTemplates(); inferenceInfo_->setText("Stopped; generated templates discarded."); UpdateEnrollment(); return; }
    enrollmentTemplates_ = std::move(results);
    int successful = 0;
    for (int index : selected) {
        for (const engine::ModelTemplate& model : enrollmentTemplates_[index].models) {
            AddRow(templateTable_, {QString::fromStdString(images_[index].name), QString::fromStdString(model.model_id),
                model.success ? "OK" : "ERR", QString::number(model.data.size()), QString::fromStdString(model.error),
                QString::number(model.time_ms, 'f', TimePrecision)});
            if (model.success) ++successful;
        }
        // A completed enrollment phase can proceed even if every enrollment fails:
        // the report must still show FTE and the predefined failed comparisons.
        if (enrollmentTemplates_[index].executed) templatesReady_ = true;
    }
    inferenceInfo_->setText(QString("Successful enrollment templates: %1 / %2.").arg(successful).arg(selected.size()));
    UpdateEnrollment();
}

void Window::DisplayComparisons(ComparisonControls& controls) {
    for (int row = 0; row < int(controls.results.size()); ++row) {
        const engine::SimilarityResult& result = controls.results[row];
        for (int model = 0; model < int(result.model_scores.size()); ++model) {
            const engine::ModelScore& score = result.model_scores[model];
            QTableWidgetItem* cell = controls.table->item(row, 4 + model);
            cell->setText(score.success ? QString::number(score.score, 'f', ScorePrecision) : "ERR");
        }
        controls.table->item(row, controls.table->columnCount() - 1)->setText(QString::number(result.time_ms, 'f', TimePrecision));
        controls.table->item(row, 3)->setToolTip(result.success
            ? QString("DLL similarity: %1 (%2).")
                .arg(result.score, 0, 'g', engine::CsvPrecision)
                .arg(QString::fromStdString(result.model_id))
            : "No valid score; see the error tooltip.");
    }
    controls.table->resizeRowsToContents();
    ApplyThreshold(controls);
}

void Window::ApplyThreshold(ComparisonControls& controls) {
    const bool genuine = &controls == &genuine_;
    int correct = 0, incorrect = 0, errors = 0;
    for (int row = 0; row < int(controls.results.size()); ++row) {
        const engine::SimilarityResult& result = controls.results[row];
        const QString decision = engine::Engine::ClassifyComparison(genuine, result, controls.threshold->value());
        controls.table->item(row, 3)->setText(decision);
        for (int column = 0; column < controls.table->columnCount(); ++column) {
            controls.table->item(row, column)->setBackground(QBrush());
            controls.table->item(row, column)->setForeground(Qt::black);
        }
        controls.table->item(row, 3)->setBackground(QColor(DecisionColor(decision)));
        for (int model = 0; model < int(result.model_scores.size()); ++model) {
            const engine::ModelScore& score = result.model_scores[model];
            QTableWidgetItem* cell = controls.table->item(row, 4 + model);
            QString modelDecision = "ERR";
            if (score.success) {
                const bool accepted = score.score >= controls.threshold->value();
                if (genuine) modelDecision = accepted ? "PASS" : "MISS";
                else modelDecision = accepted ? "FA" : "REJ";
            }
            cell->setBackground(QColor(DecisionColor(modelDecision)));
            QString detail = score.success ? QString::number(score.score, 'g', engine::CsvPrecision)
                : QString::fromStdString(score.stage + ": " + score.error);
            detail += QString("\n%1 at threshold %2.").arg(modelDecision).arg(controls.threshold->value(), 0, 'f', ThresholdPrecision);
            cell->setToolTip(detail);
        }
        if (!result.success) ++errors;
        else if (decision == "PASS" || decision == "REJ") ++correct;
        else ++incorrect;
    }
    if (!controls.results.empty())
        controls.resultInfo->setText(QString("Comparisons: %1 / %2: %3 / %4: %5 / ERR: %6")
            .arg(controls.results.size()).arg(genuine ? "PASS" : "REJ").arg(correct)
            .arg(genuine ? "MISS" : "FA").arg(incorrect).arg(errors));
    UpdateMetrics();
}

bool Window::ReadPairCsv(const QString& path, bool genuine, std::vector<engine::ComparisonPair>& pairs, QString& error) {
    pairs.clear();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        error = "Choose a readable pairs CSV file.";
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    const QStringList lines = text.split('\n');
    QStringList header;
    int enrollmentColumn = -1, attemptColumn = -1, typeColumn = -1;
    QMap<QString, int> indices;
    for (int i = 0; i < int(images_.size()); ++i)
        indices[QString::fromStdString(images_[i].name)] = i;
    for (int row = 0; row < lines.size(); ++row) {
        if (lines[row].trimmed().isEmpty())
            continue;
        QStringList fields;
        if (!CsvFields(lines[row], fields)) {
            error = QString("Invalid CSV quoting on line %1.").arg(row + 1);
            return false;
        }
        if (header.isEmpty()) {
            for (const QString& field : fields)
                header.append(field.trimmed().toLower());
            enrollmentColumn = header.indexOf("enrollment");
            attemptColumn = header.indexOf("attempt");
            typeColumn = header.indexOf("type");
            if (enrollmentColumn < 0 || attemptColumn < 0) {
                error = "CSV needs enrollment,attempt columns. Use filenames from this DB.";
                return false;
            }
            continue;
        }
        if (fields.size() != header.size()) {
            error = QString("CSV column count differs from the header on line %1.").arg(row + 1);
            return false;
        }
        if (typeColumn >= 0) {
            const QString type = fields[typeColumn].trimmed().toLower();
            if (type != "genuine" && type != "impostor") {
                error = QString("Unknown comparison type on line %1.").arg(row + 1);
                return false;
            }
            if (type != (genuine ? "genuine" : "impostor"))
                continue;
        }
        const QString enrollment = fields[enrollmentColumn], attempt = fields[attemptColumn];
        if (!indices.contains(enrollment) || !indices.contains(attempt)) {
            error = QString("Image not found in this DB on line %1.").arg(row + 1);
            return false;
        }
        pairs.push_back({indices[enrollment], indices[attempt]});
    }
    if (header.isEmpty()) {
        error = "CSV needs an enrollment,attempt header.";
        return false;
    }
    return true;
}

void Window::UpdateComparisons(ComparisonControls& controls, bool genuine) {
    controls.table->setRowCount(0);
    QStringList headers = ComparisonHeaders;
    for (const engine::ModelState& model : engine_.Models())
        headers.insert(headers.size() - 1, QString::fromStdString(model.model_id));
    controls.table->setColumnCount(headers.size());
    controls.table->setHorizontalHeaderLabels(headers);
    for (int column = 3; column < headers.size(); ++column)
        controls.table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    controls.results.clear();
    controls.pairs.clear();
    controls.resultInfo->clear();
    controls.valid = true;
    if (!ComparisonSelected(genuine)) {
        controls.info->setText(NoComparisonType);
        return;
    }
    engine::ComparisonOptions options;
    options.rule = static_cast<engine::ComparisonRule>(controls.rule->currentIndex());
    options.count = controls.count->value();
    if (options.rule == engine::ComparisonRule::RandomCount)
        options.seed = unsigned(controls.seed->value());
    QString error;
    if (options.rule == engine::ComparisonRule::CaptureNumbers) {
        for (const QString& value : controls.captures->text().split(',')) {
            bool ok = false;
            const int capture = value.trimmed().toInt(&ok);
            if (!ok || capture < 1) {
                error = "Enter positive capture numbers separated by commas (for example: 1, 3, 5).";
                break;
            }
            options.captures.push_back(capture);
        }
    } else if (options.rule == engine::ComparisonRule::FromCsv)
        ReadPairCsv(controls.file->text(), genuine, options.csv_pairs, error);
    std::vector<int> enrollments;
    for (const QListWidgetItem* item : imageList_->selectedItems())
        enrollments.push_back(imageList_->row(item));
    std::string planError;
    if (error.isEmpty() && !engine::Engine::PlanComparisons(images_, enrollments, genuine, options, controls.pairs, planError))
        error = QString::fromStdString(planError);
    if (error.isEmpty() && controls.pairs.empty()) error = NoComparisonPairs;
    controls.valid = error.isEmpty();
    if (!controls.valid) {
        controls.info->setText("Error: " + error);
        return;
    }
    for (const engine::ComparisonPair& pair : controls.pairs) {
        QStringList values = {QString::number(controls.table->rowCount() + 1),
            QString::fromStdString(images_[pair.enrollment].name),
            QString::fromStdString(images_[pair.attempt].name), "Not run"};
        while (values.size() < headers.size()) values.append(QString());
        AddRow(controls.table, values);
    }
    QString summary = QString("Planned pairs: %1. ").arg(controls.pairs.size());
    if (options.rule == engine::ComparisonRule::FromCsv)
        summary += "CSV order is preserved.";
    else if (options.rule == engine::ComparisonRule::RandomCount)
        summary += QString("Random seed: %1.").arg(options.seed);
    else
        summary += "Comparison order is shuffled and saved.";
    controls.info->setText(summary);
}

void Window::UpdateEnrollment() {
    comparisonsExecuted_ = false;
    UpdateComparisons(genuine_, true);
    UpdateComparisons(impostor_, false);
    if (ComparisonSelected(true) && ComparisonSelected(false) && genuine_.valid && impostor_.valid) {
        for (int row = 0; row < impostor_.table->rowCount(); ++row) {
            const QString a = impostor_.table->item(row, 1)->text();
            const QString b = impostor_.table->item(row, 2)->text();
            bool conflict = false;
            for (int other = 0; other < genuine_.table->rowCount(); ++other) {
                const QString c = genuine_.table->item(other, 1)->text();
                const QString d = genuine_.table->item(other, 2)->text();
                if ((a == c && b == d) || (a == d && b == c)) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) {
                impostor_.valid = false;
                impostor_.info->setText("Error: This pair is also labelled Genuine. Correct the CSV labels.");
                impostor_.table->setRowCount(0);
                impostor_.pairs.clear();
                break;
            }
        }
    }
    enrollmentInfo_->setText(QString("Enrolled images: %1").arg(imageList_->selectedItems().size()));
    for (const engine::ImageInfo& image : images_) {
        if (image.finger == 0) {
            enrollmentInfo_->setText(enrollmentInfo_->text() + "\nImages with unknown finger IDs require labelled CSV pairs for comparisons.");
            break;
        }
    }
    UpdateWorkflow();
    UpdateMetrics();
}

bool Window::ComparisonSelected(bool genuine) const {
    const int type = comparisonType_->currentIndex();
    return type == 3 || type == (genuine ? 1 : 2);
}


void Window::UpdateWorkflow() {
    const bool selected = comparisonType_->currentIndex() > 0;
    bool valid = selected;
    QStringList summary;
    for (bool genuine : {true, false}) {
        ComparisonControls& controls = genuine ? genuine_ : impostor_;
        const bool active = ComparisonSelected(genuine);
        controls.panel->setVisible(active);
        const int tab = resultTabs_->indexOf(controls.resultPanel);
        if (active && tab < 0)
            resultTabs_->insertTab(genuine || !ComparisonSelected(true) ? 0 : 1,
                                  controls.resultPanel, ResultTabs[genuine ? 0 : 1]);
        else if (!active && tab >= 0)
            resultTabs_->removeTab(tab);
        if (!active) continue;
        valid = valid && controls.valid && !controls.pairs.empty();
        summary.append(QString("%1: %2 pairs\n%3")
            .arg(genuine ? "Genuine" : "Impostor").arg(controls.pairs.size())
            .arg(controls.rule->currentText()));
    }
    runComparisons_->setEnabled(templatesReady_ && valid);
    if (!comparisonsExecuted_)
        executionInfo_->setText(summary.isEmpty() ? NoComparisonType : summary.join("\n\n"));
    static_cast<WorkflowPage*>(page(2))->SetReady(templatesReady_);
    static_cast<WorkflowPage*>(page(3))->SetReady(selected);
    static_cast<WorkflowPage*>(page(4))->SetReady(templatesReady_ && valid);
    static_cast<WorkflowPage*>(page(5))->SetReady(comparisonsExecuted_);
}

void Window::RunSelectedComparisons() {
    if (busy_ || currentId() != 5 || !runComparisons_->isEnabled() || !AlgorithmUnchanged()) return;
    runComparisons_->setEnabled(false);
    genuine_.results.clear(); impostor_.results.clear(); comparisonsExecuted_ = false;
    std::vector<engine::ComparisonPair> pairs;
    if (ComparisonSelected(true)) pairs = genuine_.pairs;
    const std::size_t genuineCount = pairs.size();
    if (ComparisonSelected(false)) pairs.insert(pairs.end(), impostor_.pairs.begin(), impostor_.pairs.end());
    std::vector<engine::SimilarityResult> results;
    const bool completed = RunWithProgress("Evaluating comparisons", [this, &pairs, &results] {
        results = engine_.Execute(pairs, images_, enrollmentTemplates_);
    });
    if (!completed) {
        genuine_.results.clear(); impostor_.results.clear();
        UpdateEnrollment(); executionInfo_->setText("Stopped; all results from this execution were discarded.");
        return;
    }
    genuine_.results.assign(results.begin(), results.begin() + genuineCount);
    impostor_.results.assign(results.begin() + genuineCount, results.end());
    DisplayComparisons(genuine_); DisplayComparisons(impostor_);
    comparisonsExecuted_ = true; executionInfo_->setText(ExecutionComplete);
    UpdateWorkflow();
    resultTabs_->setCurrentWidget(ComparisonSelected(true) ? genuine_.resultPanel : impostor_.resultPanel);
    next();
}

std::vector<bool> Window::SelectedMetrics() const {
    std::vector<bool> selected;
    for (int row = 0; row < metrics_->rowCount(); ++row) selected.push_back(!metrics_->isRowHidden(row));
    return selected;
}

engine::EvaluationRun Window::Snapshot() const {
    engine::EvaluationRun run;
    run.database = databasePath_.toStdString();
    run.algorithm_path = dllPath_->text().toStdString();
    run.models = engine_.Models();
    run.genuine_threshold = genuine_.threshold->value();
    run.impostor_threshold = impostor_.threshold->value();
    QString settings = "database=" + databasePath_ + "\nalgorithm_path=" + dllPath_->text() + "\n";
    settings += "timeout_ms=" + QString::number(timeout_->value()) + "\n"
        + "concurrency=" + QString::number(workerCount_->value()) + "\n";
    QStringList selected;
    for (int row = 0; row < metrics_->rowCount(); ++row)
        if (!metrics_->isRowHidden(row)) selected.append(MetricNames[row]);
    settings += "comparison_type=" + comparisonType_->currentText() + "\n";
    settings += "template_format=opaque bytes; lengths retained\nalgorithm_signature=" + dllHash_ + "\n";
    settings += "score_aggregation=selected DLL responsibility\nmatching_time=KISA_Compare only\n";
    settings += "report_metrics=" + selected.join("; ") + "\n";
    for (const QListWidgetItem* item : imageList_->selectedItems()) {
        const int index = imageList_->row(item);
        engine::EnrollmentRecord record;
        record.image_index = index;
        record.name = item->text().toStdString();
        if (index < int(enrollmentTemplates_.size())) record.result = enrollmentTemplates_[index];
        run.enrollments.push_back(record);
    }
    for (const ComparisonControls* controls : {&genuine_, &impostor_}) {
        const bool genuine = controls == &genuine_;
        if (!ComparisonSelected(genuine)) continue;
        const QString prefix = genuine ? "genuine_" : "impostor_";
        settings += prefix + "rule=" + controls->rule->currentText() + "\n";
        if (controls->rule->currentIndex() == int(engine::ComparisonRule::RandomCount))
            settings += prefix + "count=" + QString::number(controls->count->value()) + "\n"
                + prefix + "seed=" + QString::number(controls->seed->value()) + "\n";
        if (controls->rule->currentIndex() == int(engine::ComparisonRule::CaptureNumbers))
            settings += prefix + "captures=" + controls->captures->text() + "\n";
        if (controls->rule->currentIndex() == int(engine::ComparisonRule::FromCsv))
            settings += prefix + "csv=" + controls->file->text() + "\n";
        for (int row = 0; row < int(controls->pairs.size()); ++row) {
            const engine::ComparisonPair& pair = controls->pairs[row];
            engine::ComparisonRecord record;
            record.enrollment = images_[pair.enrollment].name;
            record.attempt = images_[pair.attempt].name;
            record.genuine = genuine;
            record.executed = row < int(controls->results.size());
            if (record.executed) record.result = controls->results[row];
            run.comparisons.push_back(record);
        }
    }
    run.settings = settings.toStdString();
    return run;
}

void Window::UpdateMetrics() {
    const QStringList values = CalculateMetrics(Snapshot());
    for (int row = 0; row < metrics_->rowCount(); ++row) {
        metrics_->item(row, 1)->setText(values[row]);
        const bool available = !values[row].startsWith("N/A");
        QString reason;
        if (!available) {
            if (values[row] == "N/A (no crossing)")
                reason = "EER cannot be determined from the current score distributions.";
            else if (values[row] == "N/A")
                reason = "Successful Genuine and Impostor comparisons are required.";
            else if (row >= 5 && row <= 10)
                reason = "Run both Genuine and Impostor comparisons to calculate this metric.";
            else if (row == 1 || row == 4 || row == 13)
                reason = "Run Genuine comparisons to calculate this metric.";
            else if (row == 2 || row == 3 || row == 14)
                reason = "Run Impostor comparisons to calculate this metric.";
            else if (row == 12)
                reason = "Run at least one comparison to calculate this metric.";
            else
                reason = "Enrollment results are required to calculate this metric.";
        }
        metricChoices_[row]->setEnabled(available);
        metricChoices_[row]->setToolTip(reason);
        metrics_->setRowHidden(row, !available || !metricChoices_[row]->isChecked());
    }
}

bool Window::SaveResults(const QString& folder) {
    if (busy_ || !comparisonsExecuted_ || !AlgorithmUnchanged()) return false;
    QDir destination(folder);
    int number = 1;
    while (destination.exists(QString("run_%1").arg(number))) ++number;
    const QString name = QString("run_%1").arg(number);
    QTemporaryDir staging(destination.filePath(".report-XXXXXX"));
    if (!staging.isValid()) return false;
    const engine::EvaluationRun run = Snapshot();
    const std::vector<bool> selected = SelectedMetrics(); // Read widgets only on the GUI thread.
    const int total = engine::Engine::RawExportSteps(run) + ReportSteps(selected);
    std::atomic<int> completedSteps{0};
    const engine::ExportControl control{[this] { return engine_.Cancelled(); }, [&completedSteps] { ++completedSteps; }};
    bool saved = false;
    const bool completed = RunWithProgress("Generating report (CSV / HTML / Word / SVG)", [&] {
        try {
            saved = engine::Engine::SaveRawResults(run, staging.path().toStdString(), control)
                && Save(run, selected, staging.path(), control);
            control.Check();
        } catch (const engine::ExportCancelled&) { saved = false; }
        catch (...) { QDir(staging.path()).removeRecursively(); throw; }
        if (!saved) QDir(staging.path()).removeRecursively();
    }, [&] { return std::make_pair(completedSteps.load(), total); });
    if (!completed || !saved) return false;
    // Publish only complete exports; never overwrite an existing successful run.
    if (!destination.rename(QFileInfo(staging.path()).fileName(), name)) return false;
    staging.setAutoRemove(false);
    return true;
}

} // namespace gui
