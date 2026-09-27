#include "Document/PathOperations.h"
#include <QLineF>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
// The handle length that makes a cubic follow a quarter circle.
constexpr double kappa = 0.5522847498307936;

QPointF polar(QPointF center, double radius, double angle)
{
    return {center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle)};
}

double perpendicular(QPointF point, QPointF a, QPointF b)
{
    const QLineF line(a, b);
    if (line.length() < 1e-9)
        return QLineF(point, a).length();
    return std::abs((b.x() - a.x()) * (a.y() - point.y()) - (a.x() - point.x()) * (b.y() - a.y())) / line.length();
}

void douglasPeucker(const std::vector<QPointF> &points, size_t first, size_t last, double tolerance, std::vector<bool> &keep)
{
    if (last <= first + 1)
        return;
    double worst = 0;
    size_t at = first;
    for (size_t index = first + 1; index < last; ++index) {
        const double distance = perpendicular(points[index], points[first], points[last]);
        if (distance > worst) {
            worst = distance;
            at = index;
        }
    }
    if (worst > tolerance) {
        keep[at] = true;
        douglasPeucker(points, first, at, tolerance, keep);
        douglasPeucker(points, at, last, tolerance, keep);
    }
}

std::vector<QPointF> reduce(const std::vector<QPointF> &points, double tolerance)
{
    if (points.size() < 3)
        return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    douglasPeucker(points, 0, points.size() - 1, tolerance, keep);
    std::vector<QPointF> result;
    for (size_t index = 0; index < points.size(); ++index) {
        if (keep[index])
            result.push_back(points[index]);
    }
    return result;
}

Contour smoothContour(const std::vector<QPointF> &points, bool closed)
{
    Contour contour;
    contour.closed = closed;
    const size_t count = points.size();
    for (size_t index = 0; index < count; ++index) {
        const QPointF here = points[index];
        PathNode node(here);
        const bool hasPrevious = closed || index > 0;
        const bool hasNext = closed || index + 1 < count;
        if (hasPrevious && hasNext && count > 2) {
            const QPointF previous = points[(index + count - 1) % count];
            const QPointF next = points[(index + 1) % count];
            // Catmull–Rom tangents, a sixth of the neighbours' span.
            const QPointF tangent = (next - previous) / 6.0;
            node.in = here - tangent;
            node.out = here + tangent;
            node.smooth = true;
        }
        contour.nodes.push_back(node);
    }
    return contour;
}
}

namespace Shapes {
VectorPath rectangle(const QRectF &input, double cornerRadius)
{
    const QRectF rect = input.normalized();
    VectorPath path;
    Contour contour;
    contour.closed = true;
    const double r = std::clamp(cornerRadius, 0.0, std::min(rect.width(), rect.height()) / 2);
    if (r <= 0) {
        for (QPointF corner : {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()})
            contour.nodes.emplace_back(corner);
    } else {
        const double k = r * kappa;
        const double l = rect.left(), t = rect.top(), ri = rect.right(), b = rect.bottom();
        contour.nodes = {
            PathNode({l + r, t}, {l + r - k, t}, {l + r, t}),
            PathNode({ri - r, t}, {ri - r, t}, {ri - r + k, t}),
            PathNode({ri, t + r}, {ri, t + r - k}, {ri, t + r}),
            PathNode({ri, b - r}, {ri, b - r}, {ri, b - r + k}),
            PathNode({ri - r, b}, {ri - r + k, b}, {ri - r, b}),
            PathNode({l + r, b}, {l + r, b}, {l + r - k, b}),
            PathNode({l, b - r}, {l, b - r + k}, {l, b - r}),
            PathNode({l, t + r}, {l, t + r}, {l, t + r - k}),
        };
    }
    path.contours.push_back(contour);
    return path;
}

VectorPath ellipse(const QRectF &input)
{
    const QRectF rect = input.normalized();
    const QPointF c = rect.center();
    const double rx = rect.width() / 2, ry = rect.height() / 2;
    const double kx = rx * kappa, ky = ry * kappa;
    Contour contour;
    contour.closed = true;
    contour.nodes = {
        PathNode({c.x(), c.y() - ry}, {c.x() - kx, c.y() - ry}, {c.x() + kx, c.y() - ry}, true),
        PathNode({c.x() + rx, c.y()}, {c.x() + rx, c.y() - ky}, {c.x() + rx, c.y() + ky}, true),
        PathNode({c.x(), c.y() + ry}, {c.x() + kx, c.y() + ry}, {c.x() - kx, c.y() + ry}, true),
        PathNode({c.x() - rx, c.y()}, {c.x() - rx, c.y() + ky}, {c.x() - rx, c.y() - ky}, true),
    };
    VectorPath path;
    path.contours.push_back(contour);
    return path;
}

VectorPath polygon(QPointF center, double radius, int sides, double rotation)
{
    sides = std::max(3, sides);
    Contour contour;
    contour.closed = true;
    for (int index = 0; index < sides; ++index)
        contour.nodes.emplace_back(polar(center, radius, rotation - std::numbers::pi / 2 + 2 * std::numbers::pi * index / sides));
    VectorPath path;
    path.contours.push_back(contour);
    return path;
}

VectorPath star(QPointF center, double outerRadius, double innerRadius, int points, double rotation)
{
    points = std::max(3, points);
    Contour contour;
    contour.closed = true;
    for (int index = 0; index < points * 2; ++index) {
        const double radius = index % 2 ? innerRadius : outerRadius;
        contour.nodes.emplace_back(polar(center, radius, rotation - std::numbers::pi / 2 + std::numbers::pi * index / points));
    }
    VectorPath path;
    path.contours.push_back(contour);
    return path;
}

VectorPath line(QPointF from, QPointF to)
{
    Contour contour;
    contour.nodes = {PathNode(from), PathNode(to)};
    VectorPath path;
    path.contours.push_back(contour);
    return path;
}
}

