#include "Document/StrokeGeometry.h"
#include <QLineF>
#include <QPainterPathStroker>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace {
// One line or cubic of a subpath.
struct Segment {
    QPointF p0, c1, c2, p1;
    bool curve = false;
    double length = 0;
};

struct Subpath {
    std::vector<Segment> segments;
    bool closed = false;
    double length() const
    {
        double total = 0;
        for (const Segment &segment : segments)
            total += segment.length;
        return total;
    }
};

double cubicLength(QPointF p0, QPointF c1, QPointF c2, QPointF p1)
{
    QPainterPath path(p0);
    path.cubicTo(c1, c2, p1);
    return path.length();
}

Segment line(QPointF from, QPointF to)
{
    return {from, from, to, to, false, QLineF(from, to).length()};
}

Segment cubic(QPointF p0, QPointF c1, QPointF c2, QPointF p1)
{
    return {p0, c1, c2, p1, true, cubicLength(p0, c1, c2, p1)};
}

std::vector<Subpath> subpaths(const QPainterPath &path)
{
    std::vector<Subpath> all;
    QPointF start, at;
    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            all.push_back({});
            start = at = element;
            break;
        case QPainterPath::LineToElement: {
            if (all.empty())
                all.push_back({});
            const Segment segment = line(at, element);
            if (segment.length > 1e-9)
                all.back().segments.push_back(segment);
            at = element;
            break;
        }
        case QPainterPath::CurveToElement: {
            if (all.empty() || index + 2 >= path.elementCount())
                break;
            const QPointF c2 = path.elementAt(index + 1), end = path.elementAt(index + 2);
            const Segment segment = cubic(at, element, c2, end);
            if (segment.length > 1e-9)
                all.back().segments.push_back(segment);
            at = end;
            index += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
        if (!all.empty() && !all.back().segments.empty())
            all.back().closed = QLineF(at, start).length() < 1e-6;
    }
    std::erase_if(all, [](const Subpath &each) { return each.segments.empty(); });
    return all;
}

QPointF lerp(QPointF a, QPointF b, double t)
{
    return a + (b - a) * t;
}

// De Casteljau: the part of a segment from 0 to t, and from t to 1.
std::pair<Segment, Segment> split(const Segment &segment, double t)
{
    if (!segment.curve) {
        const QPointF at = lerp(segment.p0, segment.p1, t);
        return {line(segment.p0, at), line(at, segment.p1)};
    }
    const QPointF a = lerp(segment.p0, segment.c1, t), b = lerp(segment.c1, segment.c2, t), c = lerp(segment.c2, segment.p1, t);
    const QPointF d = lerp(a, b, t), e = lerp(b, c, t);
    const QPointF at = lerp(d, e, t);
    return {cubic(segment.p0, a, d, at), cubic(at, e, c, segment.p1)};
}

// The curve parameter `distance` along a segment.
double parameterAt(const Segment &segment, double distance)
{
    if (segment.length <= 0)
        return 0;
    const double target = std::clamp(distance / segment.length, 0.0, 1.0);
    if (!segment.curve || target <= 0 || target >= 1)
        return target;
    double low = 0, high = 1;
    for (int step = 0; step < 18; ++step) {
        const double middle = (low + high) / 2;
        if (split(segment, middle).first.length < distance)
            low = middle;
        else
            high = middle;
    }
    return (low + high) / 2;
}

// The stretch of a subpath between two distances along it, from <= to.
std::vector<Segment> extract(const Subpath &subpath, double from, double to)
{
    std::vector<Segment> out;
    double at = 0;
    for (const Segment &segment : subpath.segments) {
        const double begin = at, end = at + segment.length;
        at = end;
        if (end <= from || begin >= to)
            continue;
        Segment part = segment;
        if (to < end)
            part = split(part, parameterAt(part, to - begin)).first;
        if (from > begin)
            part = split(part, parameterAt(part, from - begin)).second;
        if (part.length > 1e-9)
            out.push_back(part);
    }
    return out;
}

void append(QPainterPath &path, const std::vector<Segment> &segments)
{
    if (segments.empty())
        return;
    path.moveTo(segments.front().p0);
    for (const Segment &segment : segments) {
        if (segment.curve)
            path.cubicTo(segment.c1, segment.c2, segment.p1);
        else
            path.lineTo(segment.p1);
    }
}

QPointF unit(QPointF v)
{
    const double length = std::hypot(v.x(), v.y());
    return length > 1e-12 ? v / length : QPointF();
}

QPointF startDirection(const Segment &segment)
{
    for (QPointF next : {segment.c1, segment.c2, segment.p1}) {
        if (QLineF(segment.p0, next).length() > 1e-9)
            return unit(next - segment.p0);
    }
    return {1, 0};
}

QPointF endDirection(const Segment &segment)
{
    for (QPointF previous : {segment.c2, segment.c1, segment.p0}) {
        if (QLineF(previous, segment.p1).length() > 1e-9)
            return unit(segment.p1 - previous);
    }
    return {1, 0};
}

