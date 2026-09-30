#include "Agent/Hyprland.h"
#include "UI/PageWorkspaces.h"
#include <QJsonObject>
#include <algorithm>

void PageWorkspaces::workspaceEntered(const QString &name)
{
    if (m_claims.empty())
        return;
    m_arrivedAt = name;
    m_arriveTimer.start();
}

// The user landed on a workspace: if it is a page's, that page becomes the current one.
void PageWorkspaces::arrived()
{
    const QString name = std::exchange(m_arrivedAt, QString());
    const auto claim = std::find_if(m_claims.begin(), m_claims.end(), [&](const Claim &c) { return c.name == name; });
    // A numbered workspace, or anyone else's, isn't ours: nothing follows the user out.
    if (claim == m_claims.end())
        return;
    const Claim target = *claim;
    const std::shared_ptr<ProjectTab> tab = m_workspace.tab(target.tab);
    if (!tab)
        return;
    // Our own moves echo here: the page is already current, so there is nothing to do.
    if (m_workspace.selectedID() == target.tab && tab->session.currentPage() == target.page)
        return;
    const bool focused = followsFocus();
    m_fromHyprland = true;
    m_reconcileTimer.stop();
    if (m_workspace.selectedID() != target.tab)
        m_workspace.select(target.tab);
    tab->session.setCurrentPage(target.page);
    // The editor still shows the page it is leaving: paint the new one now, so the frame it arrives in is right.
    m_editor.repaint();
    reconcile();
    m_fromHyprland = false;
    m_followNext = false;
    if (!focused || m_editorAddress.isEmpty())
        return;
    // Only if they are still there: a fast run of Super+Tab has moved on.
    QString error;
    const QJsonValue active = Hyprland::query(QStringLiteral("activeworkspace"), &error);
    if (error.isEmpty() && active.toObject()["name"].toString() == target.name)
        Hyprland::focusWindow(m_editorAddress);
}

QString PageWorkspaces::goTo(const QString &workspace)
{
    if (!isActive())
        return QStringLiteral("Pages as Workspaces is off, or Omastrator has no window on show.");
    const auto claim = std::find_if(m_claims.begin(), m_claims.end(), [&](const Claim &c) { return c.name == workspace; });
    const std::shared_ptr<ProjectTab> tab = claim == m_claims.end() ? nullptr : m_workspace.tab(claim->tab);
    if (!tab)
        return QStringLiteral("No page holds the workspace “%1”.").arg(workspace);
    const Claim target = *claim;
    if (m_workspace.selectedID() == target.tab && tab->session.currentPage() == target.page) {
        // The editor is there already: only the user may not be.
        QString error;
        const QJsonValue active = Hyprland::query(QStringLiteral("activeworkspace"), &error);
        if (error.isEmpty() && active.toObject()["name"].toString() == target.name)
            return {};
        return Hyprland::focusWorkspace(Hyprland::workspaceSelector(0, target.name));
    }
    m_reconcileTimer.stop();
    m_followAnyway = true;
    if (m_workspace.selectedID() != target.tab)
        m_workspace.select(target.tab);
    tab->session.setCurrentPage(target.page);
    // The page change cleared the request for a follow when nothing has focus.
    m_followNext = true;
    reconcile();
    m_followAnyway = false;
    m_followNext = false;
    return {};
}
