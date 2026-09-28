#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// EPS and plain PostScript: converted with Ghostscript, run as a program
// (never linked as a library, to keep this MIT-clean), then read as PDF.
// A DOS EPS binary header's PostScript section is cut out first. Without
// Ghostscript installed, throws FileError naming the package to add.
// Overridable with OMASTRATOR_GS, the way OMASTRATOR_RCLONE works.
namespace EpsImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
bool canRead(const QString &path);
QStringList lastWarnings();
}
