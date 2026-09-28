#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// A .sketch file: a zip of document.json, pages/*.json and images/*, read
// with ZipReader. Each page becomes an artboard group; symbolMaster and
// symbolInstance become a component and an instance with overrides. Shadows,
// blur and image fills have no equivalent in the document model and are
// left out, with a warning each.
namespace SketchImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
// `data` is the whole .sketch file (a zip), not one page's JSON.
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
// By content: a zip whose central directory lists document.json.
bool canRead(const QByteArray &data);
}
