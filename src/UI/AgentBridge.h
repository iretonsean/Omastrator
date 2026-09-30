#pragma once
#include "Agent/AgentHost.h"
#include "Agent/AgentLauncher.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Document/EditorSession.h"
#include "Document/Swatches.h"
#include "Live/AgentWork.h"
#include "Live/DeployJob.h"
#include "Live/EditSets.h"
#include "Live/History.h"
#include "Live/LiveSession.h"
#include "Live/WriteBack.h"
#include "UI/FloatingPanel.h"
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class DesignController;
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
    // Layers ▸ Name with AI: the agent renames the selection's layers (or the document's) for what they are, or by
    // `convention`; the names arrive as a proposal. An error to show, or empty once the agent is on its way.
    QString nameLayers(const QString &convention = {});
    QString vectorize(AgentLauncher::TraceMode mode);
    QString roast();

    enum class Task { generate, edit, vectorize, roast, live, deploy };
    struct Waiting {
        QString requestId;
        Task task;
        QString agent;
        qint64 started = QDateTime::currentMSecsSinceEpoch();
        // What it's doing now, in plain words, from the calls it makes; and how many edits so far.
        QString step = QString();
        int edits = 0;
    };
    const std::optional<Waiting> &waiting() const { return m_waiting; }
    // "Claude is roasting… 12 s", in the agent's own name.
    QString waitingText() const;
    static QString stepFor(const QString &method, int *edits);
    // Cancel: stops the agent's background run (with a Live worktree, or the deploy waiting for it); an agent in a
    // terminal may still answer, which lands as usual.
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
    QString design(const QString &action, const QJsonObject &params, QJsonObject &result) override;
    QString showWindow(const QStringList &files, bool raise) override;
    QString quitApp() override;

    // Shows the window if it's hidden (the app runs in the background), then raises it.
    void bringForward();
    // Design mode everywhere (docs/ANYWHERE.md): overlays, the floating bar, Ask and the Desk.
    DesignController &designMode() { return *m_design; }
    // Ask from the floating bar: the agent edits `overlay` as a proposal, as Edit with Instruction does the front
    // document. Returns why it couldn't start, or empty.
    QString askOnOverlay(EditorSession &overlay, const QString &requestId, const QString &prompt);
    // The session an Ask on the overlay is working in, while it lasts.
    EditorSession *designTarget() const { return m_designTarget; }

    // Live mode: a page or project in Omastrator's Chromium. $OMASTRATOR_LIVE_HEADLESS runs it headless, for tests.
    LiveSession &liveSession() { return m_live; }
    BrowserLink &browserLink() { return m_browserLink; }
    // `command` is an Electron app's command line; `app` opens `url` as an app window.
    QString startLive(const QUrl &url, const QString &folder, const QString &command = QString(), bool app = false);
    // Writes the live edits back: the certain ones directly, the rest through the agent. `agentRequest` gets the
    // agent's request id when it took some. Nothing opens; each write is recorded for Review changes.
    // `folder` names the project when the edits are in Browser Views rather than the window (else the window's).
    QString liveWriteBack(QString *agentRequest = nullptr, const QString &folder = QString());
    // The window's edits, the Browser Views' and the held ones, for a project.
    std::vector<LiveEdit> pendingEdits(const QString &folder) const;
    // "Ask AI…" in the page: the agent changes the code in a worktree of its own.
    QString liveAsk(const QString &instruction, const QJsonArray &elements, QString *agentRequest = nullptr, const QString &folder = QString());
    // Hand to agent: the front document as a mockup, for an app whose code is in `folder`.
    QString handToAgent(const QString &folder, const QString &instruction);
    // Hand to Agent from any surface (docs/ANYWHERE.md): a page that isn't yours, a lifted app, art on the overlay.
    struct HandOff {
        QString folder;
        QString instruction;
        // What it came from: "example.com/pricing", "foot".
        QString source;
        QString url;
        // The mockup as vectors, when there's art; `picture` stands in for it otherwise.
        std::optional<VectorDocument> art;
        QString picture;
        // The surface as it is now, and a page before its edits.
        QString screenshot;
        QString original;
        // A site's edits, kept or not, and its origin.
        std::vector<EditSets::Edit> edits;
        QString origin;
        // Build It (docs/LIVE-IN-FRAME.md, section 5): the Browser View this is built from. Its review is named `title`;
        // `backdrop` is the page under the art in mockup.png; `selectors`, `breakpoints` and `production` say where each shape
        // sits, the widths the site has, and the address the dev server stands in for; `pending` are the frame's edits.
        QUuid frame;
        QString title;
        QImage backdrop;
        QJsonArray selectors;
        QString breakpoints;
        QString production;
        std::vector<LiveEdit> pending;
    };
    // Packages it (render, SVG, lifted selectors, the edits as CSS) and runs the agent headlessly in a worktree of
    // `folder`; its change lands as a review. Returns why it couldn't start, or empty.
    QString handOff(const HandOff &handOff, QString *requestId = nullptr);
    // Generate a page (docs/MOTION.md, section 4): the agent writes a new project into `staging`, which already holds the
    // stack's template, and answers with agentDone. Until `done` says it wrote something, the bridge owns `staging` and removes it
    // when the run is stopped or fails; after that it is the caller's.
    struct PageRequest {
        QString staging;
        QString description;
        // The stack in a sentence, and the template's files (relative), as the agent is told them.
        QString stack;
        QStringList files;
        // The file the design system's tokens went into, or empty when the document has none.
        QString tokenFile;
    };
    struct PageResult {
        bool cancelled = false;
        // The agent's one line, when it answered.
        QString summary;
        // Why it stopped without an answer.
        QString error;
    };
    using PageDone = std::function<void(const PageResult &)>;
    // Returns why it couldn't start, or empty; `done` is called once, later.
    QString generatePage(const PageRequest &request, PageDone done);
    // Steps of a longer job, one line each, for the Live panel's Activity list.
    struct ActivityLine {
        enum class State { pending, running, done, failed };
        QString text;
        State state = State::pending;
    };
    const std::vector<ActivityLine> &activity() const { return m_activity; }
    // The agent is writing a page.
    bool writingPage() const { return !m_pages.empty(); }
    void setActivity(std::vector<ActivityLine> lines);
    // The Browser View a Build It is running for, or null; and when one finished (ms since the epoch), until the next action.
    QUuid buildingFrame() const;
    QString buildingAgent() const;
    qint64 builtAt(const QUuid &frame) const { return m_built.value(frame, 0); }
    void clearBuilt(const QUuid &frame);
    // A site that isn't yours (docs/ANYWHERE.md): keep, toggle, remove and export its edit sets, Before and After, and
    // Hand to Agent. `params` holds the set's `name`, `on`, a `path` to export to, the agent's `folder`.
    QString siteAction(const QString &action, const QJsonObject &params, QJsonObject &result);
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
    // The log of a Live agent run that stopped without its change; Show log opens it.
    QString liveLog() const { return m_liveLog; }
    QString showLiveLog();

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
        // A Browser View asked: the island and the status stream leave it to the frame's bar.
        bool fromFrame = false;
    };
    // Starts it in the background; returns why it couldn't, or empty. With nothing answered yet on a first deploy
    // (or a GitHub repository to offer), `needsAnswer` is set and nothing starts: the Deploy sheet asks.
    QString liveDeploy(const DeployRequest &request, bool *needsAnswer = nullptr);
    // `folder` is the project to save; empty is deployProject().
    QString liveSave(const QString &folder = QString(), bool fromFrame = false);
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
    // The project the island and the status stream act on: the window's, else the last one it deployed or handed over.
    QString deployProject() const;
    // The island's project once Live is stopped: set by the window's Live, a deploy from the island and a hand-over. Never by a Browser View.
    void rememberProject(const QString &folder);
    // The project the Review Changes and History panels show: the one a Browser View opened them for, else the selected
    // Browser View's own site, else deployProject().
    QString panelProject();
    struct DeployState {
        // idle, writing, committing, github, pushing, deploying, done, failed.
        QString stage = QStringLiteral("idle");
        QString message;
        QString url;
        QString log;
        bool running = false;
        // A command the agent used, to offer "Remember this command".
        QString suggested;
        // The project it ran for, and when it stopped (ms since the epoch; 0 while it runs), so a frame's bar shows its own.
        QString folder;
        qint64 finishedAt = 0;
        // The run deployed (a save doesn't), and it went through.
        bool deployed = false;
        // What the run is for: false for a Save, so a failure isn't called a failed deploy.
        bool deploy = true;
        // A Browser View started it: the island and the status stream don't show it.
        bool fromFrame = false;
    };
    const DeployState &deployState() const { return m_deployState; }
    QString rememberSuggested();
    // `gh auth status`, remembered for a minute unless `refresh`.
    GitHub::Auth githubAuth(bool refresh = false);
    QString connectGitHub();
    std::vector<History::Entry> history(const QString &folder = QString());
    // Brings back that commit's files as a new commit, pushed; Deploy is offered next.
    QString restoreVersion(const QString &sha, const QString &folder = QString());

    // The Live panel: Deploy first; Review changes shows the diffs only when asked.
    // A Browser View names its `folder`; the panels keep to it until both are closed.
    void showLivePanel(bool changes = false, const QString &folder = QString());
    void showHistoryPanel(const QString &folder = QString());
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
    // The Activity list changed.
    void activityChanged();
    // The floating bar asks for the Design System panel: on `session` (an overlay; null is the front tab),
    // with a site's scan to offer when it isn't empty.
    void designSystemRequested(EditorSession *session, const QJsonObject &siteScan, const QString &source);

