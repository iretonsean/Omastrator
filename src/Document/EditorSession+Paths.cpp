#include "Document/EditorSession.h"
#include <QLineF>
#include <algorithm>
#include <limits>

namespace {
// Endpoints closer than this merge into one anchor when joined.
constexpr double mergeDistance = 0.01;

// One end of an open contour.
struct End {
    QUuid object;
    int contour = 0;
    bool atStart = false;
    friend bool operator==(const End &, const End &) = default;
};

const PathNode &endNode(const VectorDocument &document, const End &end)
{
    const Contour &contour = document.find(end.object)->path.contours[size_t(end.contour)];
    return end.atStart ? contour.nodes.front() : contour.nodes.back();
}

// Joins two ends: one contour closes, or two become one open contour in the first end's object.
void join(VectorDocument &document, const End &a, const End &b)
{
    VectorObject &first = *document.find(a.object);
    if (a.object == b.object && a.contour == b.contour) {
        Contour &contour = first.path.contours[size_t(a.contour)];
        if (contour.nodes.size() > 2 && QLineF(contour.nodes.front().anchor, contour.nodes.back().anchor).length() < mergeDistance) {
            contour.nodes.front().in = contour.nodes.back().in;
            contour.nodes.pop_back();
        } else {
            contour.nodes.back().out = contour.nodes.back().anchor;
            contour.nodes.front().in = contour.nodes.front().anchor;
        }
        contour.closed = true;
        return;
    }
    // The first runs up to its joined end; the second runs on from its own.
    Contour head = first.path.contours[size_t(a.contour)];
    if (a.atStart)
        head = reversed(head);
    Contour tail = document.find(b.object)->path.contours[size_t(b.contour)];
    if (!b.atStart)
        tail = reversed(tail);
    if (QLineF(head.nodes.back().anchor, tail.nodes.front().anchor).length() < mergeDistance) {
        head.nodes.back().out = tail.nodes.front().out;
        head.nodes.back().smooth = false;
        tail.nodes.erase(tail.nodes.begin());
    } else {
        head.nodes.back().out = head.nodes.back().anchor;
        tail.nodes.front().in = tail.nodes.front().anchor;
    }
    head.nodes.insert(head.nodes.end(), tail.nodes.begin(), tail.nodes.end());
    first.path.contours[size_t(a.contour)] = std::move(head);
    VectorObject &second = *document.find(b.object);
    second.path.contours.erase(second.path.contours.begin() + b.contour);
    if (second.path.isEmpty())
        document.remove({b.object});
}

// The two ends a join takes, when the picked anchors name exactly two.
std::vector<End> pickedEnds(const VectorDocument &document, const std::vector<EditorSession::PickedNode> &picked)
{
    std::vector<End> ends;
    for (const EditorSession::PickedNode &node : picked) {
        const VectorObject *object = document.find(node.object);
        if (!object || object->kind != ObjectKind::path || !object->path.node(node.node) || document.isEffectivelyLocked(node.object))
            continue;
        const Contour &contour = object->path.contours[size_t(node.node.contour)];
        if (contour.closed || contour.nodes.size() < 2)
            continue;
        if (node.node.node == 0)
            ends.push_back({node.object, node.node.contour, true});
        else if (node.node.node + 1 == int(contour.nodes.size()))
            ends.push_back({node.object, node.node.contour, false});
    }
    return ends;
}

std::vector<std::pair<QUuid, int>> openContours(const VectorDocument &document, const std::vector<QUuid> &ids)
{
    std::vector<std::pair<QUuid, int>> found;
    for (const QUuid &id : ids) {
        const VectorObject *object = document.find(id);
        if (!object || object->kind != ObjectKind::path || document.isEffectivelyLocked(id))
            continue;
        for (int c = 0; c < int(object->path.contours.size()); ++c) {
            const Contour &contour = object->path.contours[size_t(c)];
            if (!contour.closed && contour.nodes.size() >= 2)
                found.push_back({id, c});
        }
    }
    return found;
}
}

bool EditorSession::canJoin() const
{
    if (!m_document)
        return false;
    if (m_tool == Tool::directSelect && !m_pickedNodes.empty() && pickedEnds(*m_document, m_pickedNodes).size() == 2)
        return true;
    return !openContours(*m_document, selectedLeaves()).empty();
}

