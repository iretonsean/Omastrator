#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// Excalidraw's .excalidraw files and its clipboard JSON: rectangle, ellipse,
// diamond, line, arrow, freedraw, text, image and frame. Imported as clean
// geometry, not the hand-drawn wobble; sketchy fill textures become solid
// fills. Bindings, sketch seeds and roughness have no equivalent and are
// dropped without comment, since they change nothing the designer would see.
namespace ExcalidrawImporter {
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &json, QStringList *warnings = nullptr);
// By content: a top-level object with elements[] and a recognized `type`.
bool canRead(const QByteArray &json);
}
