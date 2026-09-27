#include "Canvas/TaskBar.h"
#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

namespace {
const QString turnedOnKey = QStringLiteral("view/contextualTaskBar");
const QString offsetKey = QStringLiteral("view/taskBarOffset");
const QString pinnedKey = QStringLiteral("view/taskBarPinned");
const QString pinnedAtKey = QStringLiteral("view/taskBarPinnedAt");
// The painted plate sits inside these, leaving room for its shadow.
constexpr QMargins shadow{3, 2, 3, 5};

QRect keptInside(QRect bar, QSize view)
{
    const int right = std::max(TaskBar::margin, view.width() - TaskBar::margin - bar.width());
    const int bottom = std::max(TaskBar::margin, view.height() - TaskBar::margin - bar.height());
    bar.moveTo(std::clamp(bar.x(), TaskBar::margin, right), std::clamp(bar.y(), TaskBar::margin, bottom));
    return bar;
}

double distanceTo(QPointF point, const QRectF &rect)
{
    const double dx = std::max({rect.left() - point.x(), 0.0, point.x() - rect.right()});
    const double dy = std::max({rect.top() - point.y(), 0.0, point.y() - rect.bottom()});
    return std::hypot(dx, dy);
}
}

// Six dots at the bar's start: drag to move it; right-click to pin, reset or turn it off.
class TaskBarGrip : public QWidget {
public:
    explicit TaskBarGrip(TaskBar &bar) : QWidget(&bar), m_bar(bar)
    {
        setObjectName(QStringLiteral("taskBarGrip"));
        setAccessibleName(QStringLiteral("Move Task Bar"));
        setToolTip(QStringLiteral("Drag to move · Right-click to pin or reset"));
        setFixedSize(12, 26);
        setCursor(Qt::SizeAllCursor);
        setContextMenuPolicy(Qt::DefaultContextMenu);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor ink = palette().color(QPalette::PlaceholderText);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 2; ++column)
                painter.drawEllipse(QPointF(3.5 + column * 5, height() / 2.0 - 5 + row * 5), 1.2, 1.2);
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_press = event->globalPosition().toPoint();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_press)
            m_bar.dragBy(event->globalPosition().toPoint() - *m_press, false);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_press)
            return;
        m_bar.dragBy(event->globalPosition().toPoint() - *m_press, true);
        m_press.reset();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override { reset(); }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction *pin = menu->addAction(QStringLiteral("Pin in Place"));
        pin->setObjectName(QStringLiteral("taskBarPin"));
        pin->setCheckable(true);
        pin->setChecked(TaskBar::isPinned());
        connect(pin, &QAction::triggered, &m_bar, [this](bool pinned) {
            TaskBar::setPinned(pinned, m_bar.pos());
            m_bar.refresh();
        });
        menu->addAction(QStringLiteral("Reset Position"), &m_bar, [this] { reset(); })->setObjectName(QStringLiteral("taskBarReset"));
        menu->addSeparator();
        menu->addAction(QStringLiteral("Hide Contextual Task Bar"), &m_bar, [] { TaskBar::setTurnedOn(false); })
            ->setObjectName(QStringLiteral("taskBarTurnOff"));
        menu->popup(event->globalPos());
    }

private:
    void reset()
    {
        TaskBar::setPinned(false);
        TaskBar::setSavedOffset(QPoint());
        m_bar.refresh();
    }

    TaskBar &m_bar;
    std::optional<QPoint> m_press;
};

TaskBar::TaskBar(EditorCanvas &canvas) : QFrame(&canvas), m_canvas(canvas), m_row(new QHBoxLayout(this)), m_grip(new TaskBarGrip(*this))
{
    setObjectName(QStringLiteral("contextualTaskBar"));
    setAccessibleName(QStringLiteral("Contextual Task Bar"));
    setFocusPolicy(Qt::NoFocus);
    m_row->setContentsMargins(shadow.left() + 4, shadow.top() + 4, shadow.right() + 5, shadow.bottom() + 4);
    m_row->setSpacing(4);
    m_row->setSizeConstraint(QLayout::SetFixedSize);
    m_row->addWidget(m_grip);
    hide();
    connect(&canvas, &EditorCanvas::gestureChanged, this, &TaskBar::refresh);
    connect(&canvas, &EditorCanvas::textEditingChanged, this, &TaskBar::refresh);
    connect(&canvas.session(), &EditorSession::changed, this, &TaskBar::refresh);
    canvas.installEventFilter(this);
}

bool TaskBar::isTurnedOn()
{
    return QSettings().value(turnedOnKey, true).toBool();
}

void TaskBar::setTurnedOn(bool on)
{
    QSettings().setValue(turnedOnKey, on);
    for (QWidget *widget : QApplication::allWidgets()) {
        if (auto *bar = qobject_cast<TaskBar *>(widget))
            bar->refresh();
    }
}

QPoint TaskBar::savedOffset()
{
    return QSettings().value(offsetKey, QPoint()).toPoint();
}

void TaskBar::setSavedOffset(QPoint offset)
{
    if (offset.isNull())
        QSettings().remove(offsetKey);
    else
        QSettings().setValue(offsetKey, offset);
}

bool TaskBar::isPinned()
{
    return QSettings().value(pinnedKey, false).toBool();
}