bool isCorner(const Segment &into, const Segment &out)
{
    const QPointF a = endDirection(into), b = startDirection(out);
    return a.x() * b.x() + a.y() * b.y() < std::cos(2.0 * M_PI / 180);
}

double headSize(const StrokeStyle &stroke)
{
    return std::max(stroke.width, 0.5) * std::max(stroke.arrowScale, 1.0) / 100;
}

// How much of the line a triangle head hides, measured back from the tip.
double trimFor(Arrowhead head, const StrokeStyle &stroke)
{
    return head == Arrowhead::triangle ? headSize(stroke) * 2.25 : 0;
}

// A head with its tip at the origin, pointing along +x.
QPainterPath headShape(Arrowhead head, double size)
{
    QPainterPath shape;
    shape.setFillRule(Qt::WindingFill);
    switch (head) {
    case Arrowhead::none:
        break;
    case Arrowhead::arrow: {
        QPainterPath chevron(QPointF(-4 * size, -2.2 * size));
        chevron.lineTo(0, 0);
        chevron.lineTo(-4 * size, 2.2 * size);
        QPainterPathStroker stroker;
        stroker.setWidth(size);
        stroker.setJoinStyle(Qt::MiterJoin);
        stroker.setCapStyle(Qt::FlatCap);
        shape = stroker.createStroke(chevron);
        break;
    }
    case Arrowhead::triangle:
        shape.addPolygon(QPolygonF({QPointF(0, 0), QPointF(-4.5 * size, -2.25 * size), QPointF(-4.5 * size, 2.25 * size), QPointF(0, 0)}));
        shape.closeSubpath();
        break;
    case Arrowhead::circle:
        shape.addEllipse(QPointF(0, 0), 2 * size, 2 * size);
        break;
    case Arrowhead::square:
        shape.addRect(QRectF(-1.75 * size, -1.75 * size, 3.5 * size, 3.5 * size));
        break;
    case Arrowhead::bar:
        shape.addRect(QRectF(-size / 2, -2.5 * size, size, 5 * size));
        break;
    }
    return shape;
}

QPainterPath placed(const QPainterPath &shape, QPointF tip, QPointF direction)
{
    QTransform transform;
    transform.translate(tip.x(), tip.y());
    transform.rotateRadians(std::atan2(direction.y(), direction.x()));
    return transform.map(shape);
}

void configure(QPainterPathStroker &stroker, const StrokeStyle &stroke, double width)
{
    stroker.setWidth(width);
    stroker.setCapStyle(stroke.cap);
    stroker.setJoinStyle(stroke.join);
    stroker.setMiterLimit(stroke.miterLimit);
}
}

