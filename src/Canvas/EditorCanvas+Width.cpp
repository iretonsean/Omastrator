#include "Canvas/EditorCanvasState.h"
#include "Document/StrokeGeometry.h"
#include <QLineF>
#include <QPainter>
#include <algorithm>
#include <tuple>

// Width (Shift-W) ----------------------------------------------------------------

std::optional<QUuid> EditorCanvas::State::widthTarget() const
{
    if (!session.document())
        return std::nullopt;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (object && object->kind == ObjectKind::path && object->stroke.isVisible() && !session.document()->isEffectivelyLocked(id))
            return id;
    }
    return std::nullopt;
}

std::optional<int> EditorCanvas::State::widthHandleAt(QPointF view) const
{
    const std::optional<QUuid> target = widthTarget();
    const VectorObject *object = target ? session.document()->find(*target) : nullptr;
    if (!object)
        return std::nullopt;
    const QPainterPath path = object->path.painterPath();
    for (int index = 0; index < int(object->stroke.widthPoints.size()); ++index) {
        const StrokeWidthPoint &point = object->stroke.widthPoints[size_t(index)];
        const StrokeGeometry::PathLocation at = StrokeGeometry::locateAtT(path, point.t);
        const QPointF left = toView(at.point + at.normal * point.left), right = toView(at.point - at.normal * point.right);
        if (QLineF(view, left).length() <= 7 || QLineF(view, right).length() <= 7)
            return index;
    }
    return std::nullopt;
}

void EditorCanvas::State::widthPress(QPointF view, Qt::KeyboardModifiers)
{
    widthPointObject.reset();
    widthPointIndex.reset();
    const std::optional<QUuid> target = widthTarget();
    if (!target)
        return;
    const VectorObject *object = session.document()->find(*target);
    const std::optional<int> handle = widthHandleAt(view);
    if (!handle) {
        // A drag has to start on the stroke itself, not anywhere on the canvas.
        const StrokeGeometry::PathLocation at = StrokeGeometry::locate(object->path.painterPath(), toDocument(view));
        const auto [halfLeft, halfRight] = StrokeGeometry::widthAt(object->stroke, at.t);
        if (at.distance > reach(8) + std::max(halfLeft, halfRight))
            return;
    }
    beginDrag(DragKind::width, view);
    drag->object = *target;
    if (handle) {
        drag->handle = *handle;
        widthPointObject = *target;
        widthPointIndex = *handle;
    } else {
        drag->handle = -1;
        drag->pathT = StrokeGeometry::locate(object->path.painterPath(), toDocument(view)).t;
    }
    session.beginInteraction(QStringLiteral("Width Point"));
    drag->interacting = true;
}

void EditorCanvas::State::dragWidth(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    const VectorObject *original = session.originalObject(drag->object);
    if (!original)
        return;
    VectorObject next = *original;
    StrokeStyle stroke = next.stroke;
    const QPainterPath path = next.path.painterPath();
    int index = drag->handle;
    // `original` never changes during the drag, so a still-pending point (-1) has to be
    // rebuilt fresh on every move; an existing one's index stays valid throughout.
    if (index < 0) {
        StrokeWidthPoint point;
        point.t = drag->pathT;
        std::tie(point.left, point.right) = StrokeGeometry::widthAt(stroke, point.t);
        const auto at = std::lower_bound(stroke.widthPoints.begin(), stroke.widthPoints.end(), point.t,
                                         [](const StrokeWidthPoint &p, double t) { return p.t < t; });
        // Two statements: in one expression `begin()` may be read before insert() reallocates.
        const auto inserted = stroke.widthPoints.insert(at, point);
        index = int(inserted - stroke.widthPoints.begin());
    }
    const StrokeGeometry::PathLocation at = StrokeGeometry::locateAtT(path, stroke.widthPoints[size_t(index)].t);
    const QPointF cursor = toDocument(view);
    const double signedOffset = QPointF::dotProduct(cursor - at.point, at.normal);
    const bool oneSided = modifiers.testFlag(Qt::AltModifier);
    if (signedOffset >= 0) {
        stroke.widthPoints[size_t(index)].left = signedOffset;
        if (!oneSided)
            stroke.widthPoints[size_t(index)].right = signedOffset;
    } else {
        stroke.widthPoints[size_t(index)].right = -signedOffset;
        if (!oneSided)
            stroke.widthPoints[size_t(index)].left = -signedOffset;
    }
    stroke.widthProfile = StrokeWidthProfile::custom;
    next.stroke = stroke;
    session.previewObject(next);
    widthPointObject = drag->object;
    widthPointIndex = index;
}

void EditorCanvas::State::finishWidth()
{
    if (!drag->interacting || !session.isInteracting())
        return;
    if (drag->started)
        session.commitInteraction();
    else
        session.cancelInteraction();
}

bool EditorCanvas::State::deleteWidthPoint()
{
    if (!widthPointObject || !widthPointIndex || !session.document())
        return false;
    const VectorObject *object = session.document()->find(*widthPointObject);
    if (!object || *widthPointIndex < 0 || size_t(*widthPointIndex) >= object->stroke.widthPoints.size())
        return false;
    VectorObject next = *object;
    next.stroke.widthPoints.erase(next.stroke.widthPoints.begin() + *widthPointIndex);
    if (next.stroke.widthPoints.empty())
        next.stroke.widthProfile = StrokeWidthProfile::uniform;
    session.updateObject(next, QStringLiteral("Remove Width Point"));
    widthPointObject.reset();
    widthPointIndex.reset();
    return true;
}

void EditorCanvas::State::drawWidth(QPainter &painter) const
{
    if (session.tool() != Tool::width)
        return;
    const std::optional<QUuid> target = widthTarget();
    const VectorObject *object = target ? session.document()->find(*target) : nullptr;
    if (!object || object->stroke.widthPoints.empty())
        return;
    const QPainterPath path = object->path.painterPath();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPen outline(QColor(0, 0, 0, 180), 1);
    outline.setCosmetic(true);
    for (const StrokeWidthPoint &point : object->stroke.widthPoints) {
        const StrokeGeometry::PathLocation at = StrokeGeometry::locateAtT(path, point.t);
        const QPointF left = toView(at.point + at.normal * point.left), right = toView(at.point - at.normal * point.right);
        painter.setPen(outline);
        painter.drawLine(left, right);
        painter.setBrush(Qt::white);
        for (const QPointF &handle : {left, right})
            painter.drawRect(QRectF(handle - QPointF(4, 4), QSizeF(8, 8)));
    }
    painter.restore();
}
