#include "Document/VectorDocument.h"
#include <QLineF>
#include <cmath>

namespace {
// The handle length that makes a cubic follow a quarter circle.
constexpr double kappa = 0.5522847498307936;

const std::array<std::pair<CornerStyle, const char *>, 3> styleNames{{
    {CornerStyle::round, "round"}, {CornerStyle::inverted, "inverted"}, {CornerStyle::chamfer, "chamfer"},
}};

double length(QPointF vector)
{
    return std::hypot(vector.x(), vector.y());
}
}

QString rawValue(CornerStyle style)
{
    return QString::fromLatin1(styleNames.at(size_t(style)).second);
}

std::optional<CornerStyle> cornerStyle(const QString &rawValue)
{
    for (const auto &[style, name] : styleNames) {
        if (rawValue == QLatin1String(name))
            return style;
    }
    return std::nullopt;
}

double LiveRectangle::effectiveRadius(int corner) const
{
    const QRectF box = rect.normalized();
    return std::clamp(radii[size_t(corner)], 0.0, std::min(box.width(), box.height()) / 2);
}

VectorPath LiveRectangle::path() const
{
    const QRectF box = rect.normalized();
    const std::array<QPointF, 4> corners{box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    Contour contour;
    contour.closed = true;
    for (int index = 0; index < 4; ++index) {
        const QPointF at = corners[size_t(index)];
        const QPointF previous = corners[size_t((index + 3) % 4)], next = corners[size_t((index + 1) % 4)];
        const double r = effectiveRadius(index);
        if (r <= 0) {
            contour.nodes.emplace_back(at);
            continue;
        }
        // Walking clockwise: the edge coming in, and the one going out.
        const QPointF in = (at - previous) / std::max(1e-12, length(at - previous));
        const QPointF out = (next - at) / std::max(1e-12, length(next - at));
        PathNode enter(at - in * r), leave(at + out * r);
        switch (styles[size_t(index)]) {
        case CornerStyle::round:
            enter.out = enter.anchor + in * (r * kappa);
            leave.in = leave.anchor - out * (r * kappa);
            break;
        case CornerStyle::inverted:
            // A quarter circle about the corner itself, bowing inward.
            enter.out = enter.anchor + out * (r * kappa);
            leave.in = leave.anchor - in * (r * kappa);
            break;
        case CornerStyle::chamfer:
            break;
        }
        contour.nodes.push_back(enter);
        contour.nodes.push_back(leave);
    }
    // Start on the top edge, as the Rectangle tool's paths do.
    if (effectiveRadius(0) > 0)
        std::rotate(contour.nodes.begin(), contour.nodes.begin() + 1, contour.nodes.end());
    VectorPath result;
    result.contours.push_back(std::move(contour));
    return placement.isIdentity() ? result : result.transformed(placement);
}

QPointF LiveRectangle::corner(int index) const
{
    const QRectF box = rect.normalized();
    const std::array<QPointF, 4> corners{box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
    return placement.map(corners[size_t(index)]);
}

QPointF LiveRectangle::inward(int index) const
{
    static const std::array<QPointF, 4> diagonals{QPointF(1, 1), QPointF(-1, 1), QPointF(-1, -1), QPointF(1, -1)};
    const QPointF direction = placement.map(diagonals[size_t(index)]) - placement.map(QPointF(0, 0));
    return direction / std::max(1e-12, length(direction));
}

std::optional<LiveRectangle> LiveRectangle::transformed(const QTransform &transform, bool scaleCorners) const
{
    const QRectF box = rect.normalized();
    const QTransform mapping = placement * transform;
    const QPointF origin = mapping.map(box.topLeft());
    const QPointF across = mapping.map(box.topRight()) - origin, down = mapping.map(box.bottomLeft()) - origin;
    const double width = length(across), height = length(down);
    // Skewed or flattened, it's no longer a rectangle.
    if (width < 1e-9 || height < 1e-9 || std::abs(QPointF::dotProduct(across, down)) > 1e-6 * width * height)
        return std::nullopt;
    LiveRectangle result = *this;
    result.rect = QRectF(0, 0, width, height);
    result.placement = QTransform(across.x() / width, across.y() / width, down.x() / height, down.y() / height, origin.x(), origin.y());
    // Upright and unflipped, the frame is just the box.
    if (std::abs(result.placement.m11() - 1) < 1e-12 && std::abs(result.placement.m22() - 1) < 1e-12 && std::abs(result.placement.m12()) < 1e-12
        && std::abs(result.placement.m21()) < 1e-12) {
        result.rect = QRectF(origin, QSizeF(width, height));
        result.placement = QTransform();
    }
    if (scaleCorners) {
        const double factor = std::sqrt(std::abs(transform.determinant()));
        for (double &radius : result.radii)
            radius *= factor;
    }
    return result;
}

LiveRectangle LiveRectangle::upright() const
{
    if (placement.type() != QTransform::TxTranslate)
        return *this;
    LiveRectangle result = *this;
    result.rect = rect.normalized().translated(placement.dx(), placement.dy());
    result.placement = QTransform();
    return result;
}

const LiveRectangle *VectorObject::liveShape() const
{
    // A frame's box is always live.
    if (kind == ObjectKind::frame && shape)
        return &*shape;
    if (kind != ObjectKind::path || !shape)
        return nullptr;
    const VectorPath made = shape->path();
    return made.contours == path.contours ? &*shape : nullptr;
}

void VectorDocument::expandEditedShapes()
{
    for (VectorObject &object : objects) {
        if (object.shape && !object.liveShape())
            object.shape.reset();
    }
}
