#include "Canvas/EditorCanvasState.h"
#include <QLineF>
#include <cmath>
#include <numbers>

namespace {
constexpr double widgetReach = 6;
// A square corner's widget sits this far in along the diagonal, in view points.
constexpr double restingInset = 14;
// Below this on screen, a side is too short for its corners' widgets.
constexpr double smallestSide = 32;

CornerStyle nextStyle(CornerStyle style)
{
    switch (style) {
    case CornerStyle::round:
        return CornerStyle::inverted;
    case CornerStyle::inverted:
        return CornerStyle::chamfer;
    case CornerStyle::chamfer:
        return CornerStyle::round;
    }
    return CornerStyle::round;
}
}

// Live corners -------------------------------------------------------------------

std::vector<EditorCanvas::State::CornerWidget> EditorCanvas::State::cornerWidgets() const
{
    std::vector<CornerWidget> widgets;
    if (session.tool() != Tool::directSelect || !session.document() || text)
        return widgets;
    const VectorDocument &document = *session.document();
    for (const QUuid &id : editablePaths()) {
        const LiveRectangle *shape = document.find(id)->liveShape();
        if (!shape)
            continue;
        const QRectF box = shape->rect.normalized();
        if (box.width() * scale() < smallestSide || box.height() * scale() < smallestSide)
            continue;
        for (int corner = 0; corner < 4; ++corner) {
            // At the arc's centre, or a little way in while the corner is square.
            const double inset = std::max(shape->effectiveRadius(corner), reach(restingInset) / std::numbers::sqrt2);
            widgets.push_back({id, corner, toView(shape->corner(corner) + shape->inward(corner) * inset * std::numbers::sqrt2)});
        }
    }
    return widgets;
}

std::optional<EditorCanvas::State::CornerWidget> EditorCanvas::State::cornerWidgetAt(QPointF view) const
{
    std::optional<CornerWidget> best;
    double bestDistance = widgetReach;
    for (const CornerWidget &widget : cornerWidgets()) {
        const double distance = QLineF(view, widget.view).length();
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = widget;
        }
    }
    return best;
}

void EditorCanvas::State::cornerPress(const CornerWidget &widget, QPointF view)
{
    beginDrag(DragKind::corner, view);
    drag->object = widget.object;
    drag->handle = widget.corner;
    // The radius the drag starts from.
    drag->guidePosition = session.document()->find(widget.object)->liveShape()->effectiveRadius(widget.corner);
}

void EditorCanvas::State::dragCorner(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Corner Radius"));
        drag->interacting = true;
    }
    const VectorObject *original = session.originalObject(drag->object);
    const LiveRectangle *shape = original ? original->liveShape() : nullptr;
    if (!shape)
        return;
    // Along the corner's diagonal: a radius of r puts the widget r·√2 in.
    const QPointF moved = toDocument(view) - drag->pressDocument;
    const double along = QPointF::dotProduct(moved, shape->inward(drag->handle)) / std::numbers::sqrt2;
    const QRectF box = shape->rect.normalized();
    double radius = std::clamp(drag->guidePosition + along, 0.0, std::min(box.width(), box.height()) / 2);
    if (session.snapsToPixel || modifiers.testFlag(Qt::ShiftModifier))
        radius = std::round(radius);
    // Every selected live rectangle follows; Alt turns only the corner dragged.
    for (const QUuid &id : editablePaths()) {
        const VectorObject *before = session.originalObject(id);
        if (!before || !before->liveShape())
            continue;
        VectorObject object = *before;
        LiveRectangle changed = *before->liveShape();
        for (int corner = 0; corner < 4; ++corner) {
            if (!modifiers.testFlag(Qt::AltModifier) || corner == drag->handle)
                changed.radii[size_t(corner)] = radius;
        }
        EditorSession::reshape(object, changed);
        session.previewObject(object);
    }
}

void EditorCanvas::State::finishCorner(Qt::KeyboardModifiers modifiers)
{
    if (drag->started) {
        if (drag->interacting && session.isInteracting())
            session.commitInteraction();
        return;
    }
    // Alt-click cycles round, inverted and chamfer, as Illustrator's widgets do.
    if (!modifiers.testFlag(Qt::AltModifier))
        return;
    const VectorObject *object = session.document()->find(drag->object);
    if (const LiveRectangle *shape = object ? object->liveShape() : nullptr)
        session.setCornerStyle(nextStyle(shape->styles[size_t(drag->handle)]), drag->handle);
}

// Scissors -------------------------------------------------------------------------

void EditorCanvas::State::scissorsPress(QPointF view)
{
    const QPointF document = toDocument(view);
    const std::optional<QUuid> leaf = hitLeaf(document);
    const VectorObject *object = leaf ? session.document()->find(*leaf) : nullptr;
    if (!object || object->kind != ObjectKind::path)
        return;
    const QUuid id = *leaf;
    // An anchor under the pointer is cut there; else the segment is split where clicked.
    if (const auto node = object->path.hitNode(document, reach(5), false); node && node->second == NodePart::anchor) {
        session.cutPath(id, node->first);
        return;
    }
    if (const auto segment = object->path.hitSegment(document, reach(5)))
        session.cutPath(id, segment->from, segment->t);
}
