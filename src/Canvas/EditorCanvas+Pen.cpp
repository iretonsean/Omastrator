#include "Canvas/EditorCanvasState.h"
#include <QLineF>

namespace {
constexpr double closeReach = 7;
// A click this near a selected path's outline adds an anchor there.
constexpr double segmentReach = 4;

// Both handles from a drag: out where the pointer is, in mirrored.
void pullHandles(PathNode &node, QPointF to)
{
    if (QLineF(node.anchor, to).length() < 1e-6) {
        node.in = node.out = node.anchor;
        node.smooth = false;
        return;
    }
    node.out = to;
    node.in = node.anchor - (to - node.anchor);
    node.smooth = true;
}
}

const Contour *EditorCanvas::State::penContour() const
{
    if (!pen || !session.document())
        return nullptr;
    const VectorObject *object = session.document()->find(pen->object);
    if (!object || pen->contour < 0 || pen->contour >= int(object->path.contours.size()))
        return nullptr;
    return &object->path.contours[size_t(pen->contour)];
}

bool EditorCanvas::State::nearPenStart(QPointF view) const
{
    const Contour *contour = penContour();
    return contour && contour->nodes.size() >= 2 && QLineF(view, toView(contour->nodes.front().anchor)).length() <= closeReach;
}

std::optional<EditorCanvas::State::PenTarget> EditorCanvas::State::penEndpointAt(QPointF view, const std::vector<QUuid> &paths) const
{
    const VectorDocument &document = *session.document();
    for (const QUuid &id : paths) {
        const VectorObject *object = document.find(id);
        if (!object || object->kind != ObjectKind::path || document.isEffectivelyLocked(id) || !document.isEffectivelyVisible(id))
            continue;
        for (int c = 0; c < int(object->path.contours.size()); ++c) {
            const Contour &contour = object->path.contours[size_t(c)];
            if (contour.closed || contour.nodes.empty())
                continue;
            for (const int n : {int(contour.nodes.size()) - 1, 0}) {
                if (QLineF(view, toView(contour.nodes[size_t(n)].anchor)).length() <= closeReach)
                    return PenTarget{PenAction::resume, id, {c, n}, 0};
            }
        }
    }
    return std::nullopt;
}

EditorCanvas::State::PenTarget EditorCanvas::State::penTargetAt(QPointF view, Qt::KeyboardModifiers modifiers) const
{
    if (!session.document())
        return {};
    const VectorDocument &document = *session.document();
    // Every unlocked path, topmost first, may be continued or joined.
    std::vector<QUuid> everyPath;
    for (auto object = document.objects.rbegin(); object != document.objects.rend(); ++object) {
        if (object->kind == ObjectKind::path && document.isOnCurrentPage(object->id))
            everyPath.push_back(object->id);
    }
    if (pen && penContour()) {
        if (nearPenStart(view))
            return {PenAction::close, pen->object, {pen->contour, 0}, 0};
        std::erase(everyPath, pen->object);
        if (std::optional<PenTarget> other = penEndpointAt(view, everyPath)) {
            other->action = PenAction::join;
            return *other;
        }
        return {};
    }
    std::vector<QUuid> selected;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = document.find(id);
        if (object && object->kind == ObjectKind::path && !document.isEffectivelyLocked(id))
            selected.push_back(id);
    }
    const double tolerance = reach(closeReach);
    if (modifiers.testFlag(Qt::AltModifier)) {
        for (const QUuid &id : selected) {
            if (const auto hit = document.find(id)->path.hitNode(toDocument(view), tolerance, false))
                return {PenAction::convert, id, hit->first, 0};
        }
    }
    // Selected paths' ends first, then any path's.
    if (std::optional<PenTarget> end = penEndpointAt(view, selected))
        return *end;
    if (std::optional<PenTarget> end = penEndpointAt(view, everyPath))
        return *end;
    for (const QUuid &id : selected) {
        const VectorPath &path = document.find(id)->path;
        if (const auto hit = path.hitNode(toDocument(view), tolerance, false)) {
            // Too few anchors left would be no path.
            if (path.contours[size_t(hit->first.contour)].nodes.size() > 2)
                return {PenAction::removeAnchor, id, hit->first, 0};
            continue;
        }
        if (const auto segment = path.hitSegment(toDocument(view), reach(segmentReach)))
            return {PenAction::addAnchor, id, segment->from, segment->t};
    }
    return {};
}

