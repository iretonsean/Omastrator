#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QString>

// File ▸ Export: PDF keeps vectors; PNG and JPEG rasterize the artboard.
namespace DocumentExporter {
enum class Format { pdf, png, jpeg, svg };
Format format(const QString &path);
// The current page's first artboard that exports, as a document of its own (the document itself when it has
// one artboard, unlisted). Throws FileError when every artboard is set not to export.
VectorDocument exportedPage(const VectorDocument &document);
// One PDF page per exported artboard across every page; the others export the current page's first exported artboard.
void writePdf(const VectorDocument &document, const QString &path);
// `scale` device pixels per point; 1 is 72 ppi.
void writePng(const VectorDocument &document, const QString &path, double scale = 1, bool transparent = false);
void writeJpeg(const VectorDocument &document, const QString &path, double scale = 1, int quality = 90);
}