void EditorSession::joinPaths()
{
    if (!canJoin())
        return;
    const std::vector<End> picked = m_tool == Tool::directSelect ? pickedEnds(*m_document, m_pickedNodes) : std::vector<End>();
    const std::vector<QUuid> leaves = selectedLeaves();
    edit(QStringLiteral("Join"), [&](VectorDocument &document) {
        m_pickedNodes.clear();
        if (picked.size() == 2) {
            join(document, picked[0], picked[1]);
            m_selection = {picked[0].object};
            return;
        }
        // Whole paths: one open contour closes; several join nearest end to nearest end until one is left.
        if (const auto open = openContours(document, leaves); open.size() == 1) {
            join(document, {open[0].first, open[0].second, true}, {open[0].first, open[0].second, false});
            return;
        }
        for (;;) {
            const auto open = openContours(document, leaves);
            if (open.size() < 2)
                break;
            std::optional<std::pair<End, End>> nearest;
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i < open.size(); ++i) {
                for (size_t j = i + 1; j < open.size(); ++j) {
                    for (const bool startA : {true, false}) {
                        for (const bool startB : {true, false}) {
                            const End a{open[i].first, open[i].second, startA}, b{open[j].first, open[j].second, startB};
                            const double distance = QLineF(endNode(document, a).anchor, endNode(document, b).anchor).length();
                            if (distance < best) {
                                best = distance;
                                nearest = std::pair(a, b);
                            }
                        }
                    }
                }
            }
            join(document, nearest->first, nearest->second);
        }
        std::erase_if(m_selection, [&](const QUuid &id) { return !document.find(id); });
    });
}

bool EditorSession::canAverage() const
{
    if (!m_document)
        return false;
    if (m_tool == Tool::directSelect && m_pickedNodes.size() >= 2)
        return true;
    int anchors = 0;
    for (const QUuid &id : selectedLeaves()) {
        if (m_document->find(id)->kind == ObjectKind::path)
            anchors += m_document->find(id)->path.nodeCount();
    }
    return anchors >= 2;
}

void EditorSession::averagePoints(Qt::Orientations along)
{
    if (!canAverage() || !along)
        return;
    std::vector<PickedNode> nodes;
    if (m_tool == Tool::directSelect && m_pickedNodes.size() >= 2) {
        nodes = m_pickedNodes;
    } else {
        for (const QUuid &id : selectedLeaves()) {
            const VectorObject *object = m_document->find(id);
            if (object->kind != ObjectKind::path)
                continue;
            for (int c = 0; c < int(object->path.contours.size()); ++c) {
                for (int n = 0; n < int(object->path.contours[size_t(c)].nodes.size()); ++n)
                    nodes.push_back({id, {c, n}});
            }
        }
    }
    QPointF sum;
    int count = 0;
    for (const PickedNode &picked : nodes) {
        if (const PathNode *node = m_document->find(picked.object)->path.node(picked.node)) {
            sum += node->anchor;
            ++count;
        }
    }
    const QPointF mean = sum / std::max(1, count);
    edit(QStringLiteral("Average"), [&](VectorDocument &document) {
        for (const PickedNode &picked : nodes) {
            VectorObject *object = document.find(picked.object);
            PathNode *node = object && !document.isEffectivelyLocked(picked.object) ? object->path.node(picked.node) : nullptr;
            if (!node)
                continue;
            // Horizontal lines them up along one y; vertical along one x.
            node->translate(QPointF(along.testFlag(Qt::Vertical) ? mean.x() - node->anchor.x() : 0,
                                    along.testFlag(Qt::Horizontal) ? mean.y() - node->anchor.y() : 0));
        }
    });
}

bool EditorSession::cutPath(const QUuid &id, NodeRef from, std::optional<double> t)
{
    const VectorObject *object = m_document ? m_document->find(id) : nullptr;
    if (!object || object->kind != ObjectKind::path || m_document->isEffectivelyLocked(id) || !object->path.node(from))
        return false;
    VectorPath path = object->path;
    NodeRef at = from;
    // Near a segment's ends, the cut is at that anchor.
    if (t && *t > 1e-3 && *t < 1 - 1e-3)
        at = path.splitSegment(from, *t);
    else if (t && *t >= 1 - 1e-3)
        at = {from.contour, (from.node + 1) % int(path.contours[size_t(from.contour)].nodes.size())};
    Contour &contour = path.contours[size_t(at.contour)];
    const int count = int(contour.nodes.size());
    std::optional<Contour> split;
    if (contour.closed) {
        // Opened at the cut: it starts and ends there.
        std::rotate(contour.nodes.begin(), contour.nodes.begin() + at.node, contour.nodes.end());
        PathNode end = contour.nodes.front();
        end.out = end.anchor;
        contour.nodes.front().in = contour.nodes.front().anchor;
        contour.nodes.front().smooth = end.smooth = false;
        contour.nodes.push_back(end);
        contour.closed = false;
    } else {
        // An open end has nothing to cut.
        if (at.node <= 0 || at.node >= count - 1)
            return false;
        Contour rest;
        rest.nodes.assign(contour.nodes.begin() + at.node, contour.nodes.end());
        rest.nodes.front().in = rest.nodes.front().anchor;
        rest.nodes.front().smooth = false;
        contour.nodes.resize(size_t(at.node + 1));
        contour.nodes.back().out = contour.nodes.back().anchor;
        contour.nodes.back().smooth = false;
        split = std::move(rest);
    }
    edit(QStringLiteral("Cut Path"), [&](VectorDocument &document) {
        VectorObject &target = *document.find(id);
        target.path = path;
        m_pickedNodes.clear();
        m_selection = {id};
        if (!split)
            return;
        // The far part becomes its own path, just above.
        VectorObject piece = target;
        piece.id = QUuid::createUuid();
        piece.shape.reset();
        piece.path.contours = {*split};
        const QUuid pieceID = piece.id;
        document.insert(std::move(piece), *target.parentID, id);
        m_selection = {id, pieceID};
    });
    return true;
}