void EditorCanvas::State::penPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    if (pen && !penContour())
        pen.reset();
    // Something else committed the path so far; later anchors are their own step.
    if (pen && !session.isInteracting())
        session.beginInteraction(QStringLiteral("Draw Path"));
    const PenTarget target = penTargetAt(view, modifiers);
    switch (target.action) {
    case PenAction::close: {
        pen->closing = true;
        VectorObject object = *session.document()->find(pen->object);
        object.path.contours[size_t(pen->contour)].closed = true;
        session.previewObject(object);
        beginDrag(DragKind::pen, view);
        drag->interacting = true;
        return;
    }
    case PenAction::join:
        joinPen(target);
        return;
    case PenAction::resume:
        resumePen(target, view);
        return;
    case PenAction::convert:
        convertPress(target, view);
        return;
    case PenAction::removeAnchor: {
        VectorObject object = *session.document()->find(target.object);
        Contour &contour = object.path.contours[size_t(target.node.contour)];
        contour.nodes.erase(contour.nodes.begin() + target.node.node);
        std::vector<EditorSession::PickedNode> picked = session.pickedNodes();
        std::erase_if(picked, [&](const EditorSession::PickedNode &node) { return node.object == target.object; });
        session.pickNodes(picked);
        session.updateObject(object, QStringLiteral("Delete Anchor Point"));
        return;
    }
    case PenAction::addAnchor: {
        VectorObject object = *session.document()->find(target.object);
        object.path.splitSegment(target.node, target.t);
        std::vector<EditorSession::PickedNode> picked = session.pickedNodes();
        std::erase_if(picked, [&](const EditorSession::PickedNode &node) { return node.object == target.object; });
        session.pickNodes(picked);
        session.updateObject(object, QStringLiteral("Add Anchor Point"));
        return;
    }
    case PenAction::draw:
        break;
    }
    const std::optional<QPointF> previous = penContour() ? std::optional(penContour()->nodes.back().anchor) : std::nullopt;
    const bool constrained = modifiers.testFlag(Qt::ShiftModifier) && previous;
    const SmartGuides guides = guidesExcluding(pen ? std::vector<QUuid>{pen->object} : std::vector<QUuid>{});
    const QPointF at = constrained ? *previous + constrain45(document - *previous) : snapPoint(guides, document);
    if (!pen) {
        // One interaction for the whole path: it lands as one undo step.
        session.beginInteraction(QStringLiteral("Draw Path"));
        VectorPath path;
        path.contours.push_back({});
        addAnchor(path.contours.back(), at, std::nullopt);
        pen = Pen{session.previewAddObject(session.pathObject(path, QStringLiteral("Path")))};
    } else {
        VectorObject object = *session.document()->find(pen->object);
        addAnchor(object.path.contours[size_t(pen->contour)], at, std::nullopt);
        session.previewObject(object);
    }
    beginDrag(DragKind::pen, view);
    drag->pressDocument = at;
    drag->interacting = true;
}

void EditorCanvas::State::resumePen(const PenTarget &target, QPointF view)
{
    // Drawing on from the first anchor runs the contour the other way.
    session.beginInteraction(QStringLiteral("Draw Path"));
    VectorObject object = *session.document()->find(target.object);
    Contour &contour = object.path.contours[size_t(target.node.contour)];
    const bool backwards = target.node.node == 0 && contour.nodes.size() > 1;
    if (backwards) {
        contour = reversed(contour);
        session.previewObject(object);
    }
    session.select({target.object});
    pen = Pen{target.object, target.node.contour, false, backwards};
    beginDrag(DragKind::pen, view);
    drag->pressDocument = contour.nodes.back().anchor;
    // A drag from the end reshapes only its outgoing handle.
    drag->part = NodePart::out;
    drag->interacting = true;
}

