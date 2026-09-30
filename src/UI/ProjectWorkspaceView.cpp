#include "UI/ProjectWorkspaceView.h"
#include "UI/ContextBar.h"
#include "UI/SharePanels.h"
#include <QCloseEvent>
#include <QLineEdit>
#include <QToolButton>

namespace {
QAction *action(QToolBar *bar, const QString &text, const QString &help, const QString &name)
{
    QAction *made = bar->addAction(text);
    made->setToolTip(help);
    made->setObjectName(name);
    return made;
}
}

ProjectWorkspaceView::ProjectWorkspaceView(ProjectWorkspace &workspace, QWidget *parent)
    : QMainWindow(parent), m_workspace(workspace), m_toolbar(new QToolBar(this)), m_tabs(new ProjectTabStrip(workspace, this))
{
    setObjectName(QStringLiteral("workspaceWindow"));
    setMinimumSize(800, 520);
    resize(1280, 820);
    m_toolbar->setObjectName(QStringLiteral("toolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_newTab = action(m_toolbar, QStringLiteral("+"), QStringLiteral("New document (Ctrl+N)"), QStringLiteral("newTabToolbar"));
    m_toolbar->addWidget(m_tabs);
    // The strip takes the toolbar's free width; zooms stay right.
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_cloudStatus = new QLabel(m_toolbar);
    m_cloudStatus->setObjectName(QStringLiteral("cloudStatus"));
    m_cloudStatus->setForegroundRole(QPalette::PlaceholderText);
    m_toolbar->addWidget(m_cloudStatus);
    m_cloudAction = action(m_toolbar, QStringLiteral("Retry Now"), QString(), QStringLiteral("cloudAction"));
    m_fit = action(m_toolbar, QStringLiteral("Fit"), QStringLiteral("Fit artboard in window (Ctrl+0)"), QStringLiteral("fitToolbar"));
    m_actualSize = action(m_toolbar, QStringLiteral("100%"), QStringLiteral("Actual size (Ctrl+1)"), QStringLiteral("actualSizeToolbar"));
    m_zoomIn = action(m_toolbar, QStringLiteral("+"), QStringLiteral("Zoom in (Ctrl+=)"), QStringLiteral("zoomInToolbar"));
    m_zoomOut = action(m_toolbar, QStringLiteral("−"), QStringLiteral("Zoom out (Ctrl+−)"), QStringLiteral("zoomOutToolbar"));
    m_toolbar->addSeparator();
    // Share, at the top right: one press shares; the arrow beside it holds the choices.
    m_shareButton = new QToolButton(m_toolbar);
    m_shareButton->setObjectName(QStringLiteral("shareToolbar"));
    m_shareButton->setText(QStringLiteral("Share"));
    m_shareButton->setAccessibleName(QStringLiteral("Share"));
    m_shareButton->setAutoRaise(true);
    m_toolbar->addWidget(m_shareButton);
    m_shareOptions = new QToolButton(m_toolbar);
    m_shareOptions->setObjectName(QStringLiteral("shareOptionsToolbar"));
    m_shareOptions->setArrowType(Qt::DownArrow);
    m_shareOptions->setAccessibleName(QStringLiteral("Share Options"));
    m_shareOptions->setToolTip(QStringLiteral("Share options: format, destination, and what's been shared"));
    m_shareOptions->setAutoRaise(true);
    m_toolbar->addWidget(m_shareOptions);
    addToolBar(Qt::TopToolBarArea, m_toolbar);
    m_workspace.window = this;
    m_agent = new AgentBridge(m_workspace, *this);
    m_share = new ShareController(m_workspace, m_agent, *this);
    new ShareToast(*m_share, *this);
    connect(m_share, &ShareController::githubQuestion, this, [this] { SharePanels::confirmGitHub(*m_share, *this); });
    connect(m_share, &ShareController::changed, this, &ProjectWorkspaceView::synchronizeSession);
    connect(m_shareButton, &QToolButton::clicked, this, [this] { SharePanels::shareNow(*m_share); });
    connect(m_shareOptions, &QToolButton::clicked, this, [this] { SharePanels::showOptions(*m_share, *this); });
    m_menus = new Menus(m_workspace, *menuBar(), *this, m_agent, m_share);
    connect(m_menus, &Menus::layersToggled, this, [this] { m_content->synchronizePanels(); });
    connect(m_menus, &Menus::propertiesToggled, this, [this] { m_content->synchronizePanels(); });
    connect(m_newTab, &QAction::triggered, this, [this] { m_workspace.newTab(); });
    connect(m_cloudAction, &QAction::triggered, this, [this] {
        const QUuid front = m_workspace.current().id;
        if (m_workspace.uploadStatus(front).phase == CloudUploader::Phase::conflict)
            m_workspace.resolveConflict(front);
        else
            m_workspace.retryUpload(front);
    });
    connect(m_fit, &QAction::triggered, this, [this] { m_workspace.current().session.zoomToFit(); });
    connect(m_actualSize, &QAction::triggered, this, [this] { m_workspace.current().session.actualSize(); });
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_workspace.current().session.zoomIn(); });
    connect(m_zoomOut, &QAction::triggered, this, [this] { m_workspace.current().session.zoomOut(); });
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectWorkspaceView::synchronize);
    m_pageWorkspaces = new PageWorkspaces(m_workspace, *this);
    synchronize();
}

