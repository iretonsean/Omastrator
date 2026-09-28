#include "UI/PanelSection.h"
#include "UI/NumberField.h"
#include "UI/PanelIcons.h"
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <array>

PanelSection::PanelSection(const QString &title, const QString &key, QWidget *parent, bool folded)
    : QWidget(parent), body(new QVBoxLayout), trailing(new QHBoxLayout), m_key(key), m_toggle(new QToolButton(this)),
      m_summary(new QLabel(this)), m_content(new QWidget(this))
{
    setObjectName(key + QStringLiteral("Section"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 8, 12, 10);
    column->setSpacing(6);
    auto *heading = new QHBoxLayout;
    heading->setSpacing(4);
    m_toggle->setObjectName(key + QStringLiteral("Toggle"));
    m_toggle->setText(title);
    m_toggle->setAutoRaise(true);
    m_toggle->setFocusPolicy(Qt::TabFocus);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setAccessibleName(title);
    m_toggle->setToolTip(QStringLiteral("Show or hide %1").arg(title));
    QFont font = m_toggle->font();
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    m_toggle->setFont(font);
    m_toggle->setIconSize(QSize(12, 12));
    heading->addWidget(m_toggle);
    // The summary takes the room between the title and the trailing controls, and clicks open the section.
    m_summary->setObjectName(key + QStringLiteral("Summary"));
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    m_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont quiet = m_summary->font();
    quiet.setPixelSize(12);
    m_summary->setFont(quiet);
    m_summary->installEventFilter(this);
    heading->addWidget(m_summary, 1);
    trailing->setSpacing(2);
    heading->addLayout(trailing);
    column->addLayout(heading);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    m_content->setLayout(body);
    column->addWidget(m_content);
    connect(m_toggle, &QToolButton::clicked, this, [this] {
        const bool collapse = !isCollapsed();
        QSettings().setValue(settingsKey(m_key), collapse);
        setCollapsed(collapse);
    });
    setCollapsed(QSettings().value(settingsKey(key), folded).toBool());
}

void PanelSection::refreshSummary()
{
    const QString text = summary && isCollapsed() ? summary() : QString();
    m_summary->setToolTip(text);
    // Elided to the room there is, as the panel narrows.
    m_summary->setText(m_summary->fontMetrics().elidedText(text, Qt::ElideRight, std::max(0, m_summary->width())));
    m_summary->setProperty("fullText", text);
}

QString PanelSection::summaryText() const
{
    return m_summary->property("fullText").toString();
}

QString PanelSection::settingsKey(const QString &key)
{
    return QStringLiteral("properties/collapsed/") + key;
}

bool PanelSection::isCollapsed() const
{
    return m_content->isHidden();
}

bool PanelSection::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_summary && event->type() == QEvent::MouseButtonRelease && isCollapsed()) {
        m_toggle->click();
        return true;
    }
    if (watched == m_summary && event->type() == QEvent::Resize)
        refreshSummary();
    return QWidget::eventFilter(watched, event);
}

void PanelSection::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyChevron();
}

void PanelSection::applyChevron()
{
    const QColor ink = palette().color(QPalette::PlaceholderText);
    m_toggle->setIcon(PanelIcons::pixmap(isCollapsed() ? PanelIcon::chevronRight : PanelIcon::chevronDown, 12, ink, devicePixelRatioF()));
}

void PanelSection::setCollapsed(bool collapsed)
{
    m_content->setVisible(!collapsed);
    m_toggle->setAccessibleDescription(collapsed ? QStringLiteral("Collapsed") : QStringLiteral("Expanded"));
    applyChevron();
    refreshSummary();
}

ReferencePointPicker::ReferencePointPicker(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("referencePoint"));
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(sizeHint());
    setAccessibleName(QStringLiteral("Reference point"));
    m_point = std::clamp(QSettings().value(QStringLiteral("referencePoint"), 0).toInt(), 0, 8);
    setToolTip(QStringLiteral("Reference point: %1. X and Y read it; W, H and rotation keep it still").arg(name(m_point)));
}

void ReferencePointPicker::setPoint(int point)
{
    point = std::clamp(point, 0, 8);
    if (point == m_point)
        return;
    m_point = point;
    QSettings().setValue(QStringLiteral("referencePoint"), point);
    setToolTip(QStringLiteral("Reference point: %1. X and Y read it; W, H and rotation keep it still").arg(name(point)));
    update();
    emit pointChanged(point);
}

QPointF ReferencePointPicker::locate(const QRectF &bounds, int point)
{
    const double column = (point % 3) / 2.0, row = (point / 3) / 2.0;
    return {bounds.left() + column * bounds.width(), bounds.top() + row * bounds.height()};
}

QString ReferencePointPicker::name(int point)
{
    static const std::array<const char *, 9> names{"top left", "top", "top right", "left", "center", "right", "bottom left", "bottom", "bottom right"};
    return QString::fromLatin1(names.at(size_t(std::clamp(point, 0, 8))));
}

void ReferencePointPicker::setCompact()
{
    m_side = NumberField::fieldHeight;
    setFixedSize(sizeHint());
    update();
}

QRectF ReferencePointPicker::cell(int point) const
{
    const double square = width() >= 30 ? 8 : 6;
    const double step = (width() - square - 2) / 2.0;
    return {1 + (point % 3) * step, 1 + (point / 3) * step, square, square};
}

void ReferencePointPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
    QColor faint = ink;
    faint.setAlphaF(0.35);
    // The bounds the points sit on.
    painter.setPen(QPen(faint, 1));
    painter.drawRect(QRectF(cell(0).center(), cell(8).center()));
    for (int point = 0; point < 9; ++point) {
        const QRectF square = cell(point);
        const bool chosen = point == m_point;
        painter.setPen(QPen(chosen ? palette().color(QPalette::Highlight) : ink, 1));
        painter.setBrush(chosen ? palette().color(QPalette::Highlight) : palette().color(QPalette::Window));
        painter.drawRect(chosen ? square : square.adjusted(1, 1, -1, -1));
    }
    if (hasFocus()) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

void ReferencePointPicker::mousePressEvent(QMouseEvent *event)
{
    int nearest = m_point;
    double best = 1e9;
    for (int point = 0; point < 9; ++point) {
        const double distance = QLineF(cell(point).center(), event->position()).length();
        if (distance < best) {
            best = distance;
            nearest = point;
        }
    }
    setPoint(nearest);
}

void ReferencePointPicker::keyPressEvent(QKeyEvent *event)
{
    const int column = m_point % 3, row = m_point / 3;
    switch (event->key()) {
    case Qt::Key_Left:
        setPoint(row * 3 + std::max(0, column - 1));
        break;
    case Qt::Key_Right:
        setPoint(row * 3 + std::min(2, column + 1));
        break;
    case Qt::Key_Up:
        setPoint(std::max(0, row - 1) * 3 + column);
        break;
    case Qt::Key_Down:
        setPoint(std::min(2, row + 1) * 3 + column);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}