QPainterPath combine(const std::vector<QPainterPath> &bottomToTop, BooleanOperation operation)
{
    if (bottomToTop.empty())
        return {};
    QPainterPath result = bottomToTop.front();
    for (size_t index = 1; index < bottomToTop.size(); ++index) {
        const QPainterPath &next = bottomToTop[index];
        switch (operation) {
        case BooleanOperation::unite:
            result = result.united(next);
            break;
        case BooleanOperation::intersect:
            result = result.intersected(next);
            break;
        case BooleanOperation::minusFront:
            result = result.subtracted(next);
            break;
        case BooleanOperation::exclude:
            result = result.subtracted(next).united(next.subtracted(result));
            break;
        }
    }
    return result.simplified();
}

QPainterPath outlineStroke(const QPainterPath &path, const StrokeStyle &stroke)
{
    QPainterPathStroker stroker;
    stroker.setWidth(stroke.width);
    stroker.setCapStyle(stroke.cap);
    stroker.setJoinStyle(stroke.join);
    stroker.setMiterLimit(stroke.miterLimit);
    if (!stroke.dashes.empty() && stroke.width > 0) {
        QList<qreal> pattern;
        for (double length : stroke.dashes)
            pattern << std::max(0.01, length / stroke.width);
        if (pattern.size() % 2)
            pattern << pattern;
        stroker.setDashPattern(pattern);
    }
    // The stroker's outline overlaps itself; a union makes it one clean shape.
    QPainterPath outline = stroker.createStroke(path);
    outline.setFillRule(Qt::WindingFill);
    return outline.simplified();
}

QPainterPath offsetPath(const QPainterPath &path, double distance, Qt::PenJoinStyle join)
{
    if (distance == 0)
        return path;
    QPainterPathStroker stroker;
    stroker.setWidth(std::abs(distance) * 2);
    stroker.setJoinStyle(join);
    QPainterPath band = stroker.createStroke(path);
    band.setFillRule(Qt::WindingFill);
    return (distance > 0 ? path.united(band) : path.subtracted(band)).simplified();
}

VectorPath fitFreehand(const std::vector<QPointF> &points, double tolerance, bool closed)
{
    VectorPath path;
    if (points.empty())
        return path;
    std::vector<QPointF> reduced = reduce(points, tolerance);
    if (closed && reduced.size() > 2 && QLineF(reduced.front(), reduced.back()).length() < tolerance * 2)
        reduced.pop_back();
    path.contours.push_back(smoothContour(reduced, closed && reduced.size() > 2));
    return path;
}

VectorPath simplify(const VectorPath &input, double tolerance)
{
    VectorPath result;
    result.fillRule = input.fillRule;
    for (const QPolygonF &polygon : input.painterPath().toSubpathPolygons()) {
        std::vector<QPointF> points(polygon.begin(), polygon.end());
        const bool closed = points.size() > 2 && QLineF(points.front(), points.back()).length() < 1e-6;
        if (closed)
            points.pop_back();
        if (points.empty())
            continue;
        std::vector<QPointF> reduced = reduce(points, tolerance);
        result.contours.push_back(smoothContour(reduced, closed));
    }
    return result;
}

void addAnchor(Contour &contour, QPointF at, std::optional<QPointF> dragTo)
{
    PathNode node(at);
    if (dragTo && QLineF(at, *dragTo).length() > 0.5) {
        node.out = *dragTo;
        node.in = at - (*dragTo - at);
        node.smooth = true;
    }
    contour.nodes.push_back(node);
}
