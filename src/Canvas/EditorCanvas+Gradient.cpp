#include "Canvas/EditorCanvasState.h"
#include <QLineF>
#include <QPainter>
#include <cmath>

// Gradient (G) -----------------------------------------------------------------

namespace {
// Where a fill's fractions live: the shape's bounds, and how they reach the document.
struct GradientFrame {
    QRectF bounds;
    QTransform toDocument;
};

GradientFrame frameOf(const VectorObject &object)
{
    if (object.kind == ObjectKind::text)
        return {object.text.outline().boundingRect(), object.transform};
    return {object.path.painterPath().boundingRect(), QTransform()};
}

QPointF place(const GradientFrame &frame, QPointF fraction)
{
    const QRectF &b = frame.bounds;
    return frame.toDocument.map(QPointF(b.left() + fraction.x() * b.width(), b.top() + fraction.y() * b.height()));
}

QPointF fractionAt(const GradientFrame &frame, QPointF document)
{
    const QPointF local = frame.toDocument.inverted().map(document);
    const QRectF &b = frame.bounds;
    return {b.width() > 0 ? (local.x() - b.left()) / b.width() : 0.5, b.height() > 0 ? (local.y() - b.top()) / b.height() : 0.5};
}

bool isGradient(const Paint &paint)
{
    return (paint.kind == PaintKind::linearGradient || paint.kind == PaintKind::radialGradient) && paint.stops.size() >= 2;
}

// Stops sit beside the bar, clear of its ends, as small swatches.
constexpr double stopOffset = 14;

QPointF stopPoint(QPointF start, QPointF end, double offset)
{
    const QPointF along = start + (end - start) * offset;
    const QLineF line(start, end);
    const QPointF normal = line.length() > 1e-9 ? QPointF(-line.dy(), line.dx()) / line.length() : QPointF(0, 1);
    return along + normal * stopOffset;
}
}

std::optional<QUuid> EditorCanvas::State::gradientTarget() const
{
    if (!session.document())
        return std::nullopt;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (object && object->hasPaint() && !session.document()->isEffectivelyLocked(id))
            return id;
    }
    return std::nullopt;
}

std::optional<int> EditorCanvas::State::gradientHandleAt(QPointF view) const
{
    const std::optional<QUuid> target = gradientTarget();
    const VectorObject *object = target ? session.document()->find(*target) : nullptr;
    if (!object || !isGradient(object->fill))
        return std::nullopt;
    const GradientFrame frame = frameOf(*object);
    const QPointF start = toView(place(frame, object->fill.start)), end = toView(place(frame, object->fill.end));
    for (int index = 0; index < int(object->fill.stops.size()); ++index) {
        if (QLineF(view, stopPoint(start, end, object->fill.stops[size_t(index)].offset)).length() <= 7)
            return index;
    }
    if (QLineF(view, end).length() <= 7)
        return -2;
    if (QLineF(view, start).length() <= 7)
        return -1;
    return std::nullopt;
}

void EditorCanvas::State::gradientPress(QPointF view, Qt::KeyboardModifiers)
{
    const QPointF document = toDocument(view);
    std::optional<QUuid> target = gradientTarget();
    const std::optional<int> handle = gradientHandleAt(view);
    if (!handle) {
        // A click on another object takes it first, as Illustrator's Gradient tool does.
        const std::optional<QUuid> leaf = hitLeaf(document);
        const bool onTarget = leaf && target && (*leaf == *target || session.document()->isAncestor(*target, *leaf));
        if (leaf && !onTarget) {
            if (const std::optional<QUuid> pick = selectableTarget(*leaf))
                session.select({*pick});
            target = gradientTarget();
        }
        if (!target)
            return;
    }
    beginDrag(DragKind::gradient, view);
    drag->object = *target;
    drag->handle = handle.value_or(-3);
    session.beginInteraction(QStringLiteral("Gradient"));
    drag->interacting = true;
}

void EditorCanvas::State::dragGradient(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    const VectorObject *original = session.originalObject(drag->object);
    if (!original)
        return;
    VectorObject next = *original;
    const GradientFrame frame = frameOf(next);
    Paint paint = next.fill;
    if (!isGradient(paint)) {
        // A solid becomes a gradient from its colour to white, as the Properties menu converts it.
        const QColor first = paint.kind == PaintKind::none ? QColor(Qt::black) : paint.swatch();
        paint = Paint::linear(first, Qt::white).withCompositeOf(paint);
    }
    QPointF at = toDocument(view);
    if (drag->handle == -3 || drag->handle == -1 || drag->handle == -2) {
        // Shift: the bar keeps to 45° steps from its other end.
        const QPointF anchor = drag->handle == -3 ? drag->pressDocument
                             : drag->handle == -1 ? place(frame, paint.end)
                                                  : place(frame, paint.start);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            const QLineF line(anchor, at);
            const double angle = std::round(line.angle() / 45) * 45;
            QLineF snapped = QLineF::fromPolar(line.length(), angle);
            snapped.translate(anchor);
            at = snapped.p2();
        }
        if (drag->handle == -3) {
            paint.start = fractionAt(frame, drag->pressDocument);
            paint.end = fractionAt(frame, at);
        } else if (drag->handle == -1) {
            paint.start = fractionAt(frame, at);
        } else {
            paint.end = fractionAt(frame, at);
        }
    } else if (drag->handle >= 0 && drag->handle < int(paint.stops.size())) {
        // A stop slides along the bar.
        const QPointF start = place(frame, paint.start), end = place(frame, paint.end);
        const QPointF along = end - start;
        const double length = along.x() * along.x() + along.y() * along.y();
        const QPointF from = at - start;
        const double t = length > 1e-12 ? (from.x() * along.x() + from.y() * along.y()) / length : 0;
        paint.stops[size_t(drag->handle)].offset = std::clamp(t, 0.0, 1.0);
    }
    paint.swatchId.clear();
    next.fill = paint;
    session.previewObject(next);
}

void EditorCanvas::State::finishGradient()
{
    if (!drag->interacting || !session.isInteracting())
        return;
    if (drag->started)
        session.commitInteraction();
    else
        session.cancelInteraction();
}

void EditorCanvas::State::drawGradient(QPainter &painter) const
{
    if (session.tool() != Tool::gradient)
        return;
    const std::optional<QUuid> target = gradientTarget();
    const VectorObject *object = target ? session.document()->find(*target) : nullptr;
    if (!object || !isGradient(object->fill))
        return;
    const GradientFrame frame = frameOf(*object);
    const QPointF start = toView(place(frame, object->fill.start)), end = toView(place(frame, object->fill.end));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // A dark line under a light one reads on any artwork.
    for (const auto &[color, width] : {std::pair{QColor(0, 0, 0, 150), 3.0}, std::pair{QColor(Qt::white), 1.5}}) {
        QPen pen(color, width);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(start, end);
    }
    QPen outline(QColor(0, 0, 0, 180), 1);
    outline.setCosmetic(true);
    painter.setPen(outline);
    // The origin is a circle, the end a square, as Illustrator draws them.
    painter.setBrush(Qt::white);
    painter.drawEllipse(start, 5, 5);
    painter.drawRect(QRectF(end - QPointF(4.5, 4.5), QSizeF(9, 9)));
    for (const GradientStop &stop : object->fill.stops) {
        const QPointF at = stopPoint(start, end, stop.offset);
        painter.setPen(outline);
        painter.drawLine(start + (end - start) * stop.offset, at);
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(at - QPointF(6, 6), QSizeF(12, 12)));
        painter.setBrush(stop.color);
        painter.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
    }
    painter.restore();
}
