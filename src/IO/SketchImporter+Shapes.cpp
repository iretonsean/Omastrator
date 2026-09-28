#include "Document/PathOperations.h"
#include "IO/SketchImporterParts.h"
#include <QJsonArray>

namespace SketchImport {

QPointF fractionPoint(const QString &text)
{
    QString inner = text.trimmed();
    if (inner.startsWith(QLatin1Char('{')) && inner.endsWith(QLatin1Char('}')))
        inner = inner.mid(1, inner.size() - 2);
    const QStringList parts = inner.split(QLatin1Char(','));
    if (parts.size() != 2)
        return QPointF(0, 0);
    return QPointF(parts[0].trimmed().toDouble(), parts[1].trimmed().toDouble());
}

LiveRectangle rectangleShape(const QJsonObject &layer, const QSizeF &size)
{
    LiveRectangle shape;
    shape.rect = QRectF(0, 0, size.width(), size.height());
    const QJsonArray points = layer.value(QStringLiteral("points")).toArray();
    const double fixedRadius = layer.value(QStringLiteral("fixedRadius")).toDouble(0);
    bool anyPointRadius = false;
    // Sketch's own point order (top left, top right, bottom right, bottom
    // left) matches LiveRectangle::radii's exactly.
    if (points.size() == 4) {
        for (int i = 0; i < 4; ++i) {
            const double radius = points[i].toObject().value(QStringLiteral("cornerRadius")).toDouble(0);
            shape.radii[size_t(i)] = radius;
            anyPointRadius = anyPointRadius || radius > 0;
        }
    }
    if (!anyPointRadius && fixedRadius > 0)
        shape.radii = {fixedRadius, fixedRadius, fixedRadius, fixedRadius};
    return shape;
}

VectorPath shapePathGeometry(const QJsonObject &layer, const QSizeF &size, QStringList &warnings)
{
    const auto scaled = [&](const QJsonObject &point, const char *key) {
        const QPointF fraction = fractionPoint(point.value(QLatin1String(key)).toString());
        return QPointF(fraction.x() * size.width(), fraction.y() * size.height());
    };
    Contour contour;
    contour.closed = layer.value(QStringLiteral("isClosed")).toBool(true);
    bool roundedCorner = false;
    for (const QJsonValue &value : layer.value(QStringLiteral("points")).toArray()) {
        const QJsonObject point = value.toObject();
        const QPointF anchor = scaled(point, "point");
        const bool hasIn = point.value(QStringLiteral("hasCurveTo")).toBool(false);
        const bool hasOut = point.value(QStringLiteral("hasCurveFrom")).toBool(false);
        const QPointF in = hasIn ? scaled(point, "curveTo") : anchor;
        const QPointF out = hasOut ? scaled(point, "curveFrom") : anchor;
        roundedCorner = roundedCorner || point.value(QStringLiteral("cornerRadius")).toDouble(0) > 0;
        contour.nodes.emplace_back(anchor, in, out, hasIn && hasOut);
    }
    if (roundedCorner)
        warnings << QStringLiteral("Rounded corners on non-rectangle shapes were left out.");
    VectorPath path;
    path.contours.push_back(std::move(contour));
    return path;
}

VectorPath booleanGroupGeometry(const QJsonArray &children, QStringList &warnings)
{
    // Each child's own place within the group, then folded into the result
    // one at a time by its own booleanOperation (mixed union/subtract/
    // intersect chains are common, so one shared operation, as the app's
    // own combine() takes, isn't enough here).
    std::vector<std::pair<QPainterPath, int>> parts;
    for (const QJsonValue &value : children) {
        const QJsonObject child = value.toObject();
        if (!child.value(QStringLiteral("isVisible")).toBool(true))
            continue;
        const QJsonObject frame = child.value(QStringLiteral("frame")).toObject();
        const QSizeF size(frame.value(QStringLiteral("width")).toDouble(), frame.value(QStringLiteral("height")).toDouble());
        const QString cls = child.value(QStringLiteral("_class")).toString();
        VectorPath local = cls == QLatin1String("rectangle") ? rectangleShape(child, size).path() : shapePathGeometry(child, size, warnings);
        const QTransform offset = QTransform::fromTranslate(frame.value(QStringLiteral("x")).toDouble(), frame.value(QStringLiteral("y")).toDouble());
        parts.push_back({local.transformed(offset).painterPath(), child.value(QStringLiteral("booleanOperation")).toInt(0)});
    }
    if (parts.empty())
        return {};
    QPainterPath result = parts.front().first;
    for (size_t i = 1; i < parts.size(); ++i) {
        BooleanOperation operation = BooleanOperation::unite;
        switch (parts[i].second) {
        case 1: operation = BooleanOperation::minusFront; break;
        case 2: operation = BooleanOperation::intersect; break;
        case 3: operation = BooleanOperation::exclude; break;
        default: operation = BooleanOperation::unite; break;
        }
        result = combine({result, parts[i].first}, operation);
    }
    return VectorPath::fromPainterPath(result);
}

}
