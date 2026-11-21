#include "GUI/GUI.h"
#include "GUI/startup.h"
#include <QApplication>
#include <QMessageBox>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef FINGERPRINT_AUTO_PREPARE_MODELS
    const gui::StartupPlan startup = gui::PlanStartup(QCoreApplication::applicationDirPath());
    if (startup.needs_preparation) {
        QString error;
        if (gui::StartPreparation(startup, QCoreApplication::applicationPid(), error)) return 0;
        QMessageBox::critical(nullptr, "Fingerprint setup", error);
        return 1;
    }
#endif
    gui::Window window;
    window.show();
    return app.exec();
}