private:
    // Checks for an agent, then launches; `task` starts waiting on success.
    QString launch(const QString &requestId, Task task, const QString &prompt, const QString &model = QString());
    // A background run ended; if its answer never came, say so.
    void runFinished(const QString &requestId, AgentRun &run);
    // A project task (Live, Hand to Agent, Deploy with agent) with project access in `directory`; returns why it couldn't start.
    QString launchProject(const QString &requestId, const QString &directory, const QString &name, const QString &prompt);
    void liveRunFinished(const QString &requestId, AgentRun &run);
    void deployRunFinished(AgentRun &run);
    // Cancels a Live agent's run; its worktree goes once it has stopped. An agent in a terminal is left to finish.
    void stopLiveJob(const QString &requestId);
    // Generate a page's side of agentDone, of a run that ended without one, and of Stop (AgentBridge+Generate.cpp).
    QString pageAgentDone(const QString &requestId, const QString &summary);
    void pageRunFinished(const QString &requestId, AgentRun &run);
    void stopPageJob(const QString &requestId);
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
    // Every headless Live and deploy run by request id, so Cancel reaches each one.
    std::map<QString, QPointer<AgentRun>> m_runs;
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
    // The user's Chromium, through Omastrator's extension (Live in a tab).
    BrowserLink m_browserLink;
    LiveSession m_live;
    std::vector<WriteBack::Review> m_reviews;
    // The project last opened in Live, which Deploy, Save and History keep acting on after Live stops.
    QString m_lastProject;
    // The project a Browser View opened the Live and History panels for; kept apart from m_lastProject, which is the island's.
    QString m_panelProject;
    void followFrame(const QString &folder);
    std::map<QString, AgentWork> m_liveJobs;
    // Build Its by request id, and when each frame's last one finished.
    struct Build {
        QUuid frame;
        QString title;
    };
    QHash<QString, Build> m_builds;
    QHash<QUuid, qint64> m_built;
    struct PageJob {
        PageRequest request;
        PageDone done;
    };
    std::map<QString, PageJob> m_pages;
    std::vector<ActivityLine> m_activity;
    QString m_liveMessage;
    QString m_liveLog;
    void wireDeploy();
    // Records a write-back that is already on disk.
    void record(const QString &title, const QString &summary, const std::vector<WriteBack::FileChange> &changes, const QString &folder,
                const QString &id = QString());
    // The pipeline: waits for the agent's write-backs, then commits and hands off to the job.
    void setStage(const QString &stage, const QString &message);
    void finishWriting();
    void commitAndShip();
    // `log` replaces the job's for Details, when an agent's run is what failed.
    void pipelineFailed(const QString &line, const QString &log = QString());
    // What a failed run left on disk: changes written and recorded but not committed, or empty.
    QString uncommittedNote(const QString &folder) const;
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
    // Set while an Ask from the floating bar works on the overlay: the agent's methods act there.
    QPointer<EditorSession> m_designTarget;
    std::unique_ptr<DesignController> m_design;
};
