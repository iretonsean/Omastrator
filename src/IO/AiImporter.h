#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// Illustrator .ai: a modern file (starting "%PDF") is read as PDF; a legacy
// PostScript-based .ai (starting "%!PS-Adobe") goes through the EPS path.
// Parsing Illustrator's own private data (AIPrivateData) is out of scope.
namespace AiImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
bool canRead(const QString &path);
QStringList lastWarnings();
}
