#pragma once
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <optional>
#include <vector>

// One anchor of a Bézier contour. Handles are absolute document points;
// a handle equal to its anchor is retracted (a corner with a straight side).
struct PathNode {
    QPointF anchor;
    QPointF in;
    QPointF out;
    // Moving one handle mirrors the other's direction.
    bool smooth = false;

    PathNode() = default;
    explicit PathNode(QPointF at) : anchor(at), in(at), out(at) {}
    PathNode(QPointF at, QPointF in, QPointF out, bool smooth = false) : anchor(at), in(in), out(out), smooth(smooth) {}
    bool hasIn() const { return in != anchor; }
    bool hasOut() const { return out != anchor; }
    void translate(QPointF by);
    friend bool operator==(const PathNode &, const PathNode &) = default;
};

struct Contour {
    std::vector<PathNode> nodes;
    bool closed = false;
    friend bool operator==(const Contour &, const Contour &) = default;
};

// Which part of a node a point handle drags.
enum class NodePart { anchor, in, out };

struct NodeRef {
    int contour = 0;
    int node = 0;
    friend bool operator==(const NodeRef &, const NodeRef &) = default;
    friend auto operator<=>(const NodeRef &, const NodeRef &) = default;
};

// A compound Bézier path: every contour is filled together under `fillRule`.
struct VectorPath {
    std::vector<Contour> contours;
    Qt::FillRule fillRule = Qt::WindingFill;

    bool isEmpty() const;
    int nodeCount() const;
    QPainterPath painterPath() const;
    // Qt's cubic and line elements, read back into nodes.
    static VectorPath fromPainterPath(const QPainterPath &path);
    QRectF bounds() const;
    VectorPath transformed(const QTransform &transform) const;
    const PathNode *node(NodeRef ref) const;
    PathNode *node(NodeRef ref);
    // Nearest anchor or handle within `tolerance`, handles first.
    std::optional<std::pair<NodeRef, NodePart>> hitNode(QPointF point, double tolerance, bool withHandles) const;
    // Distance from `point` to the outline, sampled along each segment.
    double distanceToOutline(QPointF point) const;
    // The nearest segment within `tolerance`: it runs from node `from` to the next, at `t`.
    struct SegmentHit {
        NodeRef from;
        double t = 0;
        double distance = 0;
    };
    std::optional<SegmentHit> hitSegment(QPointF point, double tolerance) const;
    // Adds an anchor at `t` on the segment after `from` without changing the shape; returns it.
    NodeRef splitSegment(NodeRef from, double t);
    friend bool operator==(const VectorPath &, const VectorPath &) = default;
};

// The same contour drawn the other way: nodes reversed, each node's handles swapped.
Contour reversed(const Contour &contour);

// Moves a handle; a smooth node turns its other handle to stay collinear.
void moveHandle(PathNode &node, NodePart part, QPointF to, bool breakSmooth = false);
