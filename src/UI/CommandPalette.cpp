#include "UI/CommandPalette.h"
#include "Agent/AgentLauncher.h"
#include "UI/AgentBridge.h"
#include "UI/Menus.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
enum Role { whereRole = Qt::UserRole + 1, shortcutRole, enabledRole, onRole, askRole };
constexpr int rowHeight = 34;
constexpr int visibleRows = 9;
// The painted plate sits inside these, leaving room for its shadow.
constexpr QMargins shadow{6, 4, 6, 10};

// A row: the name, where it lives in quieter ink, then its key or On at the right.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(200, rowHeight); }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QPalette &palette = option.palette;
        const QRectF row = QRectF(option.rect).adjusted(6, 1, -6, -1);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            QColor plate = palette.color(QPalette::Highlight);
            plate.setAlphaF(option.state & QStyle::State_Selected ? 0.22f : 0.08f);
            painter->setPen(Qt::NoPen);
            painter->setBrush(plate);
            painter->drawRoundedRect(row, 6, 6);
        }
        const bool enabled = index.data(enabledRole).toBool();
        const bool asking = index.data(askRole).toBool();
        QColor ink = palette.color(asking ? QPalette::Highlight : QPalette::WindowText);
        QColor quiet = palette.color(QPalette::PlaceholderText);
        if (!enabled) {
            ink.setAlphaF(0.4f);
            quiet.setAlphaF(0.4f);
        }
        QRectF text = row.adjusted(10, 0, -10, 0);
        // The key, or On, at the right.
        const QString shortcut = index.data(shortcutRole).toString();
        const QString trailing = !shortcut.isEmpty() ? shortcut : index.data(onRole).toBool() ? QStringLiteral("On") : QString();
        QFont small = option.font;
        small.setPixelSize(11);
        if (!trailing.isEmpty()) {
            const QFontMetricsF metrics(small);
            const double width = metrics.horizontalAdvance(trailing) + 12;
            const QRectF cap(text.right() - width, row.center().y() - 10, width, 20);
            QColor edge = palette.color(QPalette::WindowText);
            edge.setAlphaF(0.16f);
            painter->setPen(QPen(edge, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            painter->setFont(small);
            painter->setPen(quiet);
            painter->drawText(cap, Qt::AlignCenter, trailing);
            text.setRight(cap.left() - 10);
        }
        QFont font = option.font;
        font.setPixelSize(13);
        const QFontMetricsF titleMetrics(font);
        const QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width() * 0.72);
        painter->setFont(font);
        painter->setPen(ink);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, title);
        const double used = titleMetrics.horizontalAdvance(title) + 10;
        const QString where = index.data(whereRole).toString();
        if (!where.isEmpty() && used < text.width()) {
            painter->setFont(small);
            painter->setPen(quiet);
            const QRectF rest = text.adjusted(used, 0, 0, 0);
            painter->drawText(rest, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(small).elidedText(where, Qt::ElideRight, rest.width()));
        }
        painter->restore();
    }
};
}

