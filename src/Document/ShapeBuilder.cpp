#include "Document/ShapeBuilder.h"
#include <QLineF>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using Cubic = std::array<QPointF, 4>;

// Faces smaller than this, in square points, are slivers where outlines coincide.
constexpr double minimumArea = 0.05;
// How far a flattened vertex may sit from the curve it came from.
constexpr double onCurve = 0.05;

QPointF cubicAt(const Cubic &c, double t)
{
    const double u = 1 - t;
    return u * u * u * c[0] + 3 * u * u * t * c[1] + 3 * u * t * t * c[2] + t * t * t * c[3];
}

std::pair<Cubic, Cubic> split(const Cubic &c, double t)
{
    const QPointF ab = c[0] + (c[1] - c[0]) * t, bc = c[1] + (c[2] - c[1]) * t, cd = c[2] + (c[3] - c[2]) * t;
    const QPointF abc = ab + (bc - ab) * t, bcd = bc + (cd - bc) * t;
    const QPointF mid = abc + (bcd - abc) * t;
    return {Cubic{c[0], ab, abc, mid}, Cubic{mid, bcd, cd, c[3]}};
}

// The part of `c` from `a` to `b`; backwards when `b` comes first.
Cubic subCubic(const Cubic &c, double a, double b)
{
    if (a > b) {
        const Cubic forward = subCubic(c, b, a);
        return {forward[3], forward[2], forward[1], forward[0]};
    }
    const Cubic right = a > 1e-12 ? split(c, a).second : c;
    const double u = a < 1 - 1e-12 ? (b - a) / (1 - a) : 1;
    return u < 1 - 1e-12 ? split(right, u).first : right;
}

bool isLine(const Cubic &c)
{
    return c[1] == c[0] && c[2] == c[3];
}