namespace StrokeGeometry {
bool isClosed(const QPainterPath &path)
{
    const std::vector<Subpath> all = subpaths(path);
    return !all.empty() && std::all_of(all.begin(), all.end(), [](const Subpath &each) { return each.closed; });
}

QList<qreal> dashPattern(const std::vector<double> &dashes, double width)
{
    QList<qreal> pattern;
    for (double length : dashes)
        pattern << std::max(0.01, length / std::max(width, 1e-9));
    if (pattern.size() % 2)
        pattern << pattern;
    return pattern;
}

QRectF extent(const QPainterPath &path, const StrokeStyle &stroke)
{
    QRectF bounds = path.boundingRect();
    if (!stroke.isVisible())
        return bounds;
    const bool aligned = stroke.alignment != StrokeAlignment::center && isClosed(path);
    if (!aligned || stroke.alignment == StrokeAlignment::outside) {
        QPainterPathStroker stroker;
        configure(stroker, stroke, aligned ? stroke.width * 2 : stroke.width);
        bounds = bounds.united(stroker.createStroke(path).boundingRect());
    }
    const QPainterPath ends = heads(path, stroke);
    return ends.isEmpty() ? bounds : bounds.united(ends.boundingRect());
}

QPainterPath body(const QPainterPath &path, const StrokeStyle &stroke)
{
    const double startTrim = trimFor(stroke.startArrow, stroke), endTrim = trimFor(stroke.endArrow, stroke);
    if (startTrim <= 0 && endTrim <= 0)
        return path;
    QPainterPath out;
    out.setFillRule(path.fillRule());
    for (const Subpath &subpath : subpaths(path)) {
        if (subpath.closed) {
            append(out, subpath.segments);
            continue;
        }
        const double length = subpath.length();
        if (startTrim + endTrim < length)
            append(out, extract(subpath, startTrim, length - endTrim));
    }
    return out;
}

QPainterPath heads(const QPainterPath &path, const StrokeStyle &stroke)
{
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    if (stroke.startArrow == Arrowhead::none && stroke.endArrow == Arrowhead::none)
        return out;
    const double size = headSize(stroke);
    for (const Subpath &subpath : subpaths(path)) {
        if (subpath.closed)
            continue;
        if (stroke.startArrow != Arrowhead::none)
            out.addPath(placed(headShape(stroke.startArrow, size), subpath.segments.front().p0, -startDirection(subpath.segments.front())));
        if (stroke.endArrow != Arrowhead::none)
            out.addPath(placed(headShape(stroke.endArrow, size), subpath.segments.back().p1, endDirection(subpath.segments.back())));
    }
    return out;
}

QPainterPath alignedDashes(const QPainterPath &path, const std::vector<double> &dashes)
{
    std::vector<double> pattern = dashes;
    if (pattern.size() % 2) {
        const std::vector<double> again = pattern;
        pattern.insert(pattern.end(), again.begin(), again.end());
    }
    double period = 0;
    for (double length : pattern)
        period += length;
    if (pattern.empty() || period <= 1e-9)
        return path;
    QPainterPath out;
    for (const Subpath &subpath : subpaths(path)) {
        const double length = subpath.length();
        // Corners as distances along the subpath; an open one's ends count.
        std::vector<double> corners;
        double at = 0;
        for (size_t index = 0; index < subpath.segments.size(); ++index) {
            const bool first = index == 0;
            if (first ? (!subpath.closed || isCorner(subpath.segments.back(), subpath.segments.front()))
                      : isCorner(subpath.segments[index - 1], subpath.segments[index]))
                corners.push_back(at);
            at += subpath.segments[index].length;
        }
        const bool smoothLoop = subpath.closed && corners.empty();
        if (!subpath.closed)
            corners.push_back(length);
        else if (smoothLoop)
            corners = {0, length};
        else
            corners.push_back(corners.front() + length);
        // Dashes along each run between corners, in unwrapped distances.
        std::vector<std::pair<double, double>> pieces;
        for (size_t run = 0; run + 1 < corners.size(); ++run) {
            const double from = corners[run], span = corners[run + 1] - from;
            if (span <= 1e-9)
                continue;
            const double repeats = std::max(1.0, std::round(span / period));
            const double stretch = span / (repeats * period);
            // Half the first dash before the corner: every corner gets a whole dash centred on it.
            const double shift = smoothLoop ? 0 : pattern.front() * stretch / 2;
            for (int repeat = 0; repeat <= int(repeats); ++repeat) {
                double offset = repeat * period * stretch - shift;
                for (size_t index = 0; index < pattern.size(); ++index) {
                    const double size = pattern[index] * stretch;
                    if (index % 2 == 0) {
                        const double begin = std::max(0.0, offset), end = std::min(span, offset + size);
                        if (end > begin + 1e-9 || (size <= 1e-9 && offset >= 0 && offset <= span))
                            pieces.push_back({from + begin, from + std::max(begin, end)});
                    }
                    offset += size;
                }
            }
        }
        // Halves meeting at a corner join into one dash that turns it.
        std::vector<std::pair<double, double>> joined;
        for (const auto &piece : pieces) {
            if (!joined.empty() && piece.first - joined.back().second < 1e-6)
                joined.back().second = std::max(joined.back().second, piece.second);
            else
                joined.push_back(piece);
        }
        if (subpath.closed && joined.size() > 1 && joined.back().second >= corners.back() - 1e-6
            && joined.front().first <= corners.front() + 1e-6) {
            joined.front().first = joined.back().first - length;
            joined.pop_back();
        }
        for (auto [begin, end] : joined) {
            // Back into [0, length): a piece may wrap past the start of a closed subpath.
            while (begin >= length - 1e-9 && subpath.closed) {
                begin -= length;
                end -= length;
            }
            std::vector<Segment> segments;
            if (begin < 0 && subpath.closed) {
                segments = extract(subpath, begin + length, length);
                const std::vector<Segment> rest = extract(subpath, 0, end);
                segments.insert(segments.end(), rest.begin(), rest.end());
            } else if (end > length && subpath.closed) {
                segments = extract(subpath, begin, length);
                const std::vector<Segment> rest = extract(subpath, 0, end - length);
                segments.insert(segments.end(), rest.begin(), rest.end());
            } else {
                segments = extract(subpath, begin, end);
            }
            if (segments.empty() && end - begin <= 1e-9) {
                // A zero-length dash: a dot for round caps.
                const std::vector<Segment> tiny = extract(subpath, std::max(0.0, begin - 1e-4), std::min(length, begin + 1e-4));
                segments = tiny;
            }
            append(out, segments);
        }
    }
    return out;
}

QPainterPath area(const QPainterPath &path, const StrokeStyle &stroke)
{
    const bool aligned = stroke.alignment != StrokeAlignment::center && isClosed(path);
    const double width = aligned ? stroke.width * 2 : stroke.width;
    QPainterPath line = body(path, stroke);
    QPainterPathStroker outliner;
    configure(outliner, stroke, width);
    if (!stroke.dashes.empty() && width > 0) {
        if (stroke.alignDashes)
            line = alignedDashes(line, stroke.dashes);
        else
            outliner.setDashPattern(dashPattern(stroke.dashes, width));
    }
    QPainterPath covered = outliner.createStroke(line);
    covered.setFillRule(Qt::WindingFill);
    if (aligned) {
        QPainterPath shape = path;
        covered = stroke.alignment == StrokeAlignment::inside ? covered.intersected(shape) : covered.subtracted(shape);
    }
    const QPainterPath ends = heads(path, stroke);
    if (!ends.isEmpty())
        covered.addPath(ends);
    covered.setFillRule(Qt::WindingFill);
    return covered;
}
}
