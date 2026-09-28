#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// PDF in: an in-tree reader (PdfDocument, PdfContent) rebuilds live paths,
// text and images from the content streams; nothing is rasterised or traced.
// Each page becomes an artboard. Encrypted files and mesh shadings, tiling
// patterns and a few other features PDF.md marks out of scope are left out
// with a warning; encrypted files are refused outright.
namespace PdfImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
// True if `path` starts with the PDF magic bytes ("%PDF"), by content, not just its extension.
bool canRead(const QString &path);
// What the last read or parse on this thread left out, one sentence each.
QStringList lastWarnings();
}
