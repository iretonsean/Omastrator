#pragma once
#include "Document/Paint.h"
#include <QPainterPath>
#include <QRectF>
#include <vector>

// What a stroke covers beyond a plain QPen: inside and outside alignment,
// arrowheads, and dashes aligned to corners. The renderer, bounds and
// Outline Stroke all read it, so they agree.
namespace StrokeGeometry {
// Every subpath closes; alignment only applies then.
bool isClosed(const QPainterPath &path);
// Where the stroke reaches, cheaply: for bounds and hit tests.
QRectF extent(const QPainterPath &path, const StrokeStyle &stroke);
// The line the stroke runs along, trimmed on open ends where a triangle head covers it.
QPainterPath body(const QPainterPath &path, const StrokeStyle &stroke);
// The heads on each open subpath's ends, as filled shapes.
QPainterPath heads(const QPainterPath &path, const StrokeStyle &stroke);
// The dashes as open subpaths, stretched so a dash sits centred on each corner and end.
QPainterPath alignedDashes(const QPainterPath &path, const std::vector<double> &dashes);
// The whole area the stroke covers, as a fill: what Outline Stroke makes.
QPainterPath area(const QPainterPath &path, const StrokeStyle &stroke);
// The dash pattern in pen widths, as QPen and QPainterPathStroker take it.
QList<qreal> dashPattern(const std::vector<double> &dashes, double width);
}
