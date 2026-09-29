#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QImage>
#include <QSizeF>
#include <QString>

// File ▸ Export: PDF keeps vectors; PNG and JPEG rasterize the artboard.
namespace DocumentExporter {
enum class Format { pdf, png, jpeg, svg };
Format format(const QString &path);
// The first artboard that exports, as a document of its own (the document itself when it has
// one artboard, unlisted). Throws FileError when every artboard is set not to export.
VectorDocument exportedPage(const VectorDocument &document);
// PNG and JPEG hold at most 30,000 pixels a side and 200 megapixels; PDF and SVG have no such limit.
bool rasterFits(QSizeF page, double scale);
// The largest scale at which `page` still fits (1 is 72 ppi); above 1 for a small page too.
double largestRasterScale(QSizeF page);
// The exported page drawn at `scale`. Throws FileError when it doesn't fit (rasterFits).
QImage renderPage(const VectorDocument &document, double scale, bool transparent);
void writePdf(const VectorDocument &document, const QString &path);
// `scale` device pixels per point; 1 is 72 ppi.
void writePng(const VectorDocument &document, const QString &path, double scale = 1, bool transparent = false);
void writeJpeg(const VectorDocument &document, const QString &path, double scale = 1, int quality = 90);
}