QRectF hull(const Cubic &c)
{
    return QPolygonF({c[0], c[1], c[2], c[3]}).boundingRect();
}

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double shoelace(const QPolygonF &polygon)
{
    double sum = 0;
    for (qsizetype index = 0; index < polygon.size(); ++index) {
        const QPointF a = polygon[index], b = polygon[(index + 1) % polygon.size()];
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(sum) / 2;
}

double perimeter(const QPolygonF &polygon)
{
    double sum = 0;
    for (qsizetype index = 0; index < polygon.size(); ++index)
        sum += distance(polygon[index], polygon[(index + 1) % polygon.size()]);
    return sum;
}

// Enough to tell a real piece from an empty result; holes count as area here.
double roughArea(const QPainterPath &path)
{
    double sum = 0;
    for (const QPolygonF &ring : path.toSubpathPolygons())
        sum += shoelace(ring);
    return sum;
}

// Most of `inner`'s edge midpoints lie inside `outer`: rings that only touch still nest.
bool nests(const QPolygonF &outer, const QPolygonF &inner)
{
    int in = 0, out = 0;
    const qsizetype step = std::max<qsizetype>(1, inner.size() / 16);
    for (qsizetype index = 0; index < inner.size(); index += step) {
        const QPointF middle = (inner[index] + inner[(index + 1) % inner.size()]) / 2;
        outer.containsPoint(middle, Qt::OddEvenFill) ? ++in : ++out;
    }
    return in > out;
}

struct Piece {
    QPainterPath area;
    double size = 0;
};

// The connected pieces of an area: each outer ring with the holes directly inside it.
std::vector<Piece> pieces(const QPainterPath &area)
{
    std::vector<QPolygonF> rings;
    std::vector<double> sizes;
    for (QPolygonF ring : area.simplified().toSubpathPolygons()) {
        if (ring.size() > 1 && ring.front() == ring.back())
            ring.removeLast();
        const double size = shoelace(ring);
        if (ring.size() >= 3 && size > 1e-9) {
            rings.push_back(ring);
            sizes.push_back(size);
        }
    }
    const size_t count = rings.size();
    std::vector<int> depth(count, 0);
    for (size_t index = 0; index < count; ++index) {
        for (size_t other = 0; other < count; ++other) {
            if (other != index && sizes[other] > sizes[index] && nests(rings[other], rings[index]))
                ++depth[index];
        }
    }
    std::vector<Piece> result;
    std::vector<int> pieceOf(count, -1);
    for (size_t index = 0; index < count; ++index) {
        if (depth[index] % 2)
            continue;
        Piece piece;
        piece.area.setFillRule(Qt::OddEvenFill);
        piece.area.addPolygon(rings[index]);
        piece.area.closeSubpath();
        piece.size = sizes[index];
        pieceOf[index] = int(result.size());
        result.push_back(piece);
    }
    for (size_t index = 0; index < count; ++index) {
        if (depth[index] % 2 == 0)
            continue;
        // The hole belongs to the smallest outer ring one level up that holds it.
        int owner = -1;
        for (size_t other = 0; other < count; ++other) {
            if (pieceOf[other] >= 0 && depth[other] == depth[index] - 1 && sizes[other] > sizes[index] && nests(rings[other], rings[index])
                && (owner < 0 || sizes[other] < sizes[size_t(owner)]))
                owner = int(other);
        }
        if (owner < 0)
            continue;
        Piece &piece = result[size_t(pieceOf[size_t(owner)])];
        piece.area.addPolygon(rings[index]);
        piece.area.closeSubpath();
        piece.size -= sizes[index];
    }
    // Hairline slivers where two outlines run together.
    std::erase_if(result, [](const Piece &piece) {
        double edge = 0;
        for (const QPolygonF &ring : piece.area.toSubpathPolygons())
            edge += perimeter(ring);
        return piece.size < minimumArea || piece.size < edge * 0.01;
    });
    return result;
}

std::vector<Cubic> cubics(const Contour &contour)
{
    std::vector<Cubic> result;
    const size_t count = contour.nodes.size();
    const size_t segments = contour.closed ? count : (count ? count - 1 : 0);
    for (size_t index = 0; index < segments && count > 1; ++index) {
        const PathNode &from = contour.nodes[index], &to = contour.nodes[(index + 1) % count];
        result.push_back({from.anchor, from.out, to.in, to.anchor});
    }
    return result;
}

// Where `point` lies on `c`, if within `tolerance`.
std::optional<double> parameter(const Cubic &c, QPointF point, double tolerance)
{
    if (!hull(c).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point))
        return std::nullopt;
    if (isLine(c)) {
        const QPointF d = c[3] - c[0];
        const double length = QPointF::dotProduct(d, d);
        const double t = length > 0 ? std::clamp(QPointF::dotProduct(point - c[0], d) / length, 0.0, 1.0) : 0;
        return distance(point, c[0] + d * t) <= tolerance ? std::optional(t) : std::nullopt;
    }
    constexpr int samples = 32;
    double best = 0, bestDistance = std::numeric_limits<double>::infinity();
    for (int step = 0; step <= samples; ++step) {
        const double t = double(step) / samples;
        if (const double d = distance(point, cubicAt(c, t)); d < bestDistance) {
            bestDistance = d;
            best = t;
        }
    }
    double low = std::max(0.0, best - 1.0 / samples), high = std::min(1.0, best + 1.0 / samples);
    for (int step = 0; step < 40; ++step) {
        const double a = low + (high - low) / 3, b = high - (high - low) / 3;
        if (distance(point, cubicAt(c, a)) < distance(point, cubicAt(c, b)))
            high = b;
        else
            low = a;
    }
    const double t = (low + high) / 2;
    return distance(point, cubicAt(c, t)) <= tolerance ? std::optional(t) : std::nullopt;
}

// The open contour and a far loop on one side of it: a region whose edge, near the artwork, is the contour.
QPainterPath sideOf(const Contour &cutter, QPointF center, double reach)
{
    VectorPath open;
    open.contours = {cutter};
    QPainterPath side = open.painterPath();
    const QPointF start = cutter.nodes.front().anchor, end = cutter.nodes.back().anchor;
    auto away = [&](QPointF point) {
        const QPointF d = point - center;
        const double length = std::hypot(d.x(), d.y());
        return length > 1e-9 ? d / length : QPointF(1, 0);
    };
    const QPointF farEnd = end + away(end) * reach, farStart = start + away(start) * reach;
    const double radius = std::max(distance(farEnd, center), distance(farStart, center));
    const double from = std::atan2(farEnd.y() - center.y(), farEnd.x() - center.x());
    const double to = std::atan2(farStart.y() - center.y(), farStart.x() - center.x());
    const double sweep = std::fmod(to - from + 4 * std::numbers::pi, 2 * std::numbers::pi);
    side.lineTo(farEnd);
    for (int step = 0; step <= 48; ++step) {
        const double angle = from + sweep * step / 48;
        side.lineTo(center + QPointF(std::cos(angle), std::sin(angle)) * radius);
    }
    side.lineTo(farStart);
    side.lineTo(start);
    side.closeSubpath();
    return side;
}

