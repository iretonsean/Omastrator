#include "UI/ProjectTabs.h"
#include "UI/PanelIcons.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

namespace {
QFont tabFont(bool active)
{
    QFont font;
    font.setPixelSize(12);
    font.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return font;
}
}

ProjectTabButton::ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, QWidget *parent)
    : QWidget(parent), tab(std::move(tab)), m_workspace(workspace), m_select(new QToolButton(this)), m_close(new QToolButton(this))
{
    // 28 high: the strip's 34 less its margins.
    setObjectName(QStringLiteral("projectTab"));
    m_select->setObjectName(QStringLiteral("selectTab"));
    m_select->setAutoRaise(true);
    m_select->setFocusPolicy(Qt::NoFocus);
    m_select->setMinimumWidth(35);
    m_select->setMaximumWidth(180);
    m_close->setObjectName(QStringLiteral("closeTab"));
    m_close->setAutoRaise(true);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setText(QStringLiteral("×"));
    m_close->setFixedSize(16, 28);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(11, 0, 5, 0);
    row->setSpacing(0);
    row->addWidget(m_select);
    row->addWidget(m_close);
    connect(m_select, &QToolButton::clicked, this, [this] { m_workspace.select(this->tab->id); });
    connect(m_close, &QToolButton::clicked, this, [this] { m_workspace.close(this->tab->id); });
    connect(&this->tab->session, &EditorSession::changed, this, &ProjectTabButton::synchronize);
    synchronize();
}

QString ProjectTabButton::text() const
{
    return m_select->text();
}

void ProjectTabButton::synchronize()
{
    const bool active = m_workspace.selectedID() == tab->id;
    // The dot for unsaved changes stands before the title.
    const bool modified = tab->session.hasDocument() && tab->session.isModified();
    m_select->setText((modified ? QStringLiteral("● ") : QString()) + tab->title());
    m_select->setFont(tabFont(active));
    const bool locked = tab->session.isDocumentLocked();
    m_select->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_select->setIconSize(QSize(12, 12));
    m_select->setIcon(locked ? QIcon(PanelIcons::pixmap(PanelIcon::lock, 12, palette().color(QPalette::WindowText), devicePixelRatioF())) : QIcon());
    QString tip = tab->cloudStatus.isEmpty() ? tab->place() : tab->place() + QLatin1Char('\n') + tab->cloudStatus;
    if (locked)
        tip += QStringLiteral("\nLocked: read-only until unlocked (File ▸ Unlock Document)");
    m_select->setToolTip(tip);
    m_select->setEnabled(!m_workspace.isManaging() || active);
    m_close->setToolTip(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setAccessibleName(QStringLiteral("Close %1").arg(tab->title()));
    m_close->setEnabled(!m_workspace.isManaging());
    // The capsule repaints with the buttons it holds.
    setProperty("active", active);
    update();
}

bool ProjectTabButton::isActive() const
{
    return property("active").toBool();
}

void ProjectTabButton::paintEvent(QPaintEvent *)
{
    // A capsule: brighter under the front tab.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool active = isActive();
    QColor fill = palette().color(QPalette::WindowText), edge = fill;
    fill.setAlphaF(active ? 0.12f : 0.035f);
    edge.setAlphaF(active ? 0.22f : 0.08f);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 14, 14);
}

ProjectTabStrip::ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent)
    : QScrollArea(parent), m_workspace(workspace), m_row(new QWidget(this))
{
    setObjectName(QStringLiteral("projectTabs"));
    setAccessibleName(QStringLiteral("Document tabs"));
    setFixedHeight(34);
    setFrameShape(QFrame::NoFrame);
    // The toolbar shows through; a stylesheet would freeze the palette.
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *row = new QHBoxLayout(m_row);
    row->setContentsMargins(0, 3, 0, 3);
    row->setSpacing(6);
    row->addStretch(1);
    setWidget(m_row);
    m_row->setAutoFillBackground(false);
    m_row->installEventFilter(this);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectTabStrip::synchronize);
    synchronize();
}

QList<ProjectTabButton *> ProjectTabStrip::buttons() const
{
    QList<ProjectTabButton *> result;
    for (int index = 0; index < m_row->layout()->count(); ++index) {
        if (auto *button = qobject_cast<ProjectTabButton *>(m_row->layout()->itemAt(index)->widget()))
            result << button;
    }
    return result;
}

void ProjectTabStrip::synchronize()
{
    // Buttons follow the tabs: kept where the tab stays.
    auto *row = static_cast<QHBoxLayout *>(m_row->layout());
    QList<ProjectTabButton *> shown = buttons();
    int position = 0;
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        const auto kept = std::find_if(shown.begin(), shown.end(), [&](ProjectTabButton *button) { return button->tab == tab; });
        ProjectTabButton *button = kept == shown.end() ? new ProjectTabButton(m_workspace, tab, m_row) : *kept;
        if (kept != shown.end())
            shown.erase(kept);
        row->removeWidget(button);
        row->insertWidget(position, button);
        button->show();
        button->synchronize();
        position += 1;
    }
    // Later: a tab's own close button may remove it.
    for (ProjectTabButton *gone : shown) {
        row->removeWidget(gone);
        gone->hide();
        gone->deleteLater();
    }
    m_row->resize(m_row->sizeHint());
    showFront();
}

bool ProjectTabStrip::eventFilter(QObject *watched, QEvent *event)
{
    // A title changed size: the row takes its new width.
    if (event->type() == QEvent::LayoutRequest) {
        m_row->resize(m_row->sizeHint());
        showFront();
    }
    return QScrollArea::eventFilter(watched, event);
}

// The front tab scrolls into view.
void ProjectTabStrip::showFront()
{
    for (ProjectTabButton *button : buttons()) {
        if (button->tab->id == m_workspace.selectedID())
            ensureWidgetVisible(button, 0, 0);
    }
}
