#pragma once
#include "UI/ProjectWorkspace.h"
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

// One tab: its title, the unsaved dot, its close button.
class ProjectTabButton : public QWidget {
    Q_OBJECT
public:
    ProjectTabButton(ProjectWorkspace &workspace, std::shared_ptr<ProjectTab> tab, QWidget *parent = nullptr);

    const std::shared_ptr<ProjectTab> tab;
    // What the session and the workspace hold, shown.
    void synchronize();
    bool isActive() const;
    QString text() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ProjectWorkspace &m_workspace;
    QToolButton *const m_select;
    QToolButton *const m_close;
};

// The row of tabs, one a document, scrolling when long.
class ProjectTabStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit ProjectTabStrip(ProjectWorkspace &workspace, QWidget *parent = nullptr);

    QList<ProjectTabButton *> buttons() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void showFront();

    ProjectWorkspace &m_workspace;
    QWidget *const m_row;
};
