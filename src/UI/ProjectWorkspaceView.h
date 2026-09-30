#pragma once
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/Menus.h"
#include "UI/PageWorkspaces.h"
#include "UI/ProjectTabs.h"
#include "UI/ProjectWorkspace.h"
#include "UI/ShareController.h"
#include <QLabel>
#include <QMainWindow>
#include <QToolBar>
#include <QToolButton>

// The window: the front tab's editor under the tab strip.
class ProjectWorkspaceView : public QMainWindow {
    Q_OBJECT
public:
    explicit ProjectWorkspaceView(ProjectWorkspace &workspace, QWidget *parent = nullptr);

    ContentView *content() const { return m_content; }
    ProjectTabStrip *tabs() const { return m_tabs; }
    Menus *menus() const { return m_menus; }
    // The agent bridge; main starts its server.
    AgentBridge *agent() const { return m_agent; }
    // Share with client.
    ShareController *share() const { return m_share; }
    // Pages as Workspaces (View menu).
    PageWorkspaces *pageWorkspaces() const { return m_pageWorkspaces; }
    // The tray light's way in: the window forward, and the front editor's Ask field focused.
    void focusAsk();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void synchronize();
    void synchronizeSession();

    ProjectWorkspace &m_workspace;
    QToolBar *const m_toolbar;
    ProjectTabStrip *const m_tabs;
    AgentBridge *m_agent = nullptr;
    ShareController *m_share = nullptr;
    QToolButton *m_shareButton = nullptr;
    QToolButton *m_shareOptions = nullptr;
    Menus *m_menus = nullptr;
    PageWorkspaces *m_pageWorkspaces = nullptr;
    QAction *m_newTab = nullptr;
    QAction *m_fit = nullptr;
    QAction *m_actualSize = nullptr;
    QAction *m_zoomIn = nullptr;
    QAction *m_zoomOut = nullptr;
    QLabel *m_cloudStatus = nullptr;
    QAction *m_cloudAction = nullptr;
    ContentView *m_content = nullptr;
    // Held until its editor goes, which refers to its session.
    std::shared_ptr<ProjectTab> m_shownTab;
    QMetaObject::Connection m_sessionWatch;
};
