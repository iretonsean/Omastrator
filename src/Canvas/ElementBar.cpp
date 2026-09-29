#include "Canvas/ElementBar.h"
#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QEvent>
#include <QPainter>

namespace {
// The painted plate sits inside these, leaving room for its shadow.
constexpr QMargins shadow{3, 2, 3, 5};
}

ElementBar::ElementBar(EditorCanvas &canvas) : QFrame(&canvas), m_canvas(canvas), m_row(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("elementBar"));
    setAccessibleName(QStringLiteral("Element Bar"));
    setFocusPolicy(Qt::NoFocus);
    m_row->setContentsMargins(shadow.left() + 4, shadow.top() + 2, shadow.right() + 4, shadow.bottom() + 2);
    m_row->setSpacing(4);
    m_row->setSizeConstraint(QLayout::SetFixedSize);
    hide();
    connect(&canvas, &EditorCanvas::editPageChanged, this, &ElementBar::refresh);
    connect(&canvas, &EditorCanvas::editPageHostChanged, this, &ElementBar::refresh);
    connect(&canvas, &EditorCanvas::textEditingChanged, this, &ElementBar::refresh);
    connect(&canvas.session(), &EditorSession::changed, this, &ElementBar::refresh);
    canvas.installEventFilter(this);
}

void ElementBar::setFiller(std::function<QString()> signature, std::function<void(QHBoxLayout &)> fill, std::function<void()> sync)
{
    m_signatureOf = std::move(signature);
    m_fill = std::move(fill);
    m_sync = std::move(sync);
    m_signature.clear();
    refresh();
}

QRect ElementBar::place(const QRectF &selection, QSize bar, const QRectF &visible, int gap)
{
    const int x = int(std::round(selection.center().x() - bar.width() / 2.0));
    const int below = int(std::ceil(selection.bottom())) + gap;
    const int above = int(std::floor(selection.top())) - gap - bar.height();
    int y = below;
    if (below + bar.height() > visible.bottom() && above >= visible.top())
        y = above;
    QRect spot(QPoint(x, y), bar);
    const QRect inside = visible.toAlignedRect();
    spot.moveTo(std::clamp(spot.x(), inside.left(), std::max(inside.left(), inside.right() + 1 - spot.width())),
                std::clamp(spot.y(), inside.top(), std::max(inside.top(), inside.bottom() + 1 - spot.height())));
    return spot;
}

void ElementBar::refresh()
{
    const std::optional<QUuid> frame = m_canvas.editPageFrame();
    const std::optional<QRectF> selection = frame ? m_canvas.editPageSelectionRect() : std::nullopt;
    const bool wanted = selection && m_signatureOf && !m_canvas.isEditingPageText() && !m_canvas.isPaused() && !m_canvas.isGesturing();
    const QString signature = wanted ? m_signatureOf() : QString();
    if (signature.isEmpty()) {
        if (isVisible()) {
            // Keys typed into the bar go back to the canvas.
            if (isAncestorOf(QApplication::focusWidget()))
                m_canvas.setFocus(Qt::OtherFocusReason);
            hide();
        }
        return;
    }
    if (signature != m_signature) {
        m_signature = signature;
        refill();
    } else if (m_sync) {
        m_sync();
    }
    if (!isVisible())
        show();
    raise();
    reposition();
}

void ElementBar::refill()
{
    // A control may be refilling the bar from its own click: its widget goes later.
    while (m_row->count() > 0) {
        QLayoutItem *item = m_row->takeAt(0);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (m_fill)
        m_fill(*m_row);
    for (int index = 0; index < m_row->count(); ++index) {
        if (QWidget *widget = m_row->itemAt(index)->widget())
            widget->show();
    }
    m_row->activate();
    if (m_sync)
        m_sync();
}

void ElementBar::reposition()
{
    const std::optional<QRectF> selection = m_canvas.editPageSelectionRect();
    const std::optional<QRectF> visible = m_canvas.editPageVisibleRect();
    if (!selection || !visible)
        return;
    adjustSize();
    // The plate is barHeight tall (24 px controls and 2 px margins); the shadow margins sit around it.
    // A frame narrower than the bar can't hold it: it then keeps to the canvas instead.
    const QSize plate(width() - shadow.left() - shadow.right(), barHeight);
    const QRectF limit = plate.width() > visible->width() || plate.height() > visible->height() ? QRectF(m_canvas.rect()) : *visible;
    const QRect spot = place(*selection, plate, limit);
    move(spot.left() - shadow.left(), spot.top() - shadow.top());
}

bool ElementBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == &m_canvas && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(watched, event);
}

void ElementBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plate = QRectF(rect()).marginsRemoved(QMarginsF(shadow)).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    for (int step = 1; step <= 3; ++step) {
        painter.setBrush(QColor(0, 0, 0, 16));
        painter.drawRoundedRect(plate.adjusted(-step + 1, -step + 2, step - 1, step + 1), 8 + step, 8 + step);
    }
    QColor edge = palette().color(QPalette::WindowText);
    edge.setAlphaF(0.14f);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(plate, 8, 8);
}
