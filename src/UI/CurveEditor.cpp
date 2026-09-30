#include "UI/CurveEditor.h"
#include "System/TokenFiles.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("curveEditor"));
    setAccessibleName(QStringLiteral("Easing curve"));
    setFixedSize(sizeHint());
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::CrossCursor);
}

void CurveEditor::setCurve(const std::array<double, 4> &points)
{
    // A curve being dragged is not replaced under the hand.
    if (m_dragging)
        return;
    m_points = points;
    update();
}

QRectF CurveEditor::plot() const
{
    return QRectF(14, 10, width() - 28, height() - 20);
}

QPointF CurveEditor::pointOf(double x, double y) const
{
    const QRectF box = plot();
    return {box.left() + x * box.width(), box.bottom() - (y - lowest) / (highest - lowest) * box.height()};
}

QPointF CurveEditor::handle(int which) const
{
    return which == 1 ? pointOf(m_points[0], m_points[1]) : pointOf(m_points[2], m_points[3]);
}

void CurveEditor::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::WindowText);
    QColor line = text;
    line.setAlphaF(0.2f);
    const QColor accent = palette().color(QPalette::Highlight);
    // The unit square, and the diagonal a straight (linear) ease would follow.
    painter.setPen(line);
    painter.drawRect(QRectF(pointOf(0, 0), pointOf(1, 1)).normalized());
    painter.drawLine(pointOf(0, 0), pointOf(1, 1));
    // The curve and the two arms.
    QPainterPath path(pointOf(0, 0));
    path.cubicTo(handle(1), handle(2), pointOf(1, 1));
    painter.setPen(QPen(accent, 2));
    painter.drawPath(path);
    painter.setPen(QPen(text, 1, Qt::DashLine));
    painter.drawLine(pointOf(0, 0), handle(1));
    painter.drawLine(pointOf(1, 1), handle(2));
    painter.setPen(Qt::NoPen);
    painter.setBrush(text);
    for (int which : {1, 2})
        painter.drawEllipse(handle(which), 5.0, 5.0);
}

void CurveEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF at = event->position();
    const double one = std::hypot(at.x() - handle(1).x(), at.y() - handle(1).y());
    const double two = std::hypot(at.x() - handle(2).x(), at.y() - handle(2).y());
    if (std::min(one, two) > 14)
        return;
    m_dragging = one <= two ? 1 : 2;
}

void CurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const QRectF box = plot();
    const double x = std::clamp((event->position().x() - box.left()) / box.width(), 0.0, 1.0);
    const double y = std::clamp(lowest + (box.bottom() - event->position().y()) / box.height() * (highest - lowest), lowest, highest);
    // Two decimals: what a designer reads and types.
    const double rx = std::round(x * 100) / 100;
    const double ry = std::round(y * 100) / 100;
    if (m_dragging == 1) {
        m_points[0] = rx;
        m_points[1] = ry;
    } else {
        m_points[2] = rx;
        m_points[3] = ry;
    }
    update();
    emit previewed(TokenFiles::cubicBezierText(m_points));
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = 0;
    emit committed(TokenFiles::cubicBezierText(m_points));
}
