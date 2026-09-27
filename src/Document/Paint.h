#pragma once
#include <QBrush>
#include <QColor>
#include <QPen>
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

enum class PaintKind { none, solid, linearGradient, radialGradient };
QString rawValue(PaintKind kind);
std::optional<PaintKind> paintKind(const QString &rawValue);

struct GradientStop {
    double offset = 0;
    QColor color;
    friend bool operator==(const GradientStop &, const GradientStop &) = default;
};

// A fill or stroke colour. Gradient ends are fractions of the object's bounds,
// so a gradient follows its object when the object is moved or scaled.
struct Paint {
    PaintKind kind = PaintKind::none;
    QColor color = Qt::black;
    std::vector<GradientStop> stops;
    QPointF start{0, 0.5};
    QPointF end{1, 0.5};

    static Paint none() { return {}; }
    static Paint solid(const QColor &color);
    // Black to white, left to right, as a new gradient starts.
    static Paint linear(const QColor &from, const QColor &to);
    static Paint radial(const QColor &from, const QColor &to);
    bool isVisible() const;
    QBrush brush(const QRectF &bounds) const;
    // The colour a swatch shows: solid, or the first stop.
    QColor swatch() const;
    friend bool operator==(const Paint &, const Paint &) = default;
};

enum class StrokeAlignment { center, inside, outside };

struct StrokeStyle {
    Paint paint = Paint::solid(Qt::black);
    double width = 1;
    Qt::PenCapStyle cap = Qt::FlatCap;
    Qt::PenJoinStyle join = Qt::MiterJoin;
    double miterLimit = 10;
    // Dash and gap lengths in points; empty is solid.
    std::vector<double> dashes;

    bool isVisible() const { return paint.isVisible() && width > 0; }
    QPen pen(const QRectF &bounds) const;
    friend bool operator==(const StrokeStyle &, const StrokeStyle &) = default;
};

QString rawValue(Qt::PenCapStyle cap);
QString rawValue(Qt::PenJoinStyle join);
Qt::PenCapStyle penCapStyle(const QString &rawValue);
Qt::PenJoinStyle penJoinStyle(const QString &rawValue);
