#pragma once
#include "Agent/AgentHost.h"
#include "Agent/AgentLauncher.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Document/Swatches.h"
#include "Live/AgentWork.h"
#include "Live/DeployJob.h"
#include "Live/History.h"
#include "Live/LiveSession.h"
#include "Live/WriteBack.h"
#include "UI/FloatingPanel.h"
#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QRectF>
#include <QTimer>
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

    enum class Task { generate, edit, vectorize, roast, live, deploy };
    struct Waiting {
        QString requestId;
        Task task;
        QString agent;
        qint64 started = QDateTime::currentMSecsSinceEpoch();
    };
    const std::optional<Waiting> &waiting() const { return m_waiting; }
    // "Claude is roasting… 12 s", in the agent's own name.
    QString waitingText() const;
    // Cancel: stops the agent's background run; an agent in a terminal may still answer, which lands as usual.
    void stopWaiting();
    static QString displayName(const QString &agent);
    // The run in the background, or null when the agent is in a terminal or done.
    AgentRun *run() const { return m_run; }
    // The log of the last run that stopped without an answer; Show log opens it.
    QString logPath() const { return m_logPath; }
    QString showLog();
    // An edit or trace that stopped without an answer, which the proposal bar shows until dismissed.
    QString barMessage() const { return m_barMessage; }
    void dismissBarMessage();

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
    // Writes the live edits back: the certain ones directly, the rest through the agent. `agentRequest` gets the
    // agent's request id when it took some. Nothing opens; each write is recorded for Review changes.
    QString liveWriteBack(QString *agentRequest = nullptr);
    // "Ask AI…" in the page: the agent changes the code in a worktree of its own.
    QString liveAsk(const QString &instruction, const QJsonArray &elements, QString *agentRequest = nullptr);
    // Hand to agent: the front document as a mockup, for an app whose code is in `folder`.
    QString handToAgent(const QString &folder, const QString &instruction);
    // The agent says it's done: its changes are written and recorded.
    QString liveAgentDone(const QString &requestId, const QString &summary);
    // Every write-back this session, oldest first: the background record Review changes shows.
    const std::vector<WriteBack::Review> &liveReviews() const { return m_reviews; }
    // Takes a write-back out again. Committed, it becomes a new commit that reverts those files, pushed like a save.
    // Empty `id`: every write-back not yet committed, newest first.
    QString discardReview(const QString &id);
    // Files written but not yet committed.
    int unsavedFiles() const;
    QString liveMessage() const { return m_liveMessage; }

    // Deploy-first Live (docs/OS-SUITE.md): write back, commit, push, deploy, with no review in between.
    struct DeployRequest {
        // False: Save, which stops after the push.
        bool deploy = true;
        // The production confirmation was answered.
        bool confirm = false;
        // "Don't ask again for this project".
        bool remember = false;
        // A name: create that private GitHub repository first. Empty: not now, and don't ask again.
        std::optional<QString> github;
        // Default: Live's project, else the last one.
        QString folder;
    };
    // Starts it in the background; returns why it couldn't, or empty. With nothing answered yet on a first deploy
    // (or a GitHub repository to offer), `needsAnswer` is set and nothing starts: the Deploy sheet asks.
    QString liveDeploy(const DeployRequest &request, bool *needsAnswer = nullptr);
    QString liveSave();
    // What the Deploy sheet asks for the project.
    struct DeployQuestion {
        QString folder;
        Deploy::Command command;
        // Production needs confirming (first deploy, or "Don't ask again" wasn't chosen).
        bool confirm = false;
        // A GitHub repository to offer: its suggested name; empty when there's nothing to offer.
        QString github;
        // The agent that deploys when there's no command.
        QString agent;
    };
    DeployQuestion deployQuestion(const QString &folder = QString());
    void cancelDeploy();
    // `live_deployed` from the agent deploying.
    QString liveDeployed(const QString &requestId, const QString &url, const QString &command, const QString &error);
    // The project Deploy, Save and History act on.
    QString deployProject() const;
    struct DeployState {
        // idle, writing, committing, github, pushing, deploying, done, failed.
        QString stage = QStringLiteral("idle");
        QString message;
        QString url;
        QString log;
        bool running = false;
        // A command the agent used, to offer "Remember this command".
        QString suggested;
    };
    const DeployState &deployState() const { return m_deployState; }
    QString rememberSuggested();
    // `gh auth status`, remembered for a minute unless `refresh`.
    GitHub::Auth githubAuth(bool refresh = false);
    QString connectGitHub();
    std::vector<History::Entry> history();
    // Brings back that commit's files as a new commit, pushed; Deploy is offered next.
    QString restoreVersion(const QString &sha);

    // The Live panel: Deploy first; Review changes shows the diffs only when asked.
    void showLivePanel(bool changes = false);
    void showHistoryPanel();
    // Details: the last deploy's log.
    QString showDeployLog();
    FloatingPanel &reviewPanel() { return m_reviewPanel; }
    FloatingPanel &historyPanel() { return m_historyPanel; }
    // Vectorize with AI on the last screenshot Capture traced.
    QString vectorizeCapture(AgentLauncher::TraceMode mode);

    // Window ▸ Swatches.
    void showSwatchesPanel();
    FloatingPanel &swatchesPanel() { return m_swatchesPanel; }

signals:
    // A proposal opened, grew, was renamed or ended.
    void proposalChanged();
    void waitingChanged();
    // Once a second while waiting, for the elapsed time.
    void waitingTick();
    void variationsChanged();
    void roastChanged();
    // Reviews, kept files, the Live message.
    void liveReviewChanged();

private:
    // Checks for an agent, then launches; `task` starts waiting on success.
    QString launch(const QString &requestId, Task task, const QString &prompt);
    // A background run ended; if its answer never came, say so.
    void runFinished(const QString &requestId, AgentRun &run);
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
    QPointer<AgentRun> m_run;
    QTimer m_tick;
    QString m_logPath;
    QString m_barMessage;
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
    // The project last opened in Live, which Deploy, Save and History keep acting on after Live stops.
    QString m_lastProject;
    std::map<QString, AgentWork> m_liveJobs;
    QString m_liveMessage;
    void wireDeploy();
    // Records a write-back that is already on disk.
    void record(const QString &title, const QString &summary, const std::vector<WriteBack::FileChange> &changes, const QString &folder,
                const QString &id = QString());
    // The pipeline: waits for the agent's write-backs, then commits and hands off to the job.
    void setStage(const QString &stage, const QString &message);
    void finishWriting();
    void commitAndShip();
    void pipelineFailed(const QString &line);
    void launchDeployAgent();
    QString startSave(const QString &folder, const QString &doneMessage);
    struct Pipeline {
        bool active = false;
        QString folder;
        bool deploy = true;
        Deploy::Command command;
        QString github;
        QStringList waitingFor;
        QString agentRequest;
        // Said when a save finishes, instead of "Saved".
        QString doneMessage;
    };
    Pipeline m_pipeline;
    DeployState m_deployState;
    DeployJob m_job;
    std::optional<std::pair<qint64, GitHub::Auth>> m_github;
    FloatingPanel m_reviewPanel{QStringLiteral("liveReviewPanel"), m_window};
    QPointer<QWidget> m_reviewContent;
    FloatingPanel m_historyPanel{QStringLiteral("liveHistoryPanel"), m_window};
    QPointer<QWidget> m_historyContent;
    FloatingPanel m_swatchesPanel{QStringLiteral("swatchesPanel"), m_window};
    QPointer<QWidget> m_swatchesContent;
};
