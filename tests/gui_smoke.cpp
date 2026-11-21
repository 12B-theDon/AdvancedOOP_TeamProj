#include "GUI.h"
#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QListWidget>
#include <QImage>
#include <QFileInfo>
#include <iostream>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gui::Window window;
    window.LoadDatabase(QDir(QString::fromUtf8(argv[1])).filePath("data/original/FVC2000_DB3_B"), "FVC2000_DB3_B");
    if (window.pageIds().size() != 7) return 1;
    QSpinBox* workers = window.findChild<QSpinBox*>("workerCount");
    QSpinBox* timeout = window.findChild<QSpinBox*>("callTimeoutMs");
    if (!workers || !timeout || workers->value() != 1 || timeout->value() != 30000) return 2;
    for (QDoubleSpinBox* threshold : window.findChildren<QDoubleSpinBox*>()) if (threshold->value() != .5) return 3;
    window.show(); app.processEvents();
    QListWidget* images = window.findChild<QListWidget*>("enrollmentImages");
    if (!images || images->count() != 80) return 4;
    images->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
    window.next(); window.next(); app.processEvents();
    if (window.currentId() != 2) return 5;
    if (argc >= 3 && !window.grab().save(QString::fromUtf8(argv[2]))) return 6;
    if (argc >= 3) {
        const QDir output = QFileInfo(QString::fromUtf8(argv[2])).dir();
        const QImage graph(output.filePath("output/error_rates.svg"));
        if (!graph.isNull()) graph.save(output.filePath("report_graph.png"));
    }
    std::cout << "Seven pages, DB load, worker/timeout controls and initial thresholds passed (Qt offscreen).\n";
}