void ProjectWorkspaceView::synchronize()
{
    const std::shared_ptr<ProjectTab> front = m_workspace.tab(m_workspace.current().id);
    // A new front tab gets a fresh editor.
    if (m_shownTab != front) {
        // setCentralWidget deletes the old editor later; its tab lives on.
        if (m_content)
            connect(m_content, &QObject::destroyed, [keep = m_shownTab] {});
        m_shownTab = front;
        m_content = new ContentView(front->session, &m_workspace, this, m_agent);
        // The window owns the minimum; the editor may be shorter.
        m_content->setMinimumSize(0, 0);
        setCentralWidget(m_content);
        m_content->show();
        disconnect(m_sessionWatch);
        m_sessionWatch = connect(&front->session, &EditorSession::changed, this, &ProjectWorkspaceView::synchronizeSession);
        m_menus->watchFront(&m_content->canvas());
    }
    m_content->setEnabled(!m_workspace.isManaging());
    synchronizeSession();
}

void ProjectWorkspaceView::synchronizeSession()
{
    const ProjectTab &front = m_workspace.current();
    const bool drawn = front.session.hasDocument();
    m_newTab->setEnabled(!m_workspace.isManaging());
    for (QAction *zoom : {m_fit, m_actualSize, m_zoomIn, m_zoomOut})
        zoom->setEnabled(drawn);
    // Share says what it will share; this runs on every edit, so it reads no files.
    const bool site = m_share->liveRunning() || (!drawn && !m_share->liveProject().isEmpty());
    m_shareButton->setEnabled(!m_share->running() && (drawn || site));
    const QString key = m_menus ? m_menus->action(QStringLiteral("shareWithClient"))->shortcut().toString(QKeySequence::NativeText) : QString();
    const QString what = site ? QStringLiteral("Share a preview of the Live site") : QStringLiteral("Share %1").arg(m_share->scopeText());
    m_shareButton->setToolTip(key.isEmpty() ? what : QStringLiteral("%1 (%2)").arg(what, key));
    setWindowTitle(front.title() + QStringLiteral("[*] — Omastrator"));
    setWindowModified(drawn && front.session.isModified());
    setWindowFilePath(front.path.value_or(QString()));
    const CloudUploader::Status upload = m_workspace.uploadStatus(front.id);
    m_cloudStatus->setText(m_workspace.cloudStatusText());
    m_cloudStatus->setToolTip(upload.error);
    m_cloudAction->setText(upload.phase == CloudUploader::Phase::conflict ? QStringLiteral("Resolve…") : QStringLiteral("Retry Now"));
    m_cloudAction->setVisible(upload.phase == CloudUploader::Phase::conflict || upload.phase == CloudUploader::Phase::waiting);
}

void ProjectWorkspaceView::closeEvent(QCloseEvent *event)
{
    // In the background (`omastrator --daemon`) the window only hides: documents, overlays and the Desk stay open.
    if (property("background").toBool()) {
        event->ignore();
        hide();
        return;
    }
    // The workspace asks first, then closes us with its mark.
    if (property("closeConfirmed").toBool()) {
        setProperty("closeConfirmed", QVariant());
        event->accept();
        return;
    }
    event->ignore();
    m_workspace.closeWindow(this);
}

void ProjectWorkspaceView::focusAsk()
{
    if (!isVisible())
        show();
    raise();
    activateWindow();
    if (m_content && m_content->contextBar().askField())
        m_content->contextBar().askField()->setFocus(Qt::OtherFocusReason);
}