void TaskBar::setPinned(bool pinned, QPoint at)
{
    QSettings settings;
    settings.setValue(pinnedKey, pinned);
    if (pinned)
        settings.setValue(pinnedAtKey, at);
    else
        settings.remove(pinnedAtKey);
}

QPoint TaskBar::pinnedAt()
{
    return QSettings().value(pinnedAtKey, QPoint(margin, margin)).toPoint();
}

void TaskBar::setFiller(std::function<QString()> kind, std::function<void(QHBoxLayout &)> fill)
{
    m_kindOf = std::move(kind);
    m_fill = std::move(fill);
    m_kind.clear();
    refresh();
}

void TaskBar::setBlocked(std::function<bool()> blocked)
{
    m_blocked = std::move(blocked);
    refresh();
}

QRect TaskBar::place(const QRectF &selection, QSize bar, QSize view, QPoint offset)
{
    const int x = int(std::round(selection.center().x() - bar.width() / 2.0));
    const int below = int(std::ceil(selection.bottom())) + gap;
    const int above = int(std::floor(selection.top())) - gap - bar.height();
    int y = view.height() - margin - bar.height();
    if (below + bar.height() <= view.height() - margin)
        y = below;
    else if (above >= margin)
        y = above;
    return keptInside(QRect(QPoint(x, y) + offset, bar), view);
}

void TaskBar::refresh()
{
    const EditorSession &session = m_canvas.session();
    const std::optional<QRectF> selection = m_canvas.selectionViewRect();
    const bool wanted = isTurnedOn() && m_kindOf && selection && !m_canvas.isPaused() && !m_canvas.isGesturing() && !m_canvas.isEditingText()
        && !session.isInteracting() && !(m_blocked && m_blocked()) && QRectF(m_canvas.rect()).intersects(selection->adjusted(-1, -1, 1, 1));
    const QString kind = wanted ? m_kindOf() : QString();
    if (kind.isEmpty()) {
        if (isVisible()) {
            // Keys typed into the bar go back to the canvas.
            if (isAncestorOf(QApplication::focusWidget()))
                m_canvas.setFocus(Qt::OtherFocusReason);
            hide();
        }
        return;
    }
    if (kind != m_kind) {
        m_kind = kind;
        refill();
    }
    if (!isVisible()) {
        setFaded(false);
        show();
    }
    raise();
    reposition();
}

void TaskBar::refill()
{
    // A button may be refilling the bar from its own click: its widget goes later.
    while (m_row->count() > 1) {
        QLayoutItem *item = m_row->takeAt(1);
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (m_fill)
        m_fill(*m_row);
    // New children would show only later, after the bar was sized and placed without them.
    for (int index = 1; index < m_row->count(); ++index) {
        if (QWidget *widget = m_row->itemAt(index)->widget())
            widget->show();
    }
    m_row->activate();
}

void TaskBar::reposition()
{
    const std::optional<QRectF> selection = m_canvas.selectionViewRect();
    if (!selection)
        return;
    adjustSize();
    const QRect spot = isPinned() ? keptInside(QRect(pinnedAt(), size()), m_canvas.size())
                                  : place(*selection, size(), m_canvas.size(), savedOffset());
    move(spot.topLeft());
}

void TaskBar::dragBy(QPoint delta, bool finished)
{
    if (!m_dragStart)
        m_dragStart = isPinned() ? pos() : savedOffset();
    if (isPinned())
        setPinned(true, keptInside(QRect(*m_dragStart + delta, size()), m_canvas.size()).topLeft());
    else
        setSavedOffset(*m_dragStart + delta);
    reposition();
    if (finished)
        m_dragStart.reset();
}

void TaskBar::setFaded(bool faded)
{
    if (m_faded == faded)
        return;
    m_faded = faded;
    // Faded, clicks go through to the handle underneath.
    setAttribute(Qt::WA_TransparentForMouseEvents, faded);
    if (faded) {
        auto *effect = new QGraphicsOpacityEffect(this);
        effect->setOpacity(0.15);
        setGraphicsEffect(effect);
    } else {
        setGraphicsEffect(nullptr);
    }
}

void TaskBar::fadeFor(QPointF pointer)
{
    const QRectF bar = geometry();
    bool near = false;
    for (const QPointF &handle : m_canvas.selectionHandles()) {
        if (QLineF(pointer, handle).length() <= 28 && distanceTo(handle, bar) <= 40)
            near = true;
    }
    setFaded(near);
}

bool TaskBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == &m_canvas) {
        switch (event->type()) {
        case QEvent::Resize:
            if (isVisible())
                reposition();
            break;
        case QEvent::MouseMove:
            if (isVisible())
                fadeFor(static_cast<QMouseEvent *>(event)->position());
            break;
        case QEvent::Leave:
            setFaded(false);
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void TaskBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plate = QRectF(rect()).marginsRemoved(QMarginsF(shadow)).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    for (int step = 1; step <= 3; ++step) {
        painter.setBrush(QColor(0, 0, 0, 16));
        painter.drawRoundedRect(plate.adjusted(-step + 1, -step + 2, step - 1, step + 1), 10 + step, 10 + step);
    }
    QColor edge = palette().color(QPalette::WindowText);
    edge.setAlphaF(0.14f);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(plate, 10, 10);
}
