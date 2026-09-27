#include "Canvas/EditorCanvasState.h"
#include <QLineF>
#include <algorithm>

namespace {
constexpr double nodeReach = 6;
}

std::vector<QUuid> EditorCanvas::State::editablePaths() const
{
    std::vector<QUuid> result;
    if (!session.document())
        return result;
    const VectorDocument &document = *session.document();
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = document.find(id);
        if (object && object->kind == ObjectKind::path && !document.isEffectivelyLocked(id) && document.isEffectivelyVisible(id))
            result.push_back(id);
    }
    return result;
}

bool EditorCanvas::State::isPicked(const QUuid &object, NodeRef node) const
{
    const auto &picked = session.pickedNodes();
    return std::find(picked.begin(), picked.end(), EditorSession::PickedNode{object, node}) != picked.end();
}

std::optional<EditorCanvas::State::NodeHit> EditorCanvas::State::nodeAt(QPointF view) const
{
    if (session.tool() != Tool::directSelect)
        return std::nullopt;
    const VectorDocument &document = *session.document();
    std::optional<NodeHit> best;
    double bestDistance = nodeReach;
    const auto consider = [&](QPointF at, const QUuid &id, NodeRef ref, NodePart part, double bias) {
        const double distance = QLineF(view, toView(at)).length() - bias;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = NodeHit{id, ref, part};
        }
    };
    for (const QUuid &id : editablePaths()) {
        const VectorPath &path = document.find(id)->path;
        for (int c = 0; c < int(path.contours.size()); ++c) {
            for (int n = 0; n < int(path.contours[size_t(c)].nodes.size()); ++n) {
                const PathNode &node = path.contours[size_t(c)].nodes[size_t(n)];
                consider(node.anchor, id, {c, n}, NodePart::anchor, 0);
                // Only picked anchors show their handles; those sit on top.
                if (isPicked(id, {c, n})) {
                    if (node.hasIn())
                        consider(node.in, id, {c, n}, NodePart::in, 0.5);
                    if (node.hasOut())
                        consider(node.out, id, {c, n}, NodePart::out, 0.5);
                }
            }
        }
    }
    return best;
}

void EditorCanvas::State::directPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    if (const std::optional<NodeHit> hit = nodeAt(view)) {
        if (hit->part != NodePart::anchor) {
            beginDrag(DragKind::handle, view);
            drag->object = hit->object;
            drag->node = hit->node;
            drag->part = hit->part;
            drag->guides = guidesExcluding({hit->object});
            return;
        }
        std::vector<EditorSession::PickedNode> picked = session.pickedNodes();
        const EditorSession::PickedNode node{hit->object, hit->node};
        const bool was = isPicked(hit->object, hit->node);
        if (shift) {
            if (was)
                std::erase(picked, node);
            else
                picked.push_back(node);
            session.pickNodes(picked);
            if (was)
                return;
        } else if (!was) {
            session.pickNodes({node});
        }
        beginDrag(DragKind::nodes, view);
        drag->object = hit->object;
        drag->grabbed = session.document()->find(hit->object)->path.node(hit->node)->anchor;
        return;
    }
    const std::optional<QUuid> leaf = hitLeaf(document);
    if (!leaf) {
        if (!shift) {
            session.pickNodes({});
            session.deselectAll();
        }
        beginDrag(DragKind::marquee, view);
        drag->additive = shift;
        drag->selectionBefore = session.selection();
        drag->pickedBefore = session.pickedNodes();
        return;
    }
    const VectorObject *object = session.document()->find(*leaf);
    // A click on a path takes its every anchor, so the drag moves it whole.
    std::vector<EditorSession::PickedNode> nodes;
    if (object->kind == ObjectKind::path) {
        for (int c = 0; c < int(object->path.contours.size()); ++c) {
            for (int n = 0; n < int(object->path.contours[size_t(c)].nodes.size()); ++n)
                nodes.push_back({*leaf, {c, n}});
        }
    }
    if (shift) {
        std::vector<QUuid> ids = session.selection();
        if (std::find(ids.begin(), ids.end(), *leaf) == ids.end())
            ids.push_back(*leaf);
        session.select(ids);
        std::vector<EditorSession::PickedNode> picked = session.pickedNodes();
        for (const auto &node : nodes) {
            if (std::find(picked.begin(), picked.end(), node) == picked.end())
                picked.push_back(node);
        }
        session.pickNodes(picked);
    } else if (!session.isSelected(*leaf)) {
        session.select({*leaf});
        session.pickNodes(nodes);
    } else if (object->kind == ObjectKind::path) {
        session.pickNodes(nodes);
    }
    if (object->kind == ObjectKind::path) {
        beginDrag(DragKind::nodes, view);
        drag->object = *leaf;
        drag->grabbed = document;
    } else {
        beginDrag(DragKind::move, view);
        drag->startBounds = session.selectionBounds();
    }
}