// The contour carried on past each end along its tangent, so an end on an outline still cuts it.
Contour extended(Contour contour, double by)
{
    const size_t count = contour.nodes.size();
    const PathNode first = contour.nodes.front(), last = contour.nodes.back();
    auto unit = [](QPointF d) {
        const double length = std::hypot(d.x(), d.y());
        return length > 1e-9 ? d / length : QPointF();
    };
    const QPointF outEnd = unit(last.anchor - (last.hasIn() ? last.in : contour.nodes[count - 2].anchor));
    const QPointF outStart = unit(first.anchor - (first.hasOut() ? first.out : contour.nodes[1].anchor));
    contour.nodes.push_back(PathNode(last.anchor + outEnd * by));
    contour.nodes.insert(contour.nodes.begin(), PathNode(first.anchor + outStart * by));
    return contour;
}

// An open contour cut at positions `segment + t`, into pieces.
std::vector<Contour> cutContour(const Contour &contour, std::vector<double> cuts)
{
    std::sort(cuts.begin(), cuts.end());
    const std::vector<Cubic> segments = cubics(contour);
    std::vector<Contour> result;
    Contour current;
    current.nodes.push_back(PathNode(contour.nodes.front().anchor));
    size_t next = 0;
    for (size_t k = 0; k < segments.size(); ++k) {
        Cubic rest = segments[k];
        double done = 0;
        while (next < cuts.size() && cuts[next] < double(k) + 1 - 1e-6) {
            const double t = cuts[next++] - double(k);
            if (t <= done + 1e-6)
                continue;
            const auto [left, right] = split(rest, (t - done) / (1 - done));
            current.nodes.back().out = left[1];
            current.nodes.push_back(PathNode(left[3], left[2], left[3]));
            result.push_back(current);
            current = {};
            current.nodes.push_back(PathNode(left[3]));
            rest = right;
            done = t;
        }
        current.nodes.back().out = rest[1];
        current.nodes.push_back(PathNode(rest[3], rest[2], rest[3], contour.nodes[k + 1].smooth));
        // A cut on an anchor ends the piece there.
        if (next < cuts.size() && cuts[next] < double(k) + 1 + 1e-6 && k + 1 < segments.size()) {
            ++next;
            result.push_back(current);
            current = {};
            current.nodes.push_back(PathNode(rest[3]));
        }
    }
    result.push_back(current);
    return result;
}
}

bool ShapeBuilder::bindsArea(const Contour &contour, const ShapeBuilderOptions &options)
{
    if (contour.closed)
        return true;
    return options.gapDetection && contour.nodes.size() > 2
        && distance(contour.nodes.front().anchor, contour.nodes.back().anchor) <= std::max(0.0, options.gapLength);
}

std::vector<QUuid> ShapeBuilder::sourcesOf(const VectorDocument &document, const std::vector<QUuid> &leaves)
{
    std::vector<QUuid> result;
    for (const QUuid &id : leaves) {
        const VectorObject *object = document.find(id);
        if (object && object->kind == ObjectKind::path && !object->path.isEmpty() && document.isEffectivelyVisible(id)
            && !document.isEffectivelyLocked(id) && std::find(result.begin(), result.end(), id) == result.end())
            result.push_back(id);
    }
    std::sort(result.begin(), result.end(), [&](const QUuid &a, const QUuid &b) { return document.indexOf(a) < document.indexOf(b); });
    return result;
}

