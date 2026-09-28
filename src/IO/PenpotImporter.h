#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// Penpot 2.x's exported .penpot file: a zip of JSON, the "v3 binfile"
// format (manifest.json, files/<id>.json, one JSON per page and per shape).
// The older transit-based v1 binary export is refused, asking for a
// re-export from a current Penpot. See docs/import/penpot.md.
namespace PenpotImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
// By content: a zip whose central directory lists manifest.json.
bool canRead(const QByteArray &data);
}
