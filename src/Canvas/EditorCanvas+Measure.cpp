#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>
#include <numbers>

std::optional<QRectF> EditorCanvas::State::measureTarget() const
{
    const Tool tool = session.tool();
    if (!modifiers.testFlag(Qt::AltModifier) || drag || text || spaceHeld || !hover || !session.hasSelection()
        || (tool != Tool::select && tool != Tool::directSelect))
        return std::nullopt;
    const VectorDocument &document = *session.document();
    // Over another object: to it. Over the selection or nothing: to the artboard.
    if (hovered && !session.isSelected(*hovered) && document.find(*hovered)) {
        const bool inside = std::any_of(session.selection().begin(), session.selection().end(),
                                        [&](const QUuid &id) { return document.isAncestor(id, *hovered) || document.isAncestor(*hovered, id); });
        if (!inside)
            return document.bounds(*hovered);
    }
    return document.artboard(session.activeArtboard()).rect;
}

std::vector<QLineF> EditorCanvas::State::measureLines() const
{
    const std::optional<QRectF> target = measureTarget();
    return target ? SmartGuides::distances(session.selectionBounds(), *target) : std::vector<QLineF>();
}

QString EditorCanvas::State::readout() const
{
    if (!drag || !drag->started || !drag->interacting || !session.hasSelection())
        return {};
    const QRectF bounds = session.selectionBounds();
    switch (drag->kind) {
    case DragKind::move: {
        const QPointF delta = bounds.topLeft() - drag->startBounds.topLeft();
        return QStringLiteral("Δx %1  Δy %2").arg(SmartGuides::label(delta.x()), SmartGuides::label(delta.y()));
    }
    case DragKind::scale:
    case DragKind::scaleTool:
        return QStringLiteral("%1 × %2").arg(SmartGuides::label(bounds.width()), SmartGuides::label(bounds.height()));
    case DragKind::rotate: {
        const QPointF from = drag->pressDocument - drag->center, to = toDocument(drag->lastView) - drag->center;
        double degrees = (std::atan2(to.y(), to.x()) - std::atan2(from.y(), from.x())) * 180 / std::numbers::pi;
        if (modifiers.testFlag(Qt::ShiftModifier))
            degrees = std::round(degrees / 45) * 45;
        // Screen y runs down: a clockwise drag reads negative, as Illustrator's angles do.
        degrees = std::remainder(-degrees, 360.0);
        return SmartGuides::label(degrees) + QStringLiteral("°");
    }
    default:
        return {};
    }
}

void EditorCanvas::State::drawLabel(QPainter &painter, QPointF center, const QString &label) const
{
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    const QSizeF size(metrics.horizontalAdvance(label) + 10, metrics.height() + 4);
    const QRectF pill(center - QPointF(size.width() / 2, size.height() / 2), size);
    painter.save();
    painter.setFont(font);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent());
    painter.drawRoundedRect(pill, 3, 3);
    painter.setPen(canvas.palette().color(QPalette::HighlightedText));
    painter.drawText(pill, Qt::AlignCenter, label);
    painter.restore();
}

void EditorCanvas::State::drawMeasurements(QPainter &painter) const
{
    const std::optional<QRectF> target = measureTarget();
    if (!target)
        return;
    const QTransform toViewTransform = documentToView();
    painter.save();
    QPen dashed(accent(), 1);
    dashed.setCosmetic(true);
    dashed.setDashPattern({3, 3});
    painter.setPen(dashed);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(toViewTransform.mapRect(*target));
    QPen solid(accent(), 1);
    solid.setCosmetic(true);
    for (const QLineF &line : SmartGuides::distances(session.selectionBounds(), *target)) {
        const QLineF shown = toViewTransform.map(line);
        painter.setPen(solid);
        painter.drawLine(shown);
        const QPointF normal = QPointF(-shown.dy(), shown.dx()) / std::max(1e-9, shown.length()) * 3;
        painter.drawLine(shown.p1() - normal, shown.p1() + normal);
        painter.drawLine(shown.p2() - normal, shown.p2() + normal);
        // Beside a vertical line, above a horizontal one, so the line stays readable.
        const bool vertical = std::abs(shown.dx()) < std::abs(shown.dy());
        drawLabel(painter, shown.center() + (vertical ? QPointF(22, 0) : QPointF(0, -11)), SmartGuides::label(line.length()));
    }
    painter.restore();
}

void EditorCanvas::State::drawReadout(QPainter &painter) const
{
    const QString label = readout();
    if (label.isEmpty())
        return;
    // Below and right of the pointer, clear of the cursor.
    const QFontMetricsF metrics(canvas.font());
    drawLabel(painter, drag->lastView + QPointF(18 + metrics.horizontalAdvance(label) / 2, 28), label);
}

std::vector<QLineF> EditorCanvas::measurements() const
{
    return m_state->measureLines();
}

QString EditorCanvas::dragReadout() const
{
    return m_state->readout();
}
