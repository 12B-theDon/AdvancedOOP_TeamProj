#pragma once

#include <QString>
#include <QVector>

// One original or augmentation; identity includes DB + finger ID.
struct TrainingImage {
    QString path;
    QString original; // Source TIFF filename.
    QString database;
    QString identity;
    bool validation = false; // Inherited from the original capture.
    bool augmented = false;
};

// Originals plus cached augmentations; FVC2004 is center-cropped before augmentation.
QVector<TrainingImage> PrepareAugmentations(const QString& imagesFolder, const QString& outputFolder,
                                           QString* manifestFile = nullptr);
