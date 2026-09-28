#pragma once
#include "IO/FileError.h"
#include <QImage>
#include <QString>
#include <QStringList>

// File ▸ Place for raster images: JPEG, PNG, TIFF, WebP, GIF as Qt reads them,
// turned upright by their EXIF orientation. Throws FileError.
namespace ImageImporter {
// warnings is set for formats read as a flattened image, e.g. a PSD's layers.
QImage read(const QString &path, QStringList *warnings = nullptr);
// Name filters for the Place dialog, SVG included.
QStringList nameFilters();
bool isVector(const QString &path);
}
