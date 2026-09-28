#pragma once
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Internal helpers shared by SketchImporter.cpp, +Shapes.cpp, +Style.cpp and
// +Text.cpp. Coordinates in every function here are local to the layer's own
// frame (0,0)-(width,height) unless said otherwise; SketchImporter.cpp bakes
// the accumulated position and rotation into document coordinates.
namespace SketchImport {
// "{0.5, 1}" -> QPointF(0.5, 1): Sketch's string-encoded points, fractional
// of the frame for gradients and shapePath's own points alike.
QPointF fractionPoint(const QString &text);
// {red,green,blue,alpha}, each 0..1.
QColor color(const QJsonObject &json);
// Sketch's blendMode integer (Normal=0 .. Luminosity=15); hardLight and
// exclusion have no match and become the nearest of overlay/difference.
LayerBlendMode blendModeFor(int value);

// A layer's `style`: fills, borders, opacity and blend land on `object`;
// shadows, blur and image fills are dropped, each with one warning.
void applyStyle(const QJsonObject &style, VectorObject &object, QStringList &warnings);

// A rectangle's own `points` (each with its own cornerRadius) or, without
// those, `fixedRadius`: a LiveRectangle sized to the layer's own frame, kept
// editable the way the drawing tools leave one.
LiveRectangle rectangleShape(const QJsonObject &layer, const QSizeF &size);
// oval, polygon, star and shapePath all store their outline the same way:
// `points[]` (curveFrom/curveTo/cornerRadius, fractional of the frame). One
// closed or open Bézier contour, in the layer's own local pixel coordinates.
VectorPath shapePathGeometry(const QJsonObject &layer, const QSizeF &size, QStringList &warnings);
// shapeGroup: its shapePath children combined by their booleanOperation, each
// already placed at its own frame offset within the group.
VectorPath booleanGroupGeometry(const QJsonArray &children, QStringList &warnings);

// attributedString's runs, as CharacterFormat/TextRun; fixed vs auto width
// (textBehaviour) sets `content.area`.
void readText(const QJsonObject &layer, TextContent &content, QStringList &warnings);
}
