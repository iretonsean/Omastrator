#include "Document/VectorPath.h"
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>

void PathNode::translate(QPointF by)
{
    anchor += by;
    in += by;
    out += by;
}

bool VectorPath::isEmpty() const
{
    return std::none_of(contours.begin(), contours.end(), [](const Contour &contour) { return !contour.nodes.empty(); });
}

int VectorPath::nodeCount() const
{
    int count = 0;
    for (const Contour &contour : contours)
        count += int(contour.nodes.size());
    return count;
}

namespace {
void appendSegment(QPainterPath &path, const PathNode &from, const PathNode &to)
{
    if (!from.hasOut() && !to.hasIn())
        path.lineTo(to.anchor);
    else
        path.cubicTo(from.out, to.in, to.anchor);
}

bool near(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6;
}

QPointF cubicPoint(QPointF p0, QPointF p1, QPointF p2, QPointF p3, double t)
{
    const double u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

double segmentDistance(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double length = QPointF::dotProduct(ab, ab);
    const double t = length > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / length, 0.0, 1.0) : 0;
    const QPointF closest = a + t * ab;
    return QLineF(p, closest).length();
}
}

QPainterPath VectorPath::painterPath() const
{
    QPainterPath path;
    path.setFillRule(fillRule);
    for (const Contour &contour : contours) {
        if (contour.nodes.empty())
            continue;
        path.moveTo(contour.nodes.front().anchor);
        for (size_t index = 1; index < contour.nodes.size(); ++index)
            appendSegment(path, contour.nodes[index - 1], contour.nodes[index]);
        if (contour.closed) {
            if (contour.nodes.size() > 1)
                appendSegment(path, contour.nodes.back(), contour.nodes.front());
            path.closeSubpath();
        }
    }
    return path;
}

VectorPath VectorPath::fromPainterPath(const QPainterPath &source)
{
    VectorPath result;
    result.fillRule = source.fillRule();
    Contour current;
    auto finish = [&] {
        if (current.nodes.empty())
            return;
        // Qt closes a subpath with a line back to its start.
        if (current.nodes.size() > 2 && near(current.nodes.back().anchor, current.nodes.front().anchor)) {
            current.nodes.front().in = current.nodes.back().in;
            current.nodes.pop_back();
            current.closed = true;
        }
        result.contours.push_back(std::move(current));
        current = {};
    };
    for (int index = 0; index < source.elementCount(); ++index) {
        const QPainterPath::Element element = source.elementAt(index);
        const QPointF point(element.x, element.y);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            finish();
            current.nodes.emplace_back(point);
            break;
        case QPainterPath::LineToElement:
            if (current.nodes.empty())
                current.nodes.emplace_back(point);
            else if (!near(current.nodes.back().anchor, point))
                current.nodes.emplace_back(point);
            break;
        case QPainterPath::CurveToElement: {
            if (index + 2 >= source.elementCount())
                break;
            const QPainterPath::Element second = source.elementAt(index + 1);
            const QPainterPath::Element end = source.elementAt(index + 2);
            if (current.nodes.empty())
                current.nodes.emplace_back(point);
            current.nodes.back().out = point;
            current.nodes.emplace_back(QPointF(end.x, end.y), QPointF(second.x, second.y), QPointF(end.x, end.y));
            index += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    finish();
    for (Contour &contour : result.contours) {
        for (PathNode &node : contour.nodes) {
            if (node.hasIn() && node.hasOut()) {
                const QPointF a = node.anchor - node.in, b = node.out - node.anchor;
                const double cross = a.x() * b.y() - a.y() * b.x();
                node.smooth = std::abs(cross) < 1e-3 * std::max(1.0, QLineF({}, a).length() * QLineF({}, b).length());
            }
        }
    }
    return result;
}

QRectF VectorPath::bounds() const
{
    return painterPath().boundingRect();
}

VectorPath VectorPath::transformed(const QTransform &transform) const
{
    VectorPath result = *this;
    for (Contour &contour : result.contours) {
        for (PathNode &node : contour.nodes) {
            node.anchor = transform.map(node.anchor);
            node.in = transform.map(node.in);
            node.out = transform.map(node.out);
        }
    }
    return result;
}

const PathNode *VectorPath::node(NodeRef ref) const
{
    if (ref.contour < 0 || ref.contour >= int(contours.size()))
        return nullptr;
    const auto &nodes = contours[size_t(ref.contour)].nodes;
    if (ref.node < 0 || ref.node >= int(nodes.size()))
        return nullptr;
    return &nodes[size_t(ref.node)];
}

PathNode *VectorPath::node(NodeRef ref)
{
    return const_cast<PathNode *>(std::as_const(*this).node(ref));
}

std::optional<std::pair<NodeRef, NodePart>> VectorPath::hitNode(QPointF point, double tolerance, bool withHandles) const
{
    std::optional<std::pair<NodeRef, NodePart>> best;
    double bestDistance = tolerance;
    auto consider = [&](QPointF at, NodeRef ref, NodePart part) {
        const double distance = QLineF(point, at).length();
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = std::pair{ref, part};
        }
    };
    for (int c = 0; c < int(contours.size()); ++c) {
        for (int n = 0; n < int(contours[size_t(c)].nodes.size()); ++n) {
            const PathNode &node = contours[size_t(c)].nodes[size_t(n)];
            consider(node.anchor, {c, n}, NodePart::anchor);
        }
    }
    if (withHandles) {
        // Handles sit on top of anchors, so they win a tie.
        for (int c = 0; c < int(contours.size()); ++c) {
            for (int n = 0; n < int(contours[size_t(c)].nodes.size()); ++n) {
                const PathNode &node = contours[size_t(c)].nodes[size_t(n)];
                if (node.hasIn())
                    consider(node.in, {c, n}, NodePart::in);
                if (node.hasOut())
                    consider(node.out, {c, n}, NodePart::out);
            }
        }
    }
    return best;
}

double VectorPath::distanceToOutline(QPointF point) const
{
    double best = std::numeric_limits<double>::infinity();
    for (const Contour &contour : contours) {
        const size_t count = contour.nodes.size();
        if (count == 1)
            best = std::min(best, QLineF(point, contour.nodes.front().anchor).length());
        const size_t segments = contour.closed ? count : (count ? count - 1 : 0);
        for (size_t index = 0; index < segments; ++index) {
            const PathNode &from = contour.nodes[index];
            const PathNode &to = contour.nodes[(index + 1) % count];
            if (!from.hasOut() && !to.hasIn()) {
                best = std::min(best, segmentDistance(point, from.anchor, to.anchor));
                continue;
            }
            QPointF previous = from.anchor;
            constexpr int steps = 24;
            for (int step = 1; step <= steps; ++step) {
                const QPointF next = cubicPoint(from.anchor, from.out, to.in, to.anchor, double(step) / steps);
                best = std::min(best, segmentDistance(point, previous, next));
                previous = next;
            }
        }
    }
    return best;
}

std::optional<VectorPath::SegmentHit> VectorPath::hitSegment(QPointF point, double tolerance) const
{
    std::optional<SegmentHit> best;
    for (int c = 0; c < int(contours.size()); ++c) {
        const Contour &contour = contours[size_t(c)];
        const size_t count = contour.nodes.size();
        const size_t segments = contour.closed ? count : (count ? count - 1 : 0);
        for (size_t index = 0; index < segments && count >= 2; ++index) {
            const PathNode &from = contour.nodes[index];
            const PathNode &to = contour.nodes[(index + 1) % count];
            const auto at = [&](double t) { return cubicPoint(from.anchor, from.out, to.in, to.anchor, t); };
            // Coarse samples, then a finer look around the nearest.
            constexpr int steps = 48;
            double bestT = 0, bestDistance = std::numeric_limits<double>::infinity();
            for (int step = 0; step <= steps; ++step) {
                const double t = double(step) / steps;
                const double distance = QLineF(point, at(t)).length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestT = t;
                }
            }
            double span = 1.0 / steps;
            for (int round = 0; round < 20; ++round) {
                for (const double t : {std::max(0.0, bestT - span), std::min(1.0, bestT + span)}) {
                    const double distance = QLineF(point, at(t)).length();
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestT = t;
                    }
                }
                span /= 2;
            }
            if (bestDistance <= tolerance && (!best || bestDistance < best->distance))
                best = SegmentHit{{c, int(index)}, bestT, bestDistance};
        }
    }
    return best;
}

