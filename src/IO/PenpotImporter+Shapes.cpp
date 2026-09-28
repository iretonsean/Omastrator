#include "IO/PenpotImporterParts.h"
#include <QJsonArray>

namespace PenpotImport {

VectorPath contentGeometry(const QJsonArray &content)
{
    VectorPath path;
    Contour contour;
    for (const QJsonValue &value : content) {
        const QJsonObject segment = value.toObject();
        const QString command = segment.value(QStringLiteral("command")).toString();
        const QJsonObject params = segment.value(QStringLiteral("params")).toObject();
        if (command == QLatin1String("move-to")) {
            if (!contour.nodes.empty()) {
                path.contours.push_back(std::move(contour));
                contour = Contour();
            }
            contour.nodes.emplace_back(QPointF(params.value(QStringLiteral("x")).toDouble(), params.value(QStringLiteral("y")).toDouble()));
        } else if (command == QLatin1String("line-to")) {
            contour.nodes.emplace_back(QPointF(params.value(QStringLiteral("x")).toDouble(), params.value(QStringLiteral("y")).toDouble()));
        } else if (command == QLatin1String("curve-to")) {
            // c1 is the outgoing handle off the point already on the contour;
            // c2 is the incoming handle for the new point this segment adds.
            if (!contour.nodes.empty() && params.contains(QStringLiteral("c1x")) && params.contains(QStringLiteral("c1y"))) {
                contour.nodes.back().out =
                    QPointF(params.value(QStringLiteral("c1x")).toDouble(), params.value(QStringLiteral("c1y")).toDouble());
            }
            const QPointF anchor(params.value(QStringLiteral("x")).toDouble(), params.value(QStringLiteral("y")).toDouble());
            const QPointF in = (params.contains(QStringLiteral("c2x")) && params.contains(QStringLiteral("c2y")))
                                    ? QPointF(params.value(QStringLiteral("c2x")).toDouble(), params.value(QStringLiteral("c2y")).toDouble())
                                    : anchor;
            contour.nodes.emplace_back(anchor, in, anchor);
        } else if (command == QLatin1String("close-path")) {
            contour.closed = true;
        }
    }
    if (!contour.nodes.empty())
        path.contours.push_back(std::move(contour));
    return path;
}

LiveRectangle rectangleShape(const QJsonObject &shape, const QSizeF &size)
{
    LiveRectangle result;
    result.rect = QRectF(0, 0, size.width(), size.height());
    // Top left, clockwise: the same order as LiveRectangle::radii.
    result.radii = {shape.value(QStringLiteral("r1")).toDouble(0), shape.value(QStringLiteral("r2")).toDouble(0),
                     shape.value(QStringLiteral("r3")).toDouble(0), shape.value(QStringLiteral("r4")).toDouble(0)};
    return result;
}

}