ShapeBuilder::Arrangement ShapeBuilder::arrange(const VectorDocument &document, const std::vector<QUuid> &leaves, const ShapeBuilderOptions &options)
{
    Arrangement result;
    result.sources = sourcesOf(document, leaves);
    if (int(result.sources.size()) > maximumSources) {
        result.truncated = true;
        return result;
    }
    struct Cutter {
        int source;
        int contour;
        Contour path;
    };
    std::vector<QPainterPath> areas(result.sources.size());
    std::vector<Cutter> cutters;
    QRectF everything;
    for (size_t index = 0; index < result.sources.size(); ++index) {
        const VectorObject &object = *document.find(result.sources[index]);
        VectorPath closed;
        closed.fillRule = object.path.fillRule;
        for (size_t c = 0; c < object.path.contours.size(); ++c) {
            Contour contour = object.path.contours[c];
            if (contour.nodes.size() < 2)
                continue;
            if (bindsArea(contour, options)) {
                contour.closed = true;
                closed.contours.push_back(contour);
            } else {
                cutters.push_back({int(index), int(c), contour});
            }
            for (const Cubic &segment : cubics(contour))
                result.segments.push_back(segment);
        }
        if (!closed.contours.empty()) {
            areas[index] = closed.painterPath();
            everything = everything.united(areas[index].boundingRect());
        }
    }
    // Faces: each closed path splits the cells it overlaps into inside and outside.
    struct Cell {
        QPainterPath area;
        std::vector<int> owners;
    };
    std::vector<Cell> cells;
    QPainterPath covered;
    for (size_t index = 0; index < areas.size(); ++index) {
        const QPainterPath &shape = areas[index];
        if (shape.isEmpty())
            continue;
        const QRectF bounds = shape.boundingRect();
        std::vector<Cell> next;
        for (Cell &cell : cells) {
            if (!cell.area.boundingRect().intersects(bounds)) {
                next.push_back(std::move(cell));
                continue;
            }
            QPainterPath inside = cell.area.intersected(shape);
            if (roughArea(inside) < minimumArea) {
                next.push_back(std::move(cell));
                continue;
            }
            QPainterPath outside = cell.area.subtracted(shape);
            if (roughArea(outside) >= minimumArea)
                next.push_back({outside, cell.owners});
            cell.owners.push_back(int(index));
            next.push_back({inside, cell.owners});
        }
        QPainterPath fresh = covered.isEmpty() ? shape : shape.subtracted(covered);
        if (roughArea(fresh) >= minimumArea)
            next.push_back({fresh, {int(index)}});
        covered = covered.isEmpty() ? shape : covered.united(shape);
        cells = std::move(next);
        if (int(cells.size()) > maximumRegions) {
            result.truncated = true;
            return result;
        }
    }
    // Open paths cut the cells they cross from side to side.
    const double reach = std::hypot(everything.width(), everything.height()) * 4 + 1000;
    const double overshoot = 0.01 + (options.gapDetection ? std::max(0.0, options.gapLength) : 0);
    for (const Cutter &cutter : cutters) {
        const Contour blade = extended(cutter.path, overshoot);
        const QPainterPath side = sideOf(blade, everything.center(), reach);
        const QRectF span = VectorPath{{blade}, Qt::WindingFill}.bounds();
        std::vector<Cell> next;
        for (Cell &cell : cells) {
            const QRectF bounds = cell.area.boundingRect();
            if (!bounds.intersects(span.adjusted(-1, -1, 1, 1)) || cell.area.contains(blade.nodes.front().anchor)
                || cell.area.contains(blade.nodes.back().anchor)) {
                next.push_back(std::move(cell));
                continue;
            }
            QPainterPath one = cell.area.intersected(side), other = cell.area.subtracted(side);
            if (roughArea(one) < minimumArea || roughArea(other) < minimumArea) {
                next.push_back(std::move(cell));
                continue;
            }
            next.push_back({one, cell.owners});
            next.push_back({other, cell.owners});
        }
        cells = std::move(next);
        if (int(cells.size()) > maximumRegions) {
            result.truncated = true;
            return result;
        }
    }
    for (const Cell &cell : cells) {
        for (Piece &piece : pieces(cell.area))
            result.regions.push_back({std::move(piece.area), cell.owners, piece.size});
    }
    if (int(result.regions.size()) > maximumRegions) {
        result.truncated = true;
        result.regions.clear();
        return result;
    }
    // Edges: each open path in pieces between the outlines it crosses.
    auto signature = [&](QPointF point) {
        std::vector<bool> inside(areas.size());
        for (size_t index = 0; index < areas.size(); ++index)
            inside[index] = !areas[index].isEmpty() && areas[index].contains(point);
        return inside;
    };
    for (const Cutter &cutter : cutters) {
        const std::vector<Cubic> segments = cubics(cutter.path);
        std::vector<double> cuts;
        for (size_t k = 0; k < segments.size(); ++k) {
            constexpr int samples = 24;
            double previous = 0;
            std::vector<bool> was = signature(segments[k][0]);
            for (int step = 1; step <= samples; ++step) {
                const double t = double(step) / samples;
                const std::vector<bool> now = signature(cubicAt(segments[k], t));
                if (now != was) {
                    double low = previous, high = t;
                    for (int iteration = 0; iteration < 30; ++iteration) {
                        const double middle = (low + high) / 2;
                        (signature(cubicAt(segments[k], middle)) == was ? low : high) = middle;
                    }
                    cuts.push_back(double(k) + (low + high) / 2);
                }
                was = now;
                previous = t;
            }
        }
        int piece = 0;
        for (Contour &part : cutContour(cutter.path, cuts))
            result.edges.push_back({cutter.source, cutter.contour, piece++, std::move(part)});
    }
    return result;
}

