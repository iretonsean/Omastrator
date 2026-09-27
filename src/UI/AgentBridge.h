#pragma once
#include "Agent/AgentHost.h"
#include "Agent/AgentLauncher.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Document/Swatches.h"
#include "Live/AgentWork.h"
#include "Live/LiveSession.h"
#include "Live/WriteBack.h"
#include "UI/FloatingPanel.h"
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <map>
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

    enum class Task { generate, edit, vectorize, roast, live };
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
    QJsonObject statusExtras() override;
    Swatches *swatches() override { return &m_swatches; }
    QString newDocument(QSizeF size) override;
    QString showNewDocument() override;
    QString showPanel(const QString &panel) override;
    QString startAi(const AiRequest &request) override;
    QString live(const QString &action, const QJsonObject &params, QJsonObject &result) override;

    // Live mode: a page or project in Omastrator's Chromium. $OMASTRATOR_LIVE_HEADLESS runs it headless, for tests.
    LiveSession &liveSession() { return m_live; }
    // `command` is an Electron app's command line; `app` opens `url` as an app window.
    QString startLive(const QUrl &url, const QString &folder, const QString &command = QString(), bool app = false);
    // Writes the live edits back: the certain ones directly, the rest through the agent. `confirm` accepts uncommitted changes.
    QString liveWriteBack(bool confirm);
    // "Ask AI…" in the page: the agent changes the code in a worktree of its own.
    QString liveAsk(const QString &instruction, const QJsonArray &elements, bool confirm);
    // Hand to agent: the front document as a mockup, for an app whose code is in `folder`.
    QString handToAgent(const QString &folder, const QString &instruction, bool confirm);
    // The agent says it's done: its changes become a review.
    QString liveAgentDone(const QString &requestId, const QString &summary, bool confirm);
    const std::vector<WriteBack::Review> &liveReviews() const { return m_reviews; }
    // Empty `id`: every review.
    QString keepReview(const QString &id);
    QString discardReview(const QString &id);
    // Commits what was kept since the last save.
    QString liveSave();
    int unsavedFiles() const;
    std::vector<WriteBack::PublishOption> publishOptions() const;
    // Runs one publish option; nothing runs without `confirm`. `output` gets what it printed.
    QString livePublish(const QString &option, bool confirm, QString *output);
    QString liveMessage() const { return m_liveMessage; }
    // A write-back or agent task refused for the user's uncommitted changes, which the review panel can confirm.
    bool canConfirm() const { return static_cast<bool>(m_confirm); }
    QString confirmPending();
    void showReviewPanel();
    FloatingPanel &reviewPanel() { return m_reviewPanel; }
    // Vectorize with AI on the last screenshot Capture traced.
    QString vectorizeCapture(AgentLauncher::TraceMode mode);

    // Window ▸ Swatches.
    void showSwatchesPanel();
    FloatingPanel &swatchesPanel() { return m_swatchesPanel; }

signals:
    // A proposal opened, grew, was renamed or ended.
    void proposalChanged();
    void waitingChanged();
    void variationsChanged();
    void roastChanged();
    // Reviews, kept files, the Live message.
    void liveReviewChanged();

private:
    // Checks for an agent, then launches; `task` starts waiting on success.
    QString launch(const QString &requestId, Task task, const QString &prompt);
    QString quietly(const std::function<bool()> &run);
    // Follows the front tab's session, so its tool and document reach status followers.
    void watchFront();

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
    // Variations or a roast arrived that the user hasn't acted on: the tray light shows "ready".
    bool m_resultsUnseen = false;
    QMetaObject::Connection m_frontWatch;
    FloatingPanel m_variationsPanel{QStringLiteral("variationsPanel"), m_window};
    FloatingPanel m_roastPanel{QStringLiteral("roastPanel"), m_window};
    QPointer<QWidget> m_variationsContent;
    QPointer<QWidget> m_roastContent;
    Swatches m_swatches;
    LiveSession m_live;
    std::vector<WriteBack::Review> m_reviews;
    // Kept since the last save, per project folder: the files and the lines for the commit message.
    std::map<QString, std::pair<QStringList, QStringList>> m_kept;
    // Where Publish acts when Live isn't running: the last project saved.
    QString m_publishFolder;
    std::map<QString, AgentWork> m_liveJobs;
    QString m_liveMessage;
    std::function<QString()> m_confirm;
    FloatingPanel m_reviewPanel{QStringLiteral("liveReviewPanel"), m_window};
    QPointer<QWidget> m_reviewContent;
    FloatingPanel m_swatchesPanel{QStringLiteral("swatchesPanel"), m_window};
    QPointer<QWidget> m_swatchesContent;
};
