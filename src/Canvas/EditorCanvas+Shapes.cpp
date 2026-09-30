#include "Canvas/EditorCanvasState.h"
#include <cmath>

namespace {
QString shapeName(Tool tool)
{
    switch (tool) {
    case Tool::line:
        return QStringLiteral("Line");
    case Tool::roundedRectangle:
        return QStringLiteral("Rounded Rectangle");
    case Tool::ellipse:
        return QStringLiteral("Ellipse");
    case Tool::polygon:
        return QStringLiteral("Polygon");
    case Tool::star:
        return QStringLiteral("Star");
    default:
        return QStringLiteral("Rectangle");
    }
}

// A unit shape stretched to fill `rect`, so every shape follows the same drag.
VectorPath fitted(const VectorPath &unit, const QRectF &rect)
{
    const QRectF bounds = unit.bounds();
    if (bounds.width() <= 0 || bounds.height() <= 0)
        return unit;
    return unit.transformed(QTransform::fromTranslate(-bounds.left(), -bounds.top())
                            * QTransform::fromScale(rect.width() / bounds.width(), rect.height() / bounds.height())
                            * QTransform::fromTranslate(rect.left(), rect.top()));
}
}

void EditorCanvas::State::shapePress(QPointF view)
{
    beginDrag(DragKind::shape, view);
    drag->guides = guidesExcluding({});
    drag->pressDocument = snapPoint(drag->guides, drag->pressDocument);
    clearGuides();
}

VectorPath EditorCanvas::State::shapePath(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers) const
{
    const Tool tool = session.tool();
    const bool fromCenter = modifiers.testFlag(Qt::AltModifier);
    if (tool == Tool::line) {
        // Alt: the press is the line's middle.
        return fromCenter ? Shapes::line(from - (to - from), to) : Shapes::line(from, to);
    }
    QPointF delta = to - from;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        const double side = std::max(std::abs(delta.x()), std::abs(delta.y()));
        delta = QPointF(std::copysign(side, delta.x()), std::copysign(side, delta.y()));
    }
    const QRectF rect = fromCenter ? QRectF(from - delta, from + delta).normalized() : QRectF(from, from + delta).normalized();
    switch (tool) {
    case Tool::roundedRectangle:
        return Shapes::rectangle(rect, std::min({session.cornerRadius, rect.width() / 2, rect.height() / 2}));
    case Tool::ellipse:
        return Shapes::ellipse(rect);
    case Tool::polygon:
        return fitted(Shapes::polygon(QPointF(0, 0), 1, std::max(3, session.polygonSides)), rect);
    case Tool::star:
        return fitted(Shapes::star(QPointF(0, 0), 1, std::clamp(session.starInnerRatio, 0.01, 1.0), std::max(3, session.starPoints)), rect);
    default:
        return Shapes::rectangle(rect);
    }
}

// The Frame tool: a frame over the drag, nested in the innermost frame under the press, as Figma's are.
void EditorCanvas::State::dragFrame(const QRectF &drawn)
{
    if (!drag->interacting) {
        const VectorDocument &document = *session.document();
        std::optional<QUuid> host;
        for (const VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::frame && document.isOnCurrentPage(object.id) && document.isEffectivelyVisible(object.id)
                && !document.isEffectivelyLocked(object.id) && object.path.painterPath().contains(drag->pressDocument))
                host = object.id;
        }
        session.beginInteraction(QStringLiteral("Draw Frame"));
        VectorObject frame = VectorObject::frame(drawn, document.uniqueName(QStringLiteral("Frame")));
        drag->object = session.previewAddObject(frame, host);
        drag->interacting = true;
        return;
    }
    const VectorObject *current = session.document()->find(drag->object);
    if (!current)
        return;
    VectorObject object = *current;
    EditorSession::reshape(object, LiveRectangle{.rect = drawn, .placement = {}, .radii = current->shape->radii, .styles = current->shape->styles});
    session.previewObject(object);
}

void EditorCanvas::State::dragShape(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    const bool constrained = modifiers.testFlag(Qt::ShiftModifier) && session.tool() == Tool::line;
    const QPointF to = snapPoint(drag->guides, toDocument(view), drag->pressDocument, constrained);
    // Shift squares the box itself; its guides would pull it off square.
    if (modifiers.testFlag(Qt::ShiftModifier) && session.tool() != Tool::line)
        clearGuides();
    const VectorPath path = shapePath(drag->pressDocument, to, modifiers);
    if (session.tool() == Tool::frame) {
        dragFrame(path.bounds());
        return;
    }
    // Rectangles stay live, so their corners can change later.
    const auto place = [&](VectorObject &object) {
        object.path = path;
        if (session.tool() == Tool::rectangle || session.tool() == Tool::roundedRectangle) {
            LiveRectangle shape;
            shape.rect = path.bounds();
            shape.radii.fill(session.tool() == Tool::roundedRectangle ? std::max(0.0, session.cornerRadius) : 0.0);
            EditorSession::reshape(object, shape);
        }
    };
    if (!drag->interacting) {
        const QString name = shapeName(session.tool());
        session.beginInteraction(QStringLiteral("Draw %1").arg(name));
        VectorObject object = session.pathObject(path, name);
        place(object);
        drag->object = session.previewAddObject(object, session.drawingParent(drag->pressDocument));
        drag->interacting = true;
        return;
    }
    const VectorObject *current = session.document()->find(drag->object);
    if (!current)
        return;
    VectorObject object = *current;
    place(object);
    session.previewObject(object);
}
