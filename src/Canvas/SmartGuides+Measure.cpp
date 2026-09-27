#include "Canvas/SmartGuides.h"
#include <QRegularExpression>
#include <cmath>

namespace {
// One axis's gaps: `along` builds a line from one coordinate to another at the cross position.
template <typename Line>
void gaps(double fromMin, double fromMax, double toMin, double toMax, const Line &along, std::vector<QLineF> &lines)
{
    const auto add = [&](double a, double b) {
        if (b - a > 1e-6)
            lines.push_back(along(a, b));
    };
    if (fromMax <= toMin) {
        add(fromMax, toMin);
    } else if (toMax <= fromMin) {
        add(toMax, fromMin);
    } else if (toMin <= fromMin && fromMax <= toMax) {
        // Inside: the space on both sides.
        add(toMin, fromMin);
        add(fromMax, toMax);
    } else if (fromMin <= toMin && toMax <= fromMax) {
        add(fromMin, toMin);
        add(toMax, fromMax);
    }
}

// Where a line across the other axis sits: the middle of the overlap, else the selection's middle.
double crossing(double fromMin, double fromMax, double toMin, double toMax)
{
    const double low = std::max(fromMin, toMin), high = std::min(fromMax, toMax);
    return low <= high ? (low + high) / 2 : (fromMin + fromMax) / 2;
}
}

std::vector<QLineF> SmartGuides::distances(const QRectF &from, const QRectF &to)
{
    std::vector<QLineF> lines;
    const QRectF a = from.normalized(), b = to.normalized();
    const double y = crossing(a.top(), a.bottom(), b.top(), b.bottom());
    gaps(a.left(), a.right(), b.left(), b.right(), [&](double x1, double x2) { return QLineF(x1, y, x2, y); }, lines);
    const double x = crossing(a.left(), a.right(), b.left(), b.right());
    gaps(a.top(), a.bottom(), b.top(), b.bottom(), [&](double y1, double y2) { return QLineF(x, y1, x, y2); }, lines);
    return lines;
}

QString SmartGuides::label(double points)
{
    const double rounded = std::round(points * 100) / 100;
    return QString::number(rounded == 0 ? 0.0 : rounded, 'f', 2).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
}
