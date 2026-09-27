#include "Canvas/EditorCanvasState.h"
#include <QApplication>
#include <QWheelEvent>
#include <cmath>

void EditorCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_session.hasDocument() || (m_state->drag && m_state->drag->kind != State::DragKind::pan))
        return;
    // Trackpads report pixels; a wheel reports notches, lines each.
    const bool precise = !event->pixelDelta().isNull();
    QPointF delta = precise ? QPointF(event->pixelDelta()) : QPointF(event->angleDelta()) / 120.0 * QApplication::wheelScrollLines();
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
        const double step = precise ? delta.y() : delta.y() / QApplication::wheelScrollLines() * 8;
        m_state->zoomAt(m_session.viewport.zoom() * std::exp(step * 0.015), event->position());
        event->accept();
        return;
    }
    // A plain wheel scrolls down; Shift turns it sideways.
    if (event->modifiers().testFlag(Qt::ShiftModifier) && !precise && delta.x() == 0)
        delta = QPointF(delta.y(), 0);
    const double multiplier = precise ? 1 : 12;
    m_session.panView(QSizeF(delta.x() * multiplier, delta.y() * multiplier));
    event->accept();
}

void EditorCanvas::State::zoomAt(double zoom, QPointF view)
{
    session.setZoom(zoom, view);
    // The hover target moved under a still pointer.
    if (hover)
        updateHover(*hover);
}

void EditorCanvas::State::finishZoom(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QRectF area = QRectF(drag->pressView, view).normalized();
    if (!drag->started || area.width() < 4 || area.height() < 4) {
        // A click zooms a step about the pointer; Alt steps out.
        zoomAt(session.viewport.zoom() * (modifiers.testFlag(Qt::AltModifier) ? 0.5 : 2), drag->pressView);
        return;
    }
    // A dragged rectangle fills the view.
    const QSizeF viewSize = session.viewport.viewSize;
    const double factor = std::min(viewSize.width() / area.width(), viewSize.height() / area.height());
    session.setZoom(session.viewport.zoom() * factor, area.center());
    const QPointF middle = session.viewport.center();
    session.panView(QSizeF(middle.x() - area.center().x(), middle.y() - area.center().y()));
}
