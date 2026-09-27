#pragma once
#include "Agent/AgentHost.h"
#include "Agent/AgentLauncher.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "UI/FloatingPanel.h"
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <optional>
#include <vector>

class ProjectWorkspace;

// The app's side of docs/AI-DESIGN.md: the host the tools edit through, the
// socket, the four flows the menus start, and the results they bring back.
class AgentBridge : public QObject, public AgentHost {
    Q_OBJECT
public:
    AgentBridge(ProjectWorkspace &workspace, QWidget &window);
    ~AgentBridge() override;

    // Listens on the socket; returns why it could not, or empty. `path` defaults to AgentProtocol::socketPath().
    QString startServer(const QString &path = QString());
    QString serverPath() const { return m_server.path(); }
    QString serverError() const { return m_serverError; }
    AgentTools &tools() { return m_tools; }

    // The proposal the canvas shows an accept bar for.
    bool hasProposalIn(const EditorSession &session) const;
    QString proposalTitle() const;
    QString proposalSummary() const;
    // Enter and Esc: one undo step, or none.
    void keepProposal();
    void discardProposal();

    // The flows. Each returns why it could not start, or empty.
    QString generate(const QString &brief, int count, bool fitToSelection);
    // Another round from the chosen variation.
    QString refine(const QString &instruction);
    QString editWithInstruction(const QString &instruction);
    QString vectorize(AgentLauncher::TraceMode mode);
    QString roast();

    enum class Task { generate, edit, vectorize, roast };
    struct Waiting {
        QString requestId;
        Task task;
        QString agent;
    };
    const std::optional<Waiting> &waiting() const { return m_waiting; }
    // "Waiting for Claude…", in the agent's own name.
    QString waitingText() const;
    // Cancel: the app stops waiting; an answer that still comes lands as usual.
    void stopWaiting();
    static QString displayName(const QString &agent);

    struct Round {
        QString requestId;
        QString instruction;
        std::optional<QRectF> fitTo;
        // The round and variation this one refines.
        std::optional<std::pair<int, int>> from;
        std::vector<AgentVariation> variations;
    };
    const std::vector<Round> &rounds() const { return m_rounds; }
    std::optional<std::pair<int, int>> chosen() const { return m_chosen; }
    // Inserts one variation as a proposal; returns why it could not, or empty.
    QString insertVariation(int round, int index);
    const std::optional<AgentRoast> &roastResult() const { return m_roast; }
    // A plain line the panels show: a launch failure, or an answer that couldn't be used.
    QString panelMessage() const { return m_panelMessage; }

    void showVariationsPanel();
    void showRoastPanel();
    FloatingPanel &variationsPanel() { return m_variationsPanel; }
    FloatingPanel &roastPanel() { return m_roastPanel; }

    // AgentHost.
    EditorSession *session() override;
    QString openFile(const QString &path) override;
    QString saveFile(const QString &path) override;
    void showVariations(const QString &requestId, const std::vector<AgentVariation> &variations) override;
    void showRoast(const AgentRoast &roast) override;
    void proposalFinished(const QString &title, const QString &summary) override;

signals:
    // A proposal opened, grew, was renamed or ended.
    void proposalChanged();
    void waitingChanged();
    void variationsChanged();
    void roastChanged();

private:
    // Checks for an agent, then launches; `task` starts waiting on success.
    QString launch(const QString &requestId, Task task, const QString &prompt);
    QString quietly(const std::function<bool()> &run);

    ProjectWorkspace &m_workspace;
    QWidget &m_window;
    AgentTools m_tools{*this};
    AgentServer m_server{m_tools};
    QString m_serverError;
    QString m_summary;
    std::optional<Waiting> m_waiting;
    std::vector<Round> m_rounds;
    std::optional<std::pair<int, int>> m_chosen;
    // The open proposal is a picked variation: picking another replaces it.
    bool m_variationProposed = false;
    std::optional<AgentRoast> m_roast;
    QString m_panelMessage;
    FloatingPanel m_variationsPanel{QStringLiteral("variationsPanel"), m_window};
    FloatingPanel m_roastPanel{QStringLiteral("roastPanel"), m_window};
    QPointer<QWidget> m_variationsContent;
    QPointer<QWidget> m_roastContent;
};
