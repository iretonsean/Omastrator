#include "UI/NativeLayerList.h"
#include "UI/PanelIcons.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {
PanelIcon kindIcon(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::layer: return PanelIcon::layer;
    case ObjectKind::group: return PanelIcon::group;
    case ObjectKind::path: return PanelIcon::path;
    case ObjectKind::text: return PanelIcon::text;
    case ObjectKind::image: return PanelIcon::image;
    }
    return PanelIcon::path;
}

QToolButton *rowButton(const QString &name, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(22, 22);
    button->setIconSize(QSize(15, 15));
    return button;
}
}

LayerCell::LayerCell(NativeLayerList &list)
    : QWidget(nullptr), m_list(list), m_eye(rowButton(QStringLiteral("layerEye"), this)), m_lock(rowButton(QStringLiteral("layerLock"), this)),
      m_disclosure(rowButton(QStringLiteral("layerDisclosure"), this)), m_icon(new QLabel(this)), m_name(new QLabel(this)),
      m_editor(new QLineEdit(this)), m_fade(new QGraphicsOpacityEffect(this))
{
    setObjectName(QStringLiteral("layerCell"));
    setFixedHeight(rowHeight);
    setGraphicsEffect(m_fade);
    m_disclosure->setFixedSize(16, 22);
    m_disclosure->setAccessibleName(QStringLiteral("Expand or collapse"));
    m_icon->setObjectName(QStringLiteral("layerKind"));
    m_icon->setFixedSize(16, 16);
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont name = m_name->font();
    name.setPixelSize(12);
    m_name->setFont(name);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setObjectName(QStringLiteral("layerName"));
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_editor->setObjectName(QStringLiteral("layerNameEditor"));
    m_editor->setFont(name);
    m_editor->hide();
    m_editor->installEventFilter(this);
    m_eye->installEventFilter(this);
    m_lock->installEventFilter(this);
    // Alt-click, as in Illustrator: the eye or lock of every other row, as one step.
    connect(m_eye, &QToolButton::clicked, this, [this] {
        EditorSession &session = m_list.session();
        if (m_controlModifiers.testFlag(Qt::AltModifier)) {
            session.setOthersVisible(m_id, !session.anyOtherVisible(m_id));
            return;
        }
        session.setVisible(m_id, !session.document()->find(m_id)->isVisible);
    });
    connect(m_lock, &QToolButton::clicked, this, [this] {
        EditorSession &session = m_list.session();
        if (m_controlModifiers.testFlag(Qt::AltModifier)) {
            session.setOthersLocked(m_id, session.anyOtherUnlocked(m_id));
            return;
        }
        session.setLocked(m_id, !session.document()->find(m_id)->isLocked);
    });
    connect(m_disclosure, &QToolButton::clicked, this, [this] {
        const VectorObject *object = m_list.session().document()->find(m_id);
        m_list.session().setExpanded(m_id, !object->isExpanded);
    });
}

