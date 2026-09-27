#include "Document/Paint.h"
#include <QLinearGradient>
#include <QLineF>
#include <QRadialGradient>
#include <algorithm>
#include <array>
#include <utility>

namespace {
const std::array<std::pair<PaintKind, const char *>, 4> kindNames{{
    {PaintKind::none, "none"}, {PaintKind::solid, "solid"},
    {PaintKind::linearGradient, "linear"}, {PaintKind::radialGradient, "radial"},
}};

QPointF place(const QRectF &bounds, QPointF fraction)
{
    return {bounds.left() + fraction.x() * bounds.width(), bounds.top() + fraction.y() * bounds.height()};
}
}

QString rawValue(PaintKind kind)
{
    return QString::fromLatin1(kindNames.at(size_t(kind)).second);
}

std::optional<PaintKind> paintKind(const QString &rawValue)
{
    for (const auto &[kind, name] : kindNames) {
        if (rawValue == QLatin1String(name))
            return kind;
    }
    return std::nullopt;
}

Paint Paint::solid(const QColor &color)
{
    Paint paint;
    paint.kind = PaintKind::solid;
    paint.color = color;
    return paint;
}

Paint Paint::linear(const QColor &from, const QColor &to)
{
    Paint paint;
    paint.kind = PaintKind::linearGradient;
    paint.color = from;
    paint.stops = {{0, from}, {1, to}};
    return paint;
}

Paint Paint::radial(const QColor &from, const QColor &to)
{
    Paint paint = linear(from, to);
    paint.kind = PaintKind::radialGradient;
    paint.start = {0.5, 0.5};
    paint.end = {1, 0.5};
    return paint;
}

bool Paint::isVisible() const
{
    if (isHidden)
        return false;
    switch (kind) {
    case PaintKind::none:
        return false;
    case PaintKind::solid:
        return color.alpha() > 0;
    default:
        return !stops.empty();
    }
}

QBrush Paint::brush(const QRectF &bounds) const
{
    switch (kind) {
    case PaintKind::none:
        return Qt::NoBrush;
    case PaintKind::solid:
        return QBrush(color);
    case PaintKind::linearGradient: {
        QLinearGradient gradient(place(bounds, start), place(bounds, end));
        for (const GradientStop &stop : stops)
            gradient.setColorAt(std::clamp(stop.offset, 0.0, 1.0), stop.color);
        return QBrush(gradient);
    }
    case PaintKind::radialGradient: {
        const QPointF center = place(bounds, start);
        QRadialGradient gradient(center, QLineF(center, place(bounds, end)).length());
        for (const GradientStop &stop : stops)
            gradient.setColorAt(std::clamp(stop.offset, 0.0, 1.0), stop.color);
        return QBrush(gradient);
    }
    }
    return Qt::NoBrush;
}

QColor Paint::swatch() const
{
    if (kind == PaintKind::none)
        return Qt::transparent;
    if (kind == PaintKind::solid || stops.empty())
        return color;
    return stops.front().color;
}

Paint Paint::withCompositeOf(const Paint &other) const
{
    Paint paint = *this;
    paint.isHidden = other.isHidden;
    paint.opacity = other.opacity;
    paint.blendMode = other.blendMode;
    return paint;
}

bool StrokeStyle::isPlain() const
{
    return alignment == StrokeAlignment::center && startArrow == Arrowhead::none && endArrow == Arrowhead::none
        && !(alignDashes && !dashes.empty());
}

QString rawValue(StrokeAlignment alignment)
{
    switch (alignment) {
    case StrokeAlignment::inside:
        return QStringLiteral("inside");
    case StrokeAlignment::outside:
        return QStringLiteral("outside");
    default:
        return QStringLiteral("center");
    }
}

StrokeAlignment strokeAlignment(const QString &rawValue)
{
    if (rawValue == QLatin1String("inside"))
        return StrokeAlignment::inside;
    if (rawValue == QLatin1String("outside"))
        return StrokeAlignment::outside;
    return StrokeAlignment::center;
}

namespace {
const std::array<std::pair<Arrowhead, const char *>, 6> headNames{{
    {Arrowhead::none, "none"}, {Arrowhead::arrow, "arrow"}, {Arrowhead::triangle, "triangle"},
    {Arrowhead::circle, "circle"}, {Arrowhead::square, "square"}, {Arrowhead::bar, "bar"},
}};
}

QString rawValue(Arrowhead head)
{
    return QString::fromLatin1(headNames.at(size_t(head)).second);
}

Arrowhead arrowhead(const QString &rawValue)
{
    for (const auto &[head, name] : headNames) {
        if (rawValue == QLatin1String(name))
            return head;
    }
    return Arrowhead::none;
}

QPen StrokeStyle::pen(const QRectF &bounds) const
{
    if (!isVisible())
        return Qt::NoPen;
    QPen pen(paint.brush(bounds), width, Qt::SolidLine, cap, join);
    pen.setMiterLimit(miterLimit);
    if (!dashes.empty() && width > 0) {
        // Qt measures dashes in pen widths.
        QList<qreal> pattern;
        for (double length : dashes)
            pattern << std::max(0.01, length / width);
        if (pattern.size() % 2)
            pattern << pattern;
        pen.setDashPattern(pattern);
    }
    return pen;
}

QString rawValue(Qt::PenCapStyle cap)
{
    switch (cap) {
    case Qt::RoundCap:
        return QStringLiteral("round");
    case Qt::SquareCap:
        return QStringLiteral("square");
    default:
        return QStringLiteral("butt");
    }
}

QString rawValue(Qt::PenJoinStyle join)
{
    switch (join) {
    case Qt::RoundJoin:
        return QStringLiteral("round");
    case Qt::BevelJoin:
        return QStringLiteral("bevel");
    default:
        return QStringLiteral("miter");
    }
}

Qt::PenCapStyle penCapStyle(const QString &rawValue)
{
    if (rawValue == QLatin1String("round"))
        return Qt::RoundCap;
    if (rawValue == QLatin1String("square"))
        return Qt::SquareCap;
    return Qt::FlatCap;
}

Qt::PenJoinStyle penJoinStyle(const QString &rawValue)
{
    if (rawValue == QLatin1String("round"))
        return Qt::RoundJoin;
    if (rawValue == QLatin1String("bevel"))
        return Qt::BevelJoin;
    return Qt::MiterJoin;
}
