#include "UI/AgentBridge.h"
#include "UI/DesignController.h"
#include "UI/ProjectWorkspace.h"
#include <QCoreApplication>

// The bridge's side of design mode everywhere (docs/ANYWHERE.md) and of
// running in the background: the window is shown only when asked for.

void AgentBridge::bringForward()
{
    if (!m_window.isVisible())
        m_window.show();
    m_window.raise();
    m_window.activateWindow();
}

QString AgentBridge::design(const QString &action, const QJsonObject &params, QJsonObject &result)
{
    return m_design->run(action, params, result);
}

QString AgentBridge::showWindow(const QStringList &files, bool raise)
{
    // Work on the canvas from the island shows the window if it's hidden, without pulling a shown one forward.
    if (!raise && files.isEmpty()) {
        if (!m_window.isVisible())
            bringForward();
        return {};
    }
    if (m_workspace.isManaging()) {
        bringForward();
        return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
    }
    bringForward();
    m_workspace.receive(files);
    return {};
}

QString AgentBridge::quitApp()
{
    if (m_workspace.isManaging())
        return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
    const bool unsaved = std::any_of(m_workspace.tabs().begin(), m_workspace.tabs().end(),
                                     [](const std::shared_ptr<ProjectTab> &tab) { return tab->session.isModified(); });
    // The questions about unsaved documents need the window.
    if (unsaved)
        bringForward();
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_workspace.confirmQuit([](bool confirmed) {
                if (confirmed)
                    QCoreApplication::quit();
            });
        },
        Qt::QueuedConnection);
    return {};
}

QString AgentBridge::askOnOverlay(EditorSession &overlay, const QString &requestId, const QString &prompt)
{
    if (m_waiting)
        return QStringLiteral("%1 Wait for it, or stop it from the island.").arg(waitingText());
    if (m_tools.hasProposal())
        return QStringLiteral("Keep or discard the preview that's open first.");
    m_designTarget = &overlay;
    const QString error = launch(requestId, Task::edit, prompt);
    if (!error.isEmpty())
        m_designTarget = nullptr;
    return error;
}
