#include "UI/ProjectWorkspaceView.h"
#include <QCloseEvent>
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
    m_fit = action(m_toolbar, QStringLiteral("Fit"), QStringLiteral("Fit artboard in window (Ctrl+0)"), QStringLiteral("fitToolbar"));
    m_actualSize = action(m_toolbar, QStringLiteral("100%"), QStringLiteral("Actual size (Ctrl+1)"), QStringLiteral("actualSizeToolbar"));
    m_zoomIn = action(m_toolbar, QStringLiteral("+"), QStringLiteral("Zoom in (Ctrl+=)"), QStringLiteral("zoomInToolbar"));
    m_zoomOut = action(m_toolbar, QStringLiteral("−"), QStringLiteral("Zoom out (Ctrl+−)"), QStringLiteral("zoomOutToolbar"));
    addToolBar(Qt::TopToolBarArea, m_toolbar);
    m_workspace.window = this;
    m_menus = new Menus(m_workspace, *menuBar(), *this);
    connect(m_menus, &Menus::layersToggled, this, [this] { m_content->synchronizePanels(); });
    connect(m_menus, &Menus::propertiesToggled, this, [this] { m_content->synchronizePanels(); });
    connect(m_newTab, &QAction::triggered, this, [this] { m_workspace.newTab(); });
    connect(m_fit, &QAction::triggered, this, [this] { m_workspace.current().session.zoomToFit(); });
    connect(m_actualSize, &QAction::triggered, this, [this] { m_workspace.current().session.actualSize(); });
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_workspace.current().session.zoomIn(); });
    connect(m_zoomOut, &QAction::triggered, this, [this] { m_workspace.current().session.zoomOut(); });
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ProjectWorkspaceView::synchronize);
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
        m_content = new ContentView(front->session, &m_workspace, this);
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
    setWindowTitle(front.title() + QStringLiteral("[*] — Omastrator"));
    setWindowModified(drawn && front.session.isModified());
    setWindowFilePath(front.path.value_or(QString()));
}

void ProjectWorkspaceView::closeEvent(QCloseEvent *event)
{
    // The workspace asks first, then closes us with its mark.
    if (property("closeConfirmed").toBool()) {
        setProperty("closeConfirmed", QVariant());
        event->accept();
        return;
    }
    event->ignore();
    m_workspace.closeWindow(this);
}