void EditorCanvas::State::joinPen(const PenTarget &target)
{
    const VectorDocument &document = *session.document();
    VectorObject object = *document.find(pen->object);
    const VectorObject &other = *document.find(target.object);
    // The clicked end comes first, so the new segment runs into it.
    Contour joined = other.path.contours[size_t(target.node.contour)];
    if (target.node.node != 0)
        joined = reversed(joined);
    Contour &contour = object.path.contours[size_t(pen->contour)];
    contour.nodes.insert(contour.nodes.end(), joined.nodes.begin(), joined.nodes.end());
    // The other path's remaining contours come along.
    for (int c = 0; c < int(other.path.contours.size()); ++c) {
        if (c != target.node.contour)
            object.path.contours.push_back(other.path.contours[size_t(c)]);
    }
    const QUuid otherID = other.id;
    session.previewObject(object);
    session.previewRemoveObject(otherID);
    session.select({object.id});
    finishPen();
}

void EditorCanvas::State::convertPress(const PenTarget &target, QPointF view)
{
    session.beginInteraction(QStringLiteral("Convert Anchor Point"));
    VectorObject object = *session.document()->find(target.object);
    PathNode &node = *object.path.node(target.node);
    // A click makes a smooth anchor a corner; a drag pulls new symmetric handles.
    if (node.hasIn() || node.hasOut()) {
        node.in = node.out = node.anchor;
        node.smooth = false;
        session.previewObject(object);
    }
    beginDrag(DragKind::convert, view);
    drag->object = target.object;
    drag->node = target.node;
    drag->interacting = true;
}

void EditorCanvas::State::dragConvert(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started || !session.document() || !session.document()->find(drag->object))
        return;
    VectorObject object = *session.document()->find(drag->object);
    PathNode *node = object.path.node(drag->node);
    if (!node)
        return;
    QPointF to = toDocument(view);
    if (modifiers.testFlag(Qt::ShiftModifier))
        to = node->anchor + constrain45(to - node->anchor);
    pullHandles(*node, to);
    session.previewObject(object);
}

void EditorCanvas::State::dragPenHandle(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started || !penContour())
        return;
    VectorObject object = *session.document()->find(pen->object);
    Contour &contour = object.path.contours[size_t(pen->contour)];
    PathNode &node = pen->closing ? contour.nodes.front() : contour.nodes.back();
    QPointF to = toDocument(view);
    if (modifiers.testFlag(Qt::ShiftModifier))
        to = node.anchor + constrain45(to - node.anchor);
    if (drag->part == NodePart::out) {
        // A resumed end keeps the curve it had; only the next one changes.
        moveHandle(node, NodePart::out, to, true);
    } else {
        // Closing re-pulls the first anchor's handles; the mirrored one shapes the last curve.
        pullHandles(node, to);
    }
    session.previewObject(object);
}

void EditorCanvas::State::penRelease()
{
    clearGuides();
    if (pen && pen->closing)
        finishPen();
}

void EditorCanvas::State::finishPen()
{
    if (!pen)
        return;
    const Pen finished = *pen;
    pen.reset();
    if (drag && drag->kind == DragKind::pen)
        drag.reset();
    if (!session.isInteracting()) {
        canvas.update();
        return;
    }
    // A lone anchor is no path.
    const VectorObject *object = session.document() ? session.document()->find(finished.object) : nullptr;
    if (object && object->path.nodeCount() < 2) {
        session.previewRemoveObject(finished.object);
    } else if (object && finished.reversed && finished.contour < int(object->path.contours.size())) {
        // Anchors drawn off the first end were prepended; the contour turns back.
        VectorObject restored = *object;
        Contour &contour = restored.path.contours[size_t(finished.contour)];
        contour = reversed(contour);
        session.previewObject(restored);
    }
    session.commitInteraction();
    canvas.update();
}

// Pencil -----------------------------------------------------------------------

void EditorCanvas::State::pencilPress(QPointF view)
{
    beginDrag(DragKind::pencil, view);
    drag->points.push_back(drag->pressDocument);
}

void EditorCanvas::State::dragPencil(QPointF view)
{
    const QPointF point = toDocument(view);
    if (QLineF(point, drag->points.back()).length() >= reach(1))
        drag->points.push_back(point);
}

void EditorCanvas::State::finishPencil()
{
    std::vector<QPointF> points = drag->points;
    if (points.size() < 2 || !drag->started)
        return;
    // Ending near the start closes the shape.
    const bool closed = points.size() > 3 && QLineF(toView(points.front()), toView(points.back())).length() <= closeReach * 1.5;
    const VectorPath path = fitFreehand(points, reach(2), closed);
    if (path.nodeCount() >= 2)
        session.addPath(path, QStringLiteral("Path"));
}