std::optional<int> ShapeBuilder::Arrangement::regionAt(QPointF point) const
{
    std::optional<int> best;
    for (size_t index = 0; index < regions.size(); ++index) {
        const Region &region = regions[index];
        if (region.area.boundingRect().contains(point) && region.area.contains(point) && (!best || region.size < regions[size_t(*best)].size))
            best = int(index);
    }
    return best;
}

std::optional<int> ShapeBuilder::Arrangement::edgeAt(QPointF point, double tolerance) const
{
    std::optional<int> best;
    double bestDistance = tolerance;
    for (size_t index = 0; index < edges.size(); ++index) {
        const VectorPath path{{edges[index].path}, Qt::WindingFill};
        if (!path.bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point))
            continue;
        if (const double d = path.distanceToOutline(point); d <= bestDistance) {
            bestDistance = d;
            best = int(index);
        }
    }
    return best;
}

void ShapeBuilder::Arrangement::touchAlong(QPointF from, QPointF to, double tolerance, std::vector<int> &touchedRegions, std::vector<int> &touchedEdges) const
{
    const double length = distance(from, to);
    const int steps = std::clamp(int(std::ceil(length / std::max(tolerance / 2, 0.25))), 1, 2000);
    auto add = [](std::vector<int> &list, std::optional<int> value) {
        if (value && std::find(list.begin(), list.end(), *value) == list.end())
            list.push_back(*value);
    };
    for (int step = 0; step <= steps; ++step) {
        const QPointF point = from + (to - from) * (double(step) / steps);
        add(touchedRegions, regionAt(point));
        add(touchedEdges, edgeAt(point, tolerance));
    }
}

void ShapeBuilder::Arrangement::touchIn(const QRectF &rect, std::vector<int> &touchedRegions, std::vector<int> &touchedEdges) const
{
    const QRectF area = rect.normalized();
    for (size_t index = 0; index < regions.size(); ++index) {
        if (regions[index].area.intersects(area) && std::find(touchedRegions.begin(), touchedRegions.end(), int(index)) == touchedRegions.end())
            touchedRegions.push_back(int(index));
    }
    for (size_t index = 0; index < edges.size(); ++index) {
        bool inside = false;
        for (const Cubic &segment : cubics(edges[index].path)) {
            for (int step = 0; step <= 32 && !inside; ++step)
                inside = area.contains(cubicAt(segment, step / 32.0));
        }
        if (inside && std::find(touchedEdges.begin(), touchedEdges.end(), int(index)) == touchedEdges.end())
            touchedEdges.push_back(int(index));
    }
}

