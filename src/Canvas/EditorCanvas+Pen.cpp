#include "Canvas/EditorCanvasState.h"
#include <QLineF>

namespace {
constexpr double closeReach = 7;

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
    return object && !object->path.contours.empty() ? &object->path.contours.back() : nullptr;
}

bool EditorCanvas::State::nearPenStart(QPointF view) const
{
    const Contour *contour = penContour();
    return contour && contour->nodes.size() >= 2 && QLineF(view, toView(contour->nodes.front().anchor)).length() <= closeReach;
}

void EditorCanvas::State::penPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    if (pen && !penContour())
        pen.reset();
    // Something else committed the path so far; later anchors are their own step.
    if (pen && !session.isInteracting())
        session.beginInteraction(QStringLiteral("Draw Path"));
    if (pen && nearPenStart(view)) {
        pen->closing = true;
        VectorObject object = *session.document()->find(pen->object);
        object.path.contours.back().closed = true;
        session.previewObject(object);
        beginDrag(DragKind::pen, view);
        drag->interacting = true;
        return;
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
        addAnchor(object.path.contours.back(), at, std::nullopt);
        session.previewObject(object);
    }
    beginDrag(DragKind::pen, view);
    drag->pressDocument = at;
    drag->interacting = true;
}

void EditorCanvas::State::dragPenHandle(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started || !penContour())
        return;
    VectorObject object = *session.document()->find(pen->object);
    Contour &contour = object.path.contours.back();
    PathNode &node = pen->closing ? contour.nodes.front() : contour.nodes.back();
    QPointF to = toDocument(view);
    if (modifiers.testFlag(Qt::ShiftModifier))
        to = node.anchor + constrain45(to - node.anchor);
    // Closing re-pulls the first anchor's handles; the mirrored one shapes the last curve.
    pullHandles(node, to);
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
    if (object && object->path.nodeCount() < 2)
        session.previewRemoveObject(finished.object);
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