CommandPalette::CommandPalette(Menus &menus, QWidget *window)
    : QFrame(window), m_menus(menus), m_search(new QLineEdit(this)), m_list(new QListWidget(this)), m_footer(new QLabel(this))
{
    setObjectName(QStringLiteral("commandPalette"));
    setAccessibleName(QStringLiteral("Command Palette"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(shadow.left() + 8, shadow.top() + 8, shadow.right() + 8, shadow.bottom() + 6);
    column->setSpacing(6);
    m_search->setObjectName(QStringLiteral("commandSearch"));
    m_search->setAccessibleName(QStringLiteral("Search commands"));
    m_search->setPlaceholderText(QStringLiteral("Search commands, or describe a change for AI"));
    m_search->setFrame(false);
    m_search->setMinimumHeight(34);
    QFont large = m_search->font();
    large.setPixelSize(15);
    m_search->setFont(large);
    m_search->setTextMargins(8, 0, 8, 0);
    m_search->installEventFilter(this);
    m_list->setObjectName(QStringLiteral("commandList"));
    m_list->setAccessibleName(QStringLiteral("Commands"));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->viewport()->setAutoFillBackground(false);
    m_list->setAutoFillBackground(false);
    m_footer->setObjectName(QStringLiteral("commandMessage"));
    m_footer->setForegroundRole(QPalette::PlaceholderText);
    m_footer->setWordWrap(true);
    QFont small = m_footer->font();
    small.setPixelSize(11);
    m_footer->setFont(small);
    m_footer->setContentsMargins(8, 0, 8, 0);
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    column->addWidget(m_search);
    column->addWidget(line);
    column->addWidget(m_list);
    column->addWidget(m_footer);
    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::populate);
    connect(m_list, &QListWidget::clicked, this, [this](const QModelIndex &index) { runRow(index.row()); });
    hide();
}

void CommandPalette::open()
{
    if (!isVisible())
        m_returnFocus = QApplication::focusWidget();
    AgentBridge *agent = m_menus.agent();
    m_agentName = agent ? AgentBridge::displayName(AgentLauncher::defaultAgent()) : QString();
    gather();
    m_search->clear();
    populate(QString());
    place();
    show();
    raise();
    m_search->setFocus(Qt::ShortcutFocusReason);
}

void CommandPalette::close()
{
    if (!isVisible())
        return;
    hide();
    if (m_returnFocus)
        m_returnFocus->setFocus(Qt::OtherFocusReason);
    else if (EditorCanvas *canvas = m_menus.canvas())
        canvas->setFocus(Qt::OtherFocusReason);
}

QString CommandPalette::message() const
{
    return m_footer->text();
}

void CommandPalette::populate(const QString &query)
{
    m_shown = results(query);
    m_list->clear();
    for (const Command &command : m_shown) {
        auto *item = new QListWidgetItem(command.title, m_list);
        item->setData(whereRole, command.where);
        item->setData(shortcutRole, command.shortcut);
        item->setData(enabledRole, command.enabled);
        item->setData(onRole, command.on);
        item->setData(askRole, command.id == QLatin1String("ask"));
        QString spoken = command.title + QStringLiteral(", ") + command.where;
        if (!command.shortcut.isEmpty())
            spoken += QStringLiteral(", ") + command.shortcut;
        if (!command.enabled)
            spoken += QStringLiteral(", unavailable");
        item->setData(Qt::AccessibleTextRole, spoken);
    }
    m_list->setCurrentRow(m_shown.empty() ? -1 : 0);
    m_footer->setText(m_shown.empty() ? QStringLiteral("No command by that name.") : QString());
    m_footer->setVisible(!m_footer->text().isEmpty());
    place();
}

QString CommandPalette::runRow(int row)
{
    if (row < 0 || row >= int(m_shown.size()))
        return {};
    const Command command = m_shown[size_t(row)];
    if (!command.enabled) {
        const QString why = QStringLiteral("%1 isn't available right now.").arg(command.title);
        m_footer->setText(why);
        m_footer->show();
        return why;
    }
    if (command.id == QLatin1String("ask")) {
        // A failed start keeps the palette open, with the reason under the list.
        if (const QString why = command.run(); !why.isEmpty()) {
            m_footer->setText(why);
            m_footer->show();
            return why;
        }
        close();
        return {};
    }
    remember(command.id);
    // The keys go back where they were before the command runs, so it acts on the canvas and not the search.
    close();
    return command.run();
}

void CommandPalette::step(int by)
{
    if (m_list->count() == 0)
        return;
    const int row = std::clamp(m_list->currentRow() + by, 0, m_list->count() - 1);
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row));
}

void CommandPalette::place()
{
    QWidget *window = parentWidget();
    if (!window)
        return;
    const int width = std::min(620, window->width() - 40);
    const int rows = std::clamp(m_list->count(), 1, visibleRows);
    m_list->setFixedHeight(rows * rowHeight + 4);
    adjustSize();
    resize(width, sizeHint().height());
    move((window->width() - width) / 2, std::min(90, window->height() / 8));
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search) {
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            switch (key->key()) {
            case Qt::Key_Down: step(1); return true;
            case Qt::Key_Up: step(-1); return true;
            case Qt::Key_PageDown: step(visibleRows); return true;
            case Qt::Key_PageUp: step(-visibleRows); return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: runRow(m_list->currentRow()); return true;
            case Qt::Key_Escape: close(); return true;
            default: break;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // A click anywhere else puts it away; a menu opening over it doesn't.
            if (static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason && isVisible())
                hide();
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandPalette::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plate = QRectF(rect()).marginsRemoved(QMarginsF(shadow)).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    for (int step = 1; step <= 5; ++step) {
        painter.setBrush(QColor(0, 0, 0, 14));
        painter.drawRoundedRect(plate.adjusted(-step + 1, -step + 3, step - 1, step + 3), 12 + step, 12 + step);
    }
    QColor edge = palette().color(QPalette::WindowText);
    edge.setAlphaF(0.14f);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(plate, 12, 12);
}
