#include "augment.h"
#include "augmentConfig.h"
#include "encoder.h"
#include "encoderConfig.h"
#include "trainConfig.h"
#include "evaluationConfig.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>
#include <random>

namespace algorithm {

namespace {
static double Sample(std::mt19937& random, double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(random);
}

} // namespace

QVector<TrainingImage> PrepareAugmentations(const QString& imagesFolder, const QString& outputFolder, QString* manifestFile) {
    Require(CopiesPerOriginal > 0 && ScaleMin > 0 && ScaleMax >= ScaleMin
            && Fvc2004ScaleMin > 0 && Fvc2004ScaleMax >= Fvc2004ScaleMin,
            "Invalid augmentation settings.");
    const QJsonObject config = {
        {"version", Version}, {"seed", int(AugmentSeed)}, {"copies", CopiesPerOriginal},
        {"rotation_probability", RotationProbability}, {"rotation_degrees", RotationDegrees},
        {"scale_probability", ScaleProbability}, {"scale_min", ScaleMin}, {"scale_max", ScaleMax},
        {"fvc2004_scale_min", Fvc2004ScaleMin}, {"fvc2004_scale_max", Fvc2004ScaleMax},
        {"noise_probability", NoiseProbability}, {"noise_std_min", NoiseStdMin}, {"noise_std_max", NoiseStdMax},
        {"brightness_probability", BrightnessProbability}, {"brightness_min", BrightnessMin},
        {"brightness_max", BrightnessMax}, {"foreground_threshold", ForegroundThreshold},
        {"max_clipped_foreground", MaxClippedForeground}, {"geometry_attempts", GeometryAttempts},
        {"validation_first_capture", ValidationFirstCapture},
        {"source_geometry", SourceGeometry}, {"center_crop_database", CenterCropDatabase},
        {"composition", "FVC2004 center crop first; rotation+scale about image center; brightness multiply; additive Gaussian noise; clamp gray8"}
    };
    QJsonArray sources;
    QVector<TrainingImage> images;
    for (const std::string& database : training::DatabaseIds) {
        const QString db = QString::fromStdString(database);
        const QDir dir(QDir(imagesFolder).filePath(db));
        const QFileInfoList files = dir.entryInfoList({"*.tif", "*.tiff"}, QDir::Files, QDir::Name);
        Require(!files.isEmpty(), "No TIFF images in " + dir.path());
        for (const QFileInfo& file : files) {
            const QStringList parts = file.completeBaseName().split('_');
            bool fingerOk = false, captureOk = false;
            const int finger = parts.value(0).toInt(&fingerOk), capture = parts.value(1).toInt(&captureOk);
            Require(parts.size() == 2 && fingerOk && captureOk && finger > 0 && capture > 0,
                    "Expected finger_capture.tif: " + file.fileName());
            images.append({file.absoluteFilePath(), file.fileName(), db, db + ":" + QString::number(finger),
                           capture >= ValidationFirstCapture, false});
            sources.append(QJsonObject{{"db", db}, {"file", file.fileName()},
                                       {"sha256", Hash(file.absoluteFilePath())}});
        }
    }
    const QJsonObject input{{"config", config}, {"sources", sources}};
    const QDir cache(outputFolder);
    const QString base = QString::fromUtf8(FolderPattern).arg(CopiesPerOriginal).arg(AugmentSeed);
    QString folder, manifestPath;
    for (int number = 1; ; ++number) {
        folder = cache.filePath(number == 1 ? base : base + "_" + QString::number(number));
        manifestPath = folder + "/manifest.json";
        if (!QDir(folder).exists()) break;
        QFile candidate(manifestPath);
        if (candidate.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(candidate.readAll()).object().value("input").toObject() == input) break;
    }
    if (manifestFile) *manifestFile = manifestPath;
    QFile oldManifest(manifestPath);
    if (oldManifest.open(QIODevice::ReadOnly)) {
        const QJsonObject saved = QJsonDocument::fromJson(oldManifest.readAll()).object();
        Require(saved.value("input").toObject() == input, "Augmentation cache metadata mismatch.");
        const QJsonArray files = saved.value("images").toArray();
        Require(files.size() == images.size() * CopiesPerOriginal, "Incomplete augmentation cache.");
        for (const QJsonValue& value : files) {
            const QJsonObject record = value.toObject();
            const QString path = QDir(folder).filePath(record.value("file").toString());
            Require(Hash(path) == record.value("sha256").toString(), "Changed augmentation: " + path);
            images.append({path, record.value("original").toString(), record.value("db").toString(),
                           record.value("identity").toString(), record.value("validation").toBool(), true});
        }
        QTextStream(stdout) << "Reusing augmentations: " << folder << Qt::endl;
        return images;
    }
    Require(QDir().mkpath(folder), "Cannot create " + folder);
    QJsonArray generated;
    const int originalCount = images.size();
    for (int index = 0; index < originalCount; ++index) {
        // Appending augmentations can reallocate images; keep this source by value.
        const TrainingImage source = images[index];
        QImage original = QImage(source.path).convertToFormat(QImage::Format_Grayscale8);
        Require(!original.isNull(), "Cannot decode " + source.path);
        const int sourceWidth = original.width(), sourceHeight = original.height();
        const bool centerCrop = source.database == CenterCropDatabase;
        const int side = qMin(sourceWidth, sourceHeight);
        const int cropLeft = centerCrop ? (sourceWidth - side) / 2 : 0;
        const int cropTop = centerCrop ? (sourceHeight - side) / 2 : 0;
        if (centerCrop) original = original.copy(cropLeft, cropTop, side, side);
        QVector<QPointF> foreground;
        for (int y = 0; y < original.height(); ++y)
            for (int x = 0; x < original.width(); ++x)
                if (original.constScanLine(y)[x] < ForegroundThreshold) foreground.append(QPointF(x, y));
        const QByteArray key = QCryptographicHash::hash((source.database + "/" + source.original).toUtf8(), QCryptographicHash::Sha256);
        std::mt19937 random(AugmentSeed ^ qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(key.constData())));
        for (int copy = 0; copy < CopiesPerOriginal; ++copy) {
            double rotation = 0, scale = 1, clipped = 0;
            QTransform transform;
            bool accepted = false;
            for (int attempt = 0; attempt < GeometryAttempts; ++attempt) {
                rotation = Sample(random, 0, 1) < RotationProbability ? Sample(random, -RotationDegrees, RotationDegrees) : 0;
                scale = Sample(random, 0, 1) < ScaleProbability
                    ? Sample(random, centerCrop ? Fvc2004ScaleMin : ScaleMin, centerCrop ? Fvc2004ScaleMax : ScaleMax) : 1;
                transform = QTransform();
                transform.translate(original.width() / 2.0, original.height() / 2.0);
                transform.rotate(rotation);
                transform.scale(scale, scale);
                transform.translate(-original.width() / 2.0, -original.height() / 2.0);
                int outside = 0;
                for (const QPointF& point : foreground) {
                    const QPointF moved = transform.map(point);
                    if (moved.x() < 1 || moved.y() < 1 || moved.x() >= original.width() - 1 || moved.y() >= original.height() - 1)
                        ++outside;
                }
                clipped = foreground.isEmpty() ? 0 : double(outside) / foreground.size();
                if (clipped <= MaxClippedForeground) { accepted = true; break; }
            }
            if (!accepted) { transform = QTransform(); rotation = 0; scale = 1; clipped = 0; }
            QImage canvas(original.size(), QImage::Format_RGB32);
            canvas.fill(Qt::white);
            {
                QPainter painter(&canvas);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.setTransform(transform);
                painter.drawImage(0, 0, original);
            }
            QImage result = canvas.convertToFormat(QImage::Format_Grayscale8);
            const double brightness = Sample(random, 0, 1) < BrightnessProbability ? Sample(random, BrightnessMin, BrightnessMax) : 1;
            const double sigma = Sample(random, 0, 1) < NoiseProbability ? Sample(random, NoiseStdMin, NoiseStdMax) : 0;
            std::normal_distribution<double> noise(0, sigma > 0 ? sigma : 1);
            for (int y = 0; y < result.height(); ++y) {
                uchar* row = result.scanLine(y);
                for (int x = 0; x < result.width(); ++x) {
                    double value = row[x] * brightness + (sigma > 0 ? noise(random) : 0);
                    if (value < 0) value = 0;
                    if (value > 255) value = 255;
                    row[x] = static_cast<uchar>(value + 0.5);
                }
            }
            const QString relative = source.database + "/" + QFileInfo(source.original).completeBaseName()
                                     + QString("_aug%1.png").arg(copy + 1, 2, 10, QChar('0'));
            const QString path = QDir(folder).filePath(relative);
            Require(QDir().mkpath(QFileInfo(path).absolutePath()) && result.save(path, "PNG"), "Cannot save " + path);
            generated.append(QJsonObject{{"file", relative}, {"original", source.original}, {"db", source.database},
                {"identity", source.identity}, {"validation", source.validation}, {"rotation", rotation}, {"scale", scale},
                {"brightness", brightness}, {"noise_std", sigma}, {"clipped_foreground", clipped},
                {"source_width", sourceWidth}, {"source_height", sourceHeight},
                {"crop_before_augmentation", centerCrop}, {"crop_left", cropLeft}, {"crop_top", cropTop},
                {"augmentation_width", original.width()}, {"augmentation_height", original.height()},
                {"geometry_fallback", !accepted}, {"sha256", Hash(path)}});
            images.append({path, source.original, source.database, source.identity, source.validation, true});
        }
        if (index % 10 == 0) QTextStream(stdout) << "Generated augmentations " << index << " / " << originalCount << Qt::endl;
    }
    QSaveFile manifest(manifestPath);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"input", input}, {"images", generated}}).toJson();
    Require(manifest.open(QIODevice::WriteOnly) && manifest.write(bytes) == bytes.size() && manifest.commit(),
            "Cannot save augmentation manifest.");
    QTextStream(stdout) << "Prepared " << generated.size() << " augmentations: " << folder << Qt::endl;
    return images;
}

} // namespace algorithm
