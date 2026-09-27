#pragma once
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/Menus.h"
#include "UI/ProjectTabs.h"
#include "UI/ProjectWorkspace.h"
#include <QMainWindow>
#include <QToolBar>

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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void synchronize();
    void synchronizeSession();

    ProjectWorkspace &m_workspace;
    QToolBar *const m_toolbar;
    ProjectTabStrip *const m_tabs;
    AgentBridge *m_agent = nullptr;
    Menus *m_menus = nullptr;
    QAction *m_newTab = nullptr;
    QAction *m_fit = nullptr;
    QAction *m_actualSize = nullptr;
    QAction *m_zoomIn = nullptr;
    QAction *m_zoomOut = nullptr;
    ContentView *m_content = nullptr;
    // Held until its editor goes, which refers to its session.
    std::shared_ptr<ProjectTab> m_shownTab;
    QMetaObject::Connection m_sessionWatch;
};