void EditorCanvas::State::dragNodes(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started || session.pickedNodes().empty())
        return;
    if (!drag->interacting) {
        std::vector<QUuid> objects;
        for (const auto &picked : session.pickedNodes()) {
            if (std::find(objects.begin(), objects.end(), picked.object) == objects.end())
                objects.push_back(picked.object);
        }
        drag->guides = guidesExcluding(objects);
        session.beginInteraction(QStringLiteral("Move Points"));
        drag->interacting = true;
    }
    const QPointF raw = toDocument(view) - drag->pressDocument;
    const bool constrained = modifiers.testFlag(Qt::ShiftModifier);
    // The grabbed anchor snaps; the others keep their distance to it.
    const QPointF delta = snapPoint(drag->guides, drag->grabbed + raw, drag->grabbed, constrained) - drag->grabbed;
    std::vector<QUuid> done;
    for (const auto &picked : session.pickedNodes()) {
        if (std::find(done.begin(), done.end(), picked.object) != done.end())
            continue;
        done.push_back(picked.object);
        const VectorObject *original = session.originalObject(picked.object);
        if (!original)
            continue;
        VectorObject object = *original;
        for (const auto &other : session.pickedNodes()) {
            if (other.object != picked.object)
                continue;
            if (PathNode *node = object.path.node(other.node))
                node->translate(delta);
        }
        session.previewObject(object);
    }
}

void EditorCanvas::State::dragHandle(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Move Handle"));
        drag->interacting = true;
    }
    const VectorObject *original = session.originalObject(drag->object);
    if (!original || !original->path.node(drag->node))
        return;
    VectorObject object = *original;
    PathNode &node = *object.path.node(drag->node);
    const QPointF start = drag->part == NodePart::in ? node.in : node.out;
    QPointF to = start + toDocument(view) - drag->pressDocument;
    if (modifiers.testFlag(Qt::ShiftModifier))
        to = node.anchor + constrain45(to - node.anchor);
    else
        to = snapPoint(drag->guides, to);
    // Alt breaks the pair: this handle turns alone.
    moveHandle(node, drag->part, to, modifiers.testFlag(Qt::AltModifier));
    session.previewObject(object);
}

void EditorCanvas::State::finishDirectMarquee()
{
    if (!drag->started)
        return;
    const QRectF area = QRectF(drag->pressDocument, toDocument(drag->lastView)).normalized();
    const VectorDocument &document = *session.document();
    std::vector<QUuid> ids = drag->additive ? drag->selectionBefore : std::vector<QUuid>();
    std::vector<EditorSession::PickedNode> picked = drag->additive ? drag->pickedBefore : std::vector<EditorSession::PickedNode>();
    for (const QUuid &id : session.objectsIn(area, true)) {
        const VectorObject *object = document.find(id);
        bool any = object->kind != ObjectKind::path;
        if (object->kind == ObjectKind::path) {
            for (int c = 0; c < int(object->path.contours.size()); ++c) {
                for (int n = 0; n < int(object->path.contours[size_t(c)].nodes.size()); ++n) {
                    if (!area.contains(object->path.contours[size_t(c)].nodes[size_t(n)].anchor))
                        continue;
                    any = true;
                    const EditorSession::PickedNode node{id, {c, n}};
                    if (std::find(picked.begin(), picked.end(), node) == picked.end())
                        picked.push_back(node);
                }
            }
        }
        if (any && std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    session.select(ids);
    session.pickNodes(picked);
}
