#pragma once
#include "Document/Paint.h"
#include "Document/VectorPath.h"
#include <QPainterPath>
#include <QRectF>
#include <optional>
#include <vector>

// Shapes the drawing tools make, as editable Bézier paths.
namespace Shapes {
VectorPath rectangle(const QRectF &rect, double cornerRadius = 0);
VectorPath ellipse(const QRectF &rect);
// A regular polygon inscribed in `radius`, one vertex straight up.
VectorPath polygon(QPointF center, double radius, int sides, double rotation = 0);
VectorPath star(QPointF center, double outerRadius, double innerRadius, int points, double rotation = 0);
VectorPath line(QPointF from, QPointF to);
}

enum class BooleanOperation { unite, intersect, minusFront, exclude };

// Pathfinder: the bottom path combined with each path above it in turn.
// `minusFront` cuts every upper path from the bottom one.
QPainterPath combine(const std::vector<QPainterPath> &bottomToTop, BooleanOperation operation);

// The area a stroke covers, as a filled outline: alignment, dashes and arrowheads included.
QPainterPath outlineStroke(const QPainterPath &path, const StrokeStyle &stroke);

// Grows (positive) or shrinks a closed path's outline by `distance`.
QPainterPath offsetPath(const QPainterPath &path, double distance, Qt::PenJoinStyle join = Qt::MiterJoin);

// Ramer–Douglas–Peucker, then Catmull–Rom handles: a freehand drag as a smooth path.
VectorPath fitFreehand(const std::vector<QPointF> &points, double tolerance, bool closed = false);

// Flattens curves and merges nearly straight runs, like Object ▸ Path ▸ Simplify.
VectorPath simplify(const VectorPath &path, double tolerance);

// Pen tool: the path a click or drag adds to.
void addAnchor(Contour &contour, QPointF at, std::optional<QPointF> dragTo);
