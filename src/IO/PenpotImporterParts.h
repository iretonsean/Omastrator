#pragma once
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Internal helpers shared by PenpotImporter.cpp, +Shapes.cpp, +Style.cpp and
// +Text.cpp. Every coordinate Penpot gives a shape (x/y/width/height, path
// content points, gradient start/end) is already in absolute page space, not
// fractional or frame-relative, unlike Sketch's.
namespace PenpotImport {
QColor color(const QString &hex, double opacity);
LayerBlendMode blendModeFor(const QString &value);

// fills/strokes lands on `object`; shadow and blur are dropped, each with
// one warning.
void applyStyle(const QJsonObject &shape, VectorObject &object, QStringList &warnings);

// A path or bool shape's own `content`: absolute page-space move-to/line-to/
// curve-to/close-path commands, straight off the model's own PathNode shape.
VectorPath contentGeometry(const QJsonArray &content);
// rect/frame corner radii (r1..r4, top left clockwise) as a LiveRectangle
// sized to the shape's own local (0,0)-(w,h) box.
LiveRectangle rectangleShape(const QJsonObject &shape, const QSizeF &size);

// The content tree (root -> paragraph-set -> paragraph -> text spans) as
// CharacterFormat/TextRun/ParagraphFormat; vertical-align sets nothing the
// model has an equivalent for (see docs/import/penpot.md).
void readText(const QJsonObject &content, TextContent &out, QStringList &warnings);
}
