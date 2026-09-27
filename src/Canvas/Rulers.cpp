#include "Canvas/Rulers.h"
#include "Canvas/SmartGuides.h"
#include <QPainter>
#include <cmath>

Rulers::Rulers(EditorSession &session, QWidget *canvas) : QWidget(canvas), m_session(session)
{
    setObjectName(QStringLiteral("rulers"));
    setAccessibleName(QStringLiteral("Rulers"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void Rulers::setMarker(std::optional<QPointF> view)
{
    if (view == m_marker)
        return;
    m_marker = view;
    if (isVisible())
        update();
}

double Rulers::labelStep(double pointsPerUnit)
{
    const double wanted = 50 / std::max(pointsPerUnit, 1e-9);
    const double magnitude = std::pow(10, std::floor(std::log10(wanted)));
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (factor * magnitude >= wanted)
            return factor * magnitude;
    }
    return 10 * magnitude;
}

void Rulers::paintEvent(QPaintEvent *)
{
    if (!m_session.document())
        return;
    const QSizeF documentSize = m_session.document()->size;
    const double step = labelStep(m_session.viewport.pointsPerPixel());
    const double minor = step / 5;
    QPainter painter(this);
    const QColor paper = palette().color(QPalette::Window), ink = palette().color(QPalette::PlaceholderText);
    painter.fillRect(QRectF(0, 0, width(), thickness), paper);
    painter.fillRect(QRectF(0, 0, thickness, height()), paper);
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawLine(QPointF(0, thickness - 0.5), QPointF(width(), thickness - 0.5));
    painter.drawLine(QPointF(thickness - 0.5, 0), QPointF(thickness - 0.5, height()));
    QFont font = this->font();
    font.setPixelSize(9);
    painter.setFont(font);
    const QPointF first = m_session.viewport.documentPoint(QPointF(0, 0), documentSize);
    const QPointF last = m_session.viewport.documentPoint(QPointF(width(), height()), documentSize);
    for (int axis = 0; axis < 2; ++axis) {
        const bool across = axis == 0;
        const double from = across ? first.x() : first.y(), to = across ? last.x() : last.y();
        for (double at = std::floor(from / minor) * minor; at <= to; at += minor) {
            const QPointF view = m_session.viewport.viewPoint(QPointF(at, at), documentSize);
            const double place = std::round(across ? view.x() : view.y()) + 0.5;
            if (place < thickness)
                continue;
            const bool major = std::abs(std::remainder(at, step)) < minor / 2;
            const double length = major ? thickness : thickness / 3.0;
            painter.setPen(QPen(ink, 1));
            if (across)
                painter.drawLine(QPointF(place, thickness - length), QPointF(place, thickness));
            else
                painter.drawLine(QPointF(thickness - length, place), QPointF(thickness, place));
            if (!major)
                continue;
            const QString label = SmartGuides::label(std::abs(at) < minor / 2 ? 0 : at);
            if (across) {
                painter.drawText(QRectF(place + 3, 1, 60, thickness - 4), Qt::AlignLeft | Qt::AlignTop, label);
            } else {
                // Side labels read bottom to top, as Illustrator's do.
                painter.save();
                painter.translate(1, place - 3);
                painter.rotate(-90);
                painter.drawText(QRectF(0, 0, 60, thickness - 4), Qt::AlignLeft | Qt::AlignTop, label);
                painter.restore();
            }
        }
    }
    // The pointer, marked on both rulers.
    if (m_marker) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
        const double x = std::round(m_marker->x()) + 0.5, y = std::round(m_marker->y()) + 0.5;
        if (x > thickness)
            painter.drawLine(QPointF(x, 0), QPointF(x, thickness));
        if (y > thickness)
            painter.drawLine(QPointF(0, y), QPointF(thickness, y));
    }
    painter.fillRect(QRectF(0, 0, thickness, thickness), paper);
}
