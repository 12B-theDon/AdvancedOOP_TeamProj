#include <QGuiApplication>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <cmath>
#include <QFile>
#include <QXmlStreamReader>
#include <QRegularExpression>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    if (argc < 3) {
        QTextStream(stderr) << "Usage: RenderDiagrams <PNG width> <SVG file>...\n";
        return 1;
    }
    bool valid = false;
    const int width = QString::fromLocal8Bit(argv[1]).toInt(&valid);
    if (!valid || width < 100 || width > 12000) return 1;
    for (int index = 2; index < argc; ++index) {
        const QString source = QString::fromLocal8Bit(argv[index]);
        QSvgRenderer renderer(source);
        if (!renderer.isValid() || renderer.viewBoxF().width() <= 0) {
            QTextStream(stderr) << "Invalid SVG: " << source << '\n';
            return 2;
        }
        const QRectF view = renderer.viewBoxF();
        const int height = static_cast<int>(std::lround(width * view.height() / view.width()));
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        double physicalWidth = .297;
        QFile svg(source);
        if (!svg.open(QIODevice::ReadOnly)) return 2;
        QXmlStreamReader xml(&svg);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("svg")) {
                const QString value = xml.attributes().value("width").toString();
                const QRegularExpression unit("^([0-9.]+)(mm|pt|px)?$");
                const QRegularExpressionMatch match = unit.match(value);
                if (match.hasMatch()) {
                    const double number = match.captured(1).toDouble();
                    const QString suffix = match.captured(2);
                    physicalWidth = suffix == "mm" ? number / 1000.0
                        : suffix == "pt" ? number * .0254 / 72.0 : number * .0254 / 96.0;
                }
                break;
            }
        }
        const int resolution = static_cast<int>(std::lround(width / physicalWidth));
        image.setDotsPerMeterX(resolution); image.setDotsPerMeterY(resolution);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        renderer.render(&painter); painter.end();
        const QFileInfo file(source);
        const QString output = file.absolutePath() + '/' + file.completeBaseName() + ".png";
        if (!image.save(output, "PNG")) return 3;
        QTextStream(stdout) << file.fileName() << " -> " << width << 'x' << height << '\n';
    }
}