VectorPath ShapeBuilder::Arrangement::shape(const QPainterPath &area) const
{
    VectorPath result;
    result.fillRule = Qt::OddEvenFill;
    std::vector<QRectF> hulls;
    hulls.reserve(segments.size());
    for (const Cubic &segment : segments)
        hulls.push_back(hull(segment).adjusted(-onCurve, -onCurve, onCurve, onCurve));
    for (QPolygonF ring : area.toSubpathPolygons()) {
        if (ring.size() > 1 && ring.front() == ring.back())
            ring.removeLast();
        QPolygonF points;
        for (const QPointF point : ring) {
            if (points.isEmpty() || distance(points.back(), point) > 1e-9)
                points << point;
        }
        if (points.size() > 1 && distance(points.front(), points.back()) <= 1e-9)
            points.removeLast();
        const qsizetype count = points.size();
        if (count < 3)
            continue;
        // Where each vertex lies on the sources' segments.
        std::vector<std::vector<std::pair<int, double>>> on(static_cast<size_t>(count));
        for (qsizetype index = 0; index < count; ++index) {
            for (size_t s = 0; s < segments.size(); ++s) {
                if (!hulls[s].contains(points[index]))
                    continue;
                if (const std::optional<double> t = parameter(segments[s], points[index], onCurve))
                    on[size_t(index)].push_back({int(s), *t});
            }
        }
        // Each edge follows the segment both its ends lie on and its middle stays near.
        struct Label {
            int segment = -1;
            double from = 0, to = 0;
        };
        std::vector<Label> labels(static_cast<size_t>(count));
        for (qsizetype index = 0; index < count; ++index) {
            const qsizetype next = (index + 1) % count;
            const QPointF middle = (points[index] + points[next]) / 2;
            const double chord = distance(points[index], points[next]);
            double best = std::numeric_limits<double>::infinity();
            for (const auto &[s, a] : on[size_t(index)]) {
                for (const auto &[t, b] : on[size_t(next)]) {
                    // A flattened curve moves in small steps; a line may span its segment.
                    if (t != s || (std::abs(b - a) >= 0.5 && !isLine(segments[size_t(s)])))
                        continue;
                    const double off = isLine(segments[size_t(s)]) ? 0 : distance(middle, cubicAt(segments[size_t(s)], (a + b) / 2));
                    if (off <= chord * 0.25 + onCurve && off < best) {
                        best = off;
                        labels[size_t(index)] = {s, a, b};
                    }
                }
            }
        }
        auto sameRun = [&](const Label &a, const Label &b) {
            return a.segment >= 0 && a.segment == b.segment && (a.to - a.from) * (b.to - b.from) > 0 && std::abs(a.to - b.from) < 1e-6;
        };
        qsizetype start = 0;
        for (qsizetype index = 0; index < count; ++index) {
            if (!sameRun(labels[size_t((index + count - 1) % count)], labels[size_t(index)])) {
                start = index;
                break;
            }
        }
        Contour contour;
        contour.closed = true;
        contour.nodes.push_back(PathNode(points[start]));
        for (qsizetype done = 0; done < count;) {
            const qsizetype index = (start + done) % count;
            const Label label = labels[size_t(index)];
            qsizetype last = index;
            ++done;
            double to = label.to;
            while (done < count && sameRun(labels[size_t(last)], labels[size_t((last + 1) % count)])) {
                last = (last + 1) % count;
                to = labels[size_t(last)].to;
                ++done;
            }
            const QPointF end = points[(last + 1) % count];
            if (label.segment < 0 || isLine(segments[size_t(label.segment)])) {
                contour.nodes.push_back(PathNode(end));
            } else {
                const Cubic piece = subCubic(segments[size_t(label.segment)], label.from, to);
                contour.nodes.back().out = contour.nodes.back().anchor + (piece[1] - piece[0]);
                contour.nodes.push_back(PathNode(end, end + (piece[2] - piece[3]), end));
            }
        }
        // The last node came back to the first.
        const PathNode closing = contour.nodes.back();
        contour.nodes.pop_back();
        contour.nodes.front().in = contour.nodes.front().anchor + (closing.in - closing.anchor);
        if (contour.nodes.size() >= 2)
            result.contours.push_back(std::move(contour));
    }
    return result;
}