void EditorSession::reversePaths()
{
    if (!m_document)
        return;
    // With anchors picked, only their contours turn around.
    const bool picked = m_tool == Tool::directSelect && !m_pickedNodes.empty();
    const std::vector<PickedNode> nodes = m_pickedNodes;
    const std::vector<QUuid> leaves = selectedLeaves();
    edit(QStringLiteral("Reverse Path Direction"), [&](VectorDocument &document) {
        for (const QUuid &id : leaves) {
            VectorObject *object = document.find(id);
            if (!object || object->kind != ObjectKind::path || document.isEffectivelyLocked(id))
                continue;
            for (int c = 0; c < int(object->path.contours.size()); ++c) {
                const bool chosen = !picked || std::any_of(nodes.begin(), nodes.end(), [&](const PickedNode &node) {
                    return node.object == id && node.node.contour == c;
                });
                if (chosen)
                    object->path.contours[size_t(c)] = reversed(object->path.contours[size_t(c)]);
            }
        }
        m_pickedNodes.clear();
    });
}

std::vector<QUuid> EditorSession::selectedCompoundPaths() const
{
    std::vector<QUuid> found;
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *object = m_document->find(id);
        if (object->kind == ObjectKind::path && object->path.contours.size() > 1)
            found.push_back(id);
    }
    return found;
}

void EditorSession::setFillRuleOfSelection(Qt::FillRule rule)
{
    const std::vector<QUuid> paths = selectedCompoundPaths();
    if (paths.empty())
        return;
    edit(QStringLiteral("Fill Rule"), [&](VectorDocument &document) {
        for (const QUuid &id : paths) {
            if (!document.isEffectivelyLocked(id))
                document.find(id)->path.fillRule = rule;
        }
    });
}

// Live corners ------------------------------------------------------------------

std::vector<QUuid> EditorSession::selectedShapes() const
{
    std::vector<QUuid> found;
    for (const QUuid &id : selectedLeaves()) {
        if (m_document->find(id)->liveShape() && !m_document->isEffectivelyLocked(id))
            found.push_back(id);
    }
    return found;
}

void EditorSession::reshape(VectorObject &object, const LiveRectangle &shape)
{
    const Qt::FillRule rule = object.path.fillRule;
    object.shape = shape;
    object.path = shape.path();
    object.path.fillRule = rule;
}

void EditorSession::setCornerRadius(double radius, std::optional<int> corner)
{
    const std::vector<QUuid> shapes = selectedShapes();
    if (shapes.empty())
        return;
    edit(QStringLiteral("Corner Radius"), [&](VectorDocument &document) {
        for (const QUuid &id : shapes) {
            VectorObject &object = *document.find(id);
            LiveRectangle shape = *object.shape;
            for (int index = 0; index < 4; ++index) {
                if (!corner || *corner == index)
                    shape.radii[size_t(index)] = std::max(0.0, radius);
            }
            reshape(object, shape);
        }
    });
}

void EditorSession::setCornerStyle(CornerStyle style, std::optional<int> corner)
{
    const std::vector<QUuid> shapes = selectedShapes();
    if (shapes.empty())
        return;
    edit(QStringLiteral("Corner Style"), [&](VectorDocument &document) {
        for (const QUuid &id : shapes) {
            VectorObject &object = *document.find(id);
            LiveRectangle shape = *object.shape;
            for (int index = 0; index < 4; ++index) {
                if (!corner || *corner == index)
                    shape.styles[size_t(index)] = style;
            }
            reshape(object, shape);
        }
    });
}