NodeRef VectorPath::splitSegment(NodeRef from, double t)
{
    Contour &contour = contours[size_t(from.contour)];
    const size_t count = contour.nodes.size();
    PathNode &a = contour.nodes[size_t(from.node)];
    PathNode &b = contour.nodes[(size_t(from.node) + 1) % count];
    const auto lerp = [t](QPointF p, QPointF q) { return p + (q - p) * t; };
    PathNode middle;
    if (!a.hasOut() && !b.hasIn()) {
        // A straight side stays straight: the new anchor is a corner.
        middle = PathNode(lerp(a.anchor, b.anchor));
    } else {
        // de Casteljau: the two halves trace the original curve exactly.
        const QPointF p01 = lerp(a.anchor, a.out), p12 = lerp(a.out, b.in), p23 = lerp(b.in, b.anchor);
        const QPointF p012 = lerp(p01, p12), p123 = lerp(p12, p23);
        middle = PathNode(lerp(p012, p123), p012, p123, true);
        a.out = p01;
        b.in = p23;
    }
    const int at = from.node + 1;
    contour.nodes.insert(contour.nodes.begin() + at, middle);
    return {from.contour, at};
}

Contour reversed(const Contour &contour)
{
    Contour result = contour;
    std::reverse(result.nodes.begin(), result.nodes.end());
    for (PathNode &node : result.nodes)
        std::swap(node.in, node.out);
    return result;
}

void moveHandle(PathNode &node, NodePart part, QPointF to, bool breakSmooth)
{
    if (part == NodePart::anchor) {
        node.translate(to - node.anchor);
        return;
    }
    QPointF &moved = part == NodePart::in ? node.in : node.out;
    QPointF &other = part == NodePart::in ? node.out : node.in;
    moved = to;
    if (breakSmooth) {
        node.smooth = false;
        return;
    }
    if (node.smooth && other != node.anchor) {
        const QPointF direction = node.anchor - moved;
        const double length = QLineF({}, direction).length();
        const double otherLength = QLineF(node.anchor, other).length();
        if (length > 1e-9)
            other = node.anchor + direction * (otherLength / length);
    }
}
