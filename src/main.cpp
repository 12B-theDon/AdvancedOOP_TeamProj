#include "GUI/GUI.h"
#include "encoder.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    algorithm::FingerprintEncoder encoder;
    gui::Window window;
    window.SetAlgorithm(&encoder);
    window.show();
    return app.exec();
}