void LayerCell::configure(const VectorObject &object, int depth, bool visible, const QColor &layerColor)
{
    const QColor ink = palette().color(QPalette::WindowText), faint = palette().color(QPalette::PlaceholderText);
    const double ratio = devicePixelRatio();
    m_id = object.id;
    m_isLayer = object.kind == ObjectKind::layer;
    m_isContainer = object.isContainer();
    m_layerColor = layerColor;
    m_indent = std::min(depth, 10) * 14;
    m_eye->setIcon(PanelIcons::pixmap(object.isVisible ? PanelIcon::eye : PanelIcon::eyeSlash, 15, ink, ratio));
    m_eye->setAccessibleName((object.isVisible ? QStringLiteral("Hide ") : QStringLiteral("Show ")) + object.name);
    m_eye->setToolTip(object.isVisible ? QStringLiteral("Hide") : QStringLiteral("Show"));
    // Unlocked rows show a faint open lock as a hint.
    m_lock->setIcon(PanelIcons::pixmap(object.isLocked ? PanelIcon::lock : PanelIcon::unlock, 15, object.isLocked ? ink : faint, ratio));
    m_lock->setAccessibleName((object.isLocked ? QStringLiteral("Unlock ") : QStringLiteral("Lock ")) + object.name);
    m_lock->setToolTip(object.isLocked ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
    m_disclosure->setVisible(m_isContainer);
    m_disclosure->setIcon(PanelIcons::pixmap(object.isExpanded ? PanelIcon::chevronDown : PanelIcon::chevronRight, 12, ink, ratio));
    m_icon->setPixmap(PanelIcons::pixmap(kindIcon(object.kind), 16, faint, ratio));
    QFont font = m_name->font();
    font.setWeight(m_isLayer ? QFont::DemiBold : QFont::Normal);
    m_name->setFont(font);
    m_objectName = object.name;
    if (!m_renaming)
        m_name->setText(object.isClipGroup ? object.name + QStringLiteral(" (clip)") : object.name);
    m_fade->setOpacity(visible ? 1 : 0.4);
    relayout();
    QWidget::update();
}

void LayerCell::relayout()
{
    const int middle = rowHeight / 2;
    m_eye->move(4, middle - 11);
    m_lock->move(26, middle - 11);
    const int left = 52 + m_indent;
    m_disclosure->move(left, middle - 11);
    m_icon->move(left + 18, middle - 8);
    const int nameLeft = left + 40, nameWidth = std::max(10, width() - nameLeft - 22);
    m_name->setGeometry(nameLeft, middle - 9, nameWidth, 18);
    m_editor->setGeometry(nameLeft - 2, middle - 11, nameWidth + 2, 22);
}

void LayerCell::resizeEvent(QResizeEvent *)
{
    relayout();
}

void LayerCell::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const bool highlighted = m_list.isHighlighted(m_id);
    if (highlighted) {
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlphaF(0.3f);
        painter.fillRect(rect(), fill);
    }
    // Layer colour: a bar on layers, a square on selections.
    if (m_isLayer)
        painter.fillRect(QRect(50, 4, 2, height() - 8), m_layerColor);
    if (highlighted && !m_isLayer)
        painter.fillRect(QRect(width() - 16, height() / 2 - 4, 8, 8), m_layerColor);
    QColor edge = palette().color(QPalette::WindowText);
    edge.setAlphaF(0.06f);
    painter.fillRect(QRectF(0, height() - 1.0 / devicePixelRatio(), width(), 1.0 / devicePixelRatio()), edge);
}

bool LayerCell::isOnControl(QPoint point) const
{
    for (const QWidget *control : {static_cast<QWidget *>(m_eye), static_cast<QWidget *>(m_lock), static_cast<QWidget *>(m_disclosure)}) {
        if (control->isVisible() && control->geometry().contains(point))
            return true;
    }
    return false;
}

void LayerCell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_list.clickRow(*this, event->modifiers());
    // Layers drag among layers; art drags between rows.
    m_press = event->position().toPoint();
}

// Left held past the distance drags.
void LayerCell::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_press || !event->buttons().testFlag(Qt::LeftButton)
        || (event->position().toPoint() - *m_press).manhattanLength() < QApplication::startDragDistance())
        return;
    m_press = std::nullopt;
    m_list.startDrag(*this);
}

void LayerCell::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !isOnControl(event->position().toPoint()))
        beginRenaming();
}

void LayerCell::beginRenaming()
{
    if (m_renaming)
        return;
    m_renaming = true;
    m_editor->setText(m_objectName);
    m_editor->show();
    m_editor->setFocus(Qt::OtherFocusReason);
    m_editor->selectAll();
}

void LayerCell::endRenaming(bool keeping)
{
    if (!m_renaming)
        return;
    m_renaming = false;
    m_editor->hide();
    // The session refuses an empty or unchanged name.
    if (keeping)
        m_list.session().rename(m_id, m_editor->text());
    if (const VectorObject *object = m_list.session().document() ? m_list.session().document()->find(m_id) : nullptr)
        m_name->setText(object->name);
    m_list.setFocus(Qt::OtherFocusReason);
}

bool LayerCell::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_eye || watched == m_lock) && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        m_controlModifiers = static_cast<QMouseEvent *>(event)->modifiers();
        return false;
    }
    if (watched != m_editor || !m_renaming)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::FocusOut) {
        // A menu borrows focus and gives it back: no leaving.
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason != Qt::MenuBarFocusReason && reason != Qt::PopupFocusReason)
            endRenaming(true);
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        endRenaming(true);
    else if (key == Qt::Key_Escape)
        endRenaming(false);
    else
        return false;
    return true;
}
