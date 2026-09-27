#include "UI/AgentBridge.h"
#include "Agent/AgentProtocol.h"
#include "Logging.h"
#include "UI/AgentPanels.h"
#include "UI/AgentSheets.h"
#include "UI/ProjectWorkspace.h"
#include "UI/SwatchesPanel.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

namespace {
QString newRequestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// What the agent is doing, for "Claude is roasting…".
const char *verb(AgentBridge::Task task)
{
    switch (task) {
    case AgentBridge::Task::generate:
        return "generating";
    case AgentBridge::Task::edit:
        return "editing";
    case AgentBridge::Task::vectorize:
        return "tracing";
    case AgentBridge::Task::roast:
        return "roasting";
    case AgentBridge::Task::deploy:
        return "deploying";
    case AgentBridge::Task::live:
        break;
    }
    return "working";
}

// The task as the status and the log name call it.
QString taskName(AgentBridge::Task task)
{
    static const QStringList names{"generate", "edit", "vectorize", "roast", "live", "deploy"};
    return names.value(int(task));
}

QString openLog(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QStringLiteral("The log is gone.");
    const QString opener = qEnvironmentVariable("OMASTRATOR_XDG_OPEN", QStringLiteral("xdg-open"));
    if (!QProcess::startDetached(opener, {path}))
        return QStringLiteral("Could not open %1.").arg(path);
    return {};
}

// One line on how a run ended without its answer.
QString endMessage(const AgentRun &run)
{
    const QString name = AgentBridge::displayName(run.agent());
    if (run.end() == AgentRun::End::timedOut) {
        const int limit = run.timeoutSeconds();
        const bool minutes = limit % 60 == 0;
        const int count = minutes ? limit / 60 : limit;
        const QString unit = minutes ? QStringLiteral("minute") : QStringLiteral("second");
        return QStringLiteral("%1 ran out of time after %2 %3%4 and was stopped.")
            .arg(name)
            .arg(count)
            .arg(unit, count == 1 ? QString() : QStringLiteral("s"));
    }
    const QString line = run.lastLine();
    return line.isEmpty() ? QStringLiteral("%1 stopped without an answer.").arg(name)
                          : QStringLiteral("%1 stopped without an answer: %2").arg(name, line);
}
}

AgentBridge::AgentBridge(ProjectWorkspace &workspace, QWidget &window) : QObject(&window), m_workspace(workspace), m_window(window)
{
    connect(&m_tools, &AgentTools::proposalChanged, this, &AgentBridge::proposalChanged);
    m_variationsPanel.onClose = [this] {
        m_variationsPanel.close();
        m_resultsUnseen = false;
        m_server.statusMayHaveChanged();
    };
    m_swatchesPanel.onClose = [this] { m_swatchesPanel.close(); };
    // New swatches show where they landed.
    connect(&m_swatches, &Swatches::changed, this, [this] {
        if (!m_swatchesPanel.isVisible())
            showSwatchesPanel();
    });
    // A global swatch's new colour reaches every open document.
    connect(&m_swatches, &Swatches::globalSwatchRecolored, this, [this](const QString &id, const QColor &color) {
        for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs())
            tab->session.recolorSwatch(id, color);
    });
    m_roastPanel.onClose = [this] {
        m_roastPanel.close();
        m_resultsUnseen = false;
        m_server.statusMayHaveChanged();
    };
    for (auto signal : {&AgentBridge::proposalChanged, &AgentBridge::waitingChanged, &AgentBridge::variationsChanged,
                        &AgentBridge::roastChanged})
        connect(this, signal, &m_server, &AgentServer::statusMayHaveChanged);
    connect(&m_live, &LiveSession::changed, &m_server, &AgentServer::statusMayHaveChanged);
    connect(this, &AgentBridge::liveReviewChanged, &m_server, &AgentServer::statusMayHaveChanged);
    wireDeploy();
    // "Ask AI…" in the page; a refusal is said in the page's own bar.
    connect(&m_live, &LiveSession::askRequested, this, [this](const QString &prompt, const QJsonArray &elements) {
        if (const QString failure = liveAsk(prompt, elements); !failure.isEmpty())
            m_live.notice(failure);
    });
    connect(&m_workspace, &ProjectWorkspace::changed, this, &AgentBridge::watchFront);
    watchFront();
    // The elapsed time ticks only while something is waited on, whoever started it.
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &AgentBridge::waitingTick);
    connect(this, &AgentBridge::waitingChanged, this, [this] {
        if (!m_waiting)
            m_tick.stop();
        else if (!m_tick.isActive())
            m_tick.start();
    });
}

void AgentBridge::watchFront()
{
    disconnect(m_frontWatch);
    m_frontWatch = connect(&m_workspace.current().session, &EditorSession::changed, &m_server, &AgentServer::statusMayHaveChanged);
    m_server.statusMayHaveChanged();
}

QJsonObject AgentBridge::statusExtras()
{
    QJsonObject extras{{"summary", proposalSummary()}, {"waiting", waitingText()},
                       {"error", m_panelMessage.isEmpty() ? m_barMessage : m_panelMessage},
                       {"log", m_logPath},
                       {"task", m_waiting ? taskName(m_waiting->task) : QString()},
                       {"agent", m_waiting ? displayName(m_waiting->agent) : QString()},
                       {"ready", m_tools.hasProposal() || m_resultsUnseen},
                       {"roastId", m_roast ? m_roast->requestId : QString()},
                       {"live", [this] {
                            QJsonObject live = m_live.status();
                            live["reviews"] = int(m_reviews.size());
                            live["unsaved"] = unsavedFiles();
                            live["working"] = m_waiting && m_waiting->task == Task::live;
                            live["liveMessage"] = m_liveMessage;
                            // Deploy, Review changes and History act on this project, with Live stopped too.
                            live["deployProject"] = deployProject();
                            live["deploy"] = QJsonObject{{"stage", m_deployState.stage}, {"message", m_deployState.message},
                                                         {"url", m_deployState.url}, {"running", m_deployState.running},
                                                         {"failed", m_deployState.stage == QLatin1String("failed")},
                                                         {"suggested", m_deployState.suggested}};
                            return live;
                        }()}};
    // The newest round that has come back.
    for (auto round = m_rounds.rbegin(); round != m_rounds.rend(); ++round) {
        if (!round->variations.empty()) {
            extras["variations"] = int(round->variations.size());
            extras["variationsId"] = round->requestId;
            break;
        }
    }
    return extras;
}

AgentBridge::~AgentBridge() = default;

QString AgentBridge::startServer(const QString &path)
{
    m_serverError = path.isEmpty() ? m_server.listen() : m_server.listen(path);
    if (!m_serverError.isEmpty())
        qCWarning(lcApp).noquote() << "agent bridge not listening:" << m_serverError;
    return m_serverError;
}

bool AgentBridge::hasProposalIn(const EditorSession &session) const
{
    return m_tools.proposalSession() == &session;
}

QString AgentBridge::proposalTitle() const
{
    return m_tools.proposalTitle();
}

QString AgentBridge::proposalSummary() const
{
    return m_tools.hasProposal() ? m_summary : QString();
}

void AgentBridge::keepProposal()
{
    m_resultsUnseen = false;
    if (EditorSession *open = m_tools.proposalSession())
        open->commitInteraction();
    m_summary.clear();
    m_variationProposed = false;
    emit proposalChanged();
}

void AgentBridge::discardProposal()
{
    m_resultsUnseen = false;
    if (EditorSession *open = m_tools.proposalSession())
        open->cancelInteraction();
    m_summary.clear();
    m_variationProposed = false;
    emit proposalChanged();
}

QString AgentBridge::displayName(const QString &agent)
{
    static const QHash<QString, QString> names{
        {"claude", "Claude"}, {"codex", "Codex"}, {"opencode", "OpenCode"}, {"gemini", "Gemini"}, {"copilot", "Copilot"},
        {"crush", "Crush"}, {"grok", "Grok"}, {"cursor-agent", "Cursor"}, {"pi", "Pi"}, {"omp", "Oh My Pi"},
        {"hermes", "Hermes"}, {"muse", "Muse"}, {"openclaw", "OpenClaw"}};
    return names.value(agent, agent.isEmpty() ? QStringLiteral("the agent") : agent);
}

QString AgentBridge::waitingText() const
{
    if (!m_waiting)
        return {};
    const QString text = QStringLiteral("%1 is %2…").arg(displayName(m_waiting->agent), QLatin1String(verb(m_waiting->task)));
    const qint64 seconds = (QDateTime::currentMSecsSinceEpoch() - m_waiting->started) / 1000;
    return seconds > 0 ? QStringLiteral("%1 %2 s").arg(text).arg(seconds) : text;
}

void AgentBridge::stopWaiting()
{
    // A deploy waiting on this agent stops with it; a Live agent's worktree goes when it has stopped.
    if (m_waiting && m_pipeline.active
        && (m_pipeline.waitingFor.contains(m_waiting->requestId) || m_pipeline.agentRequest == m_waiting->requestId))
        cancelDeploy();
    else if (m_waiting && m_waiting->task == Task::live)
        stopLiveJob(m_waiting->requestId);
    if (m_run)
        m_run->cancel();
    m_run = nullptr;
    if (!m_waiting)
        return;
    m_waiting.reset();
    emit waitingChanged();
}

QString AgentBridge::showLog()
{
    return openLog(m_logPath);
}

QString AgentBridge::showLiveLog()
{
    return openLog(m_liveLog);
}

void AgentBridge::dismissBarMessage()
{
    m_barMessage.clear();
    emit proposalChanged();
}

QString AgentBridge::launch(const QString &requestId, Task task, const QString &prompt)
{
    // An older run's log goes with its message.
    m_logPath.clear();
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    AgentLauncher::LaunchOptions options;
    options.task = taskName(task);
    options.finished = [bridge = QPointer<AgentBridge>(this), requestId](AgentRun &run) {
        if (bridge)
            bridge->runFinished(requestId, run);
    };
    QPointer<AgentRun> run;
    error = AgentLauncher::launch(prompt, m_server.isListening() ? m_server.path() : QString(), options, &run);
    if (!error.isEmpty())
        return error;
    m_run = run;
    m_waiting = Waiting{requestId, task, agent};
    m_panelMessage.clear();
    m_barMessage.clear();
    m_logPath.clear();
    emit waitingChanged();
    return {};
}

void AgentBridge::runFinished(const QString &requestId, AgentRun &run)
{
    if (m_run == &run)
        m_run = nullptr;
    m_runs.erase(requestId);
    // Live and deploy runs answer to their worktree and pipeline, whatever is waited on now.
    if (m_liveJobs.count(requestId))
        return liveRunFinished(requestId, run);
    if (m_pipeline.active && m_pipeline.agentRequest == requestId && m_job.running())
        return deployRunFinished(run);
    // Answered, cancelled, or replaced by a newer task: nothing to say.
    if (!m_waiting || m_waiting->requestId != requestId || run.end() == AgentRun::End::cancelled)
        return;
    const Task task = m_waiting->task;
    const QString message = endMessage(run);
    m_logPath = run.logPath();
    m_waiting.reset();
    if (task == Task::edit || task == Task::vectorize)
        m_barMessage = message;
    else
        m_panelMessage = message;
    emit waitingChanged();
    if (task == Task::roast) {
        showRoastPanel();
        emit roastChanged();
    } else if (task == Task::generate) {
        emit variationsChanged();
        showVariationsPanel();
    } else {
        emit proposalChanged();
    }
}

QString AgentBridge::launchProject(const QString &requestId, const QString &directory, const QString &name, const QString &prompt)
{
    const AgentLauncher::LaunchOptions options{AgentAccess::project, directory, name, 0,
                                               [bridge = QPointer<AgentBridge>(this), requestId](AgentRun &run) {
                                                   if (bridge)
                                                       bridge->runFinished(requestId, run);
                                               }};
    QPointer<AgentRun> run;
    if (const QString error = AgentLauncher::launch(prompt, m_server.isListening() ? m_server.path() : QString(), options, &run); !error.isEmpty())
        return error;
    m_run = run;
    if (run)
        m_runs[requestId] = run;
    return {};
}

void AgentBridge::liveRunFinished(const QString &requestId, AgentRun &run)
{
    auto job = m_liveJobs.find(requestId);
    job->second.cleanup();
    m_liveJobs.erase(job);
    if (m_waiting && m_waiting->requestId == requestId) {
        m_waiting.reset();
        emit waitingChanged();
    }
    const bool deploying = m_pipeline.active && m_pipeline.waitingFor.removeAll(requestId) > 0;
    const QString message = run.end() == AgentRun::End::cancelled ? QStringLiteral("Cancelled.") : endMessage(run);
    if (deploying)
        return pipelineFailed(message, run.end() == AgentRun::End::cancelled ? QString() : run.logPath());
    if (run.end() != AgentRun::End::cancelled) {
        m_liveMessage = message;
        m_liveLog = run.logPath();
        if (m_live.state() == LiveSession::State::running)
            m_live.notice(message);
    }
    emit liveReviewChanged();
}

void AgentBridge::deployRunFinished(AgentRun &run)
{
    if (m_waiting && m_waiting->task == Task::deploy) {
        m_waiting.reset();
        emit waitingChanged();
    }
    if (run.end() == AgentRun::End::cancelled)
        return m_job.cancel();
    // The job's finished handler says "Deploy failed: …"; Details then opens the agent's own log.
    m_job.agentFinished(QString(), endMessage(run));
    m_deployState.log = run.logPath();
    emit liveReviewChanged();
}

void AgentBridge::stopLiveJob(const QString &requestId)
{
    auto job = m_liveJobs.find(requestId);
    auto found = m_runs.find(requestId);
    if (job == m_liveJobs.end() || found == m_runs.end() || !found->second || !found->second->isRunning())
        return;
    AgentRun *run = found->second;
    AgentWork work = job->second;
    m_liveJobs.erase(job);
    m_runs.erase(found);
    // The agent may still be writing in the worktree until it has stopped.
    connect(run, &AgentRun::finished, run, [work]() mutable { work.cleanup(); });
    run->cancel();
    if (m_run == run)
        m_run = nullptr;
    if (m_waiting && m_waiting->requestId == requestId) {
        m_waiting.reset();
        emit waitingChanged();
    }
    emit liveReviewChanged();
}

QString AgentBridge::generate(const QString &brief, int count, bool fitToSelection)
{
    if (brief.trimmed().isEmpty())
        return QStringLiteral("Describe what to generate.");
    EditorSession &front = *session();
    Round round;
    round.requestId = newRequestId();
    round.instruction = brief.trimmed();
    if (fitToSelection && front.hasSelection())
        round.fitTo = front.selectionBounds();
    const QString error = launch(round.requestId, Task::generate,
                                 AgentLauncher::generatePrompt(round.requestId, round.instruction, count, round.fitTo));
    if (!error.isEmpty())
        return error;
    m_rounds.push_back(round);
    emit variationsChanged();
    showVariationsPanel();
    return {};
}

QString AgentBridge::refine(const QString &instruction)
{
    if (instruction.trimmed().isEmpty())
        return QStringLiteral("Say how to change the chosen variation.");
    if (!m_chosen)
        return QStringLiteral("Pick a variation to refine first.");
    // The instructions that led here, oldest first, ending at the chosen variation.
    std::vector<AgentLauncher::Round> history;
    for (std::optional<std::pair<int, int>> step = m_chosen; step; step = m_rounds[size_t(step->first)].from) {
        const Round &round = m_rounds[size_t(step->first)];
        history.insert(history.begin(), {round.instruction, history.empty() ? round.variations[size_t(step->second)].svg : QString()});
    }
    Round round;
    round.requestId = newRequestId();
    round.instruction = instruction.trimmed();
    round.fitTo = m_rounds[size_t(m_chosen->first)].fitTo;
    round.from = m_chosen;
    const int count = std::max(1, int(m_rounds[size_t(m_chosen->first)].variations.size()));
    const QString error = launch(round.requestId, Task::generate,
                                 AgentLauncher::generatePrompt(round.requestId, round.instruction, count, round.fitTo, history));
    if (!error.isEmpty())
        return error;
    m_rounds.push_back(round);
    emit variationsChanged();
    return {};
}

QString AgentBridge::editWithInstruction(const QString &instruction)
{
    if (instruction.trimmed().isEmpty())
        return QStringLiteral("Describe the change.");
    const QString requestId = newRequestId();
    return launch(requestId, Task::edit, AgentLauncher::editPrompt(requestId, instruction.trimmed(), session()->hasSelection()));
}

QString AgentBridge::vectorize(AgentLauncher::TraceMode mode)
{
    EditorSession &front = *session();
    const std::optional<QUuid> image = front.selectedImage();
    if (!image)
        return QStringLiteral("Select one placed image to vectorize.");
    // No agent means no trace either: nothing changes until it can finish.
    QString error;
    if (AgentLauncher::defaultAgent(&error).isEmpty())
        return error;
    QJsonObject traced;
    try {
        const bool logo = mode == AgentLauncher::TraceMode::logo;
        traced = m_tools.call(QStringLiteral("trace_image"), {{"id", image->toString(QUuid::WithoutBraces)},
                                                              {"mode", logo ? "color" : "blackAndWhite"}, {"colors", 4}});
    } catch (const AgentProtocol::Error &failure) {
        return failure.message();
    }
    const QString requestId = newRequestId();
    // The rough trace stays as a proposal the user can keep even if the launch fails.
    return launch(requestId, Task::vectorize,
                  AgentLauncher::smartTracePrompt(requestId, traced["id"].toString(), traced["imagePath"].toString(), mode));
}

QString AgentBridge::roast()
{
    EditorSession &front = *session();
    if (!front.hasDocument())
        return QStringLiteral("Open a document to roast.");
    QString error;
    if (AgentLauncher::defaultAgent(&error).isEmpty()) {
        m_panelMessage = error;
        m_logPath.clear();
        showRoastPanel();
        emit roastChanged();
        return error;
    }
    const bool selectionOnly = front.hasSelection();
    QString path;
    try {
        path = m_tools.call(QStringLiteral("render"), {{"selectionOnly", selectionOnly}, {"scale", 2}})["path"].toString();
    } catch (const AgentProtocol::Error &failure) {
        return failure.message();
    }
    const QString requestId = newRequestId();
    error = launch(requestId, Task::roast, AgentLauncher::roastPrompt(requestId, path, selectionOnly, AgentLauncher::savedRoastHeat()));
    m_panelMessage = error;
    if (error.isEmpty())
        m_roast.reset();
    showRoastPanel();
    emit roastChanged();
    return error;
}

QString AgentBridge::insertVariation(int roundIndex, int index)
{
    if (roundIndex < 0 || roundIndex >= int(m_rounds.size()) || index < 0 || index >= int(m_rounds[size_t(roundIndex)].variations.size()))
        return QStringLiteral("That variation is gone.");
    const Round &round = m_rounds[size_t(roundIndex)];
    const AgentVariation &variation = round.variations[size_t(index)];
    EditorSession &front = *session();
    // Picking another variation swaps it for the one on show.
    if (m_variationProposed && hasProposalIn(front))
        front.cancelInteraction();
    QJsonObject params{{"svg", variation.svg}, {"name", variation.name}};
    if (round.fitTo) {
        params["at"] = QJsonArray{round.fitTo->x(), round.fitTo->y()};
        params["fit"] = QJsonArray{round.fitTo->width(), round.fitTo->height()};
    } else {
        // Where the eye already is, not the SVG's own corner.
        params["center"] = true;
    }
    try {
        m_tools.call(QStringLiteral("insert_svg"), params);
        m_tools.call(QStringLiteral("proposal_finish"), {{"title", "Generate"}, {"summary", variation.name}});
    } catch (const AgentProtocol::Error &failure) {
        return failure.message();
    }
    m_variationProposed = true;
    m_resultsUnseen = false;
    m_chosen = std::pair(roundIndex, index);
    emit variationsChanged();
    return {};
}

void AgentBridge::showVariationsPanel()
{
    if (!m_variationsContent || !m_variationsPanel.isVisible()) {
        m_variationsContent = new VariationsPanel(*this);
        m_variationsPanel.show(QStringLiteral("Variations"), m_variationsContent);
    }
}

void AgentBridge::showRoastPanel()
{
    if (!m_roastContent || !m_roastPanel.isVisible()) {
        m_roastContent = new RoastPanel(*this);
        m_roastPanel.show(QStringLiteral("Roast My Design"), m_roastContent);
    }
}

void AgentBridge::showSwatchesPanel()
{
    if (!m_swatchesContent || !m_swatchesPanel.isVisible()) {
        m_swatchesContent = new SwatchesPanel(m_swatches, [this]() -> EditorSession & { return *session(); });
        m_swatchesPanel.show(QStringLiteral("Swatches"), m_swatchesContent);
    }
}

QString AgentBridge::newDocument(QSizeF size)
{
    if (m_workspace.isManaging())
        return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
    m_workspace.createDocument(size);
    return {};
}

QString AgentBridge::showNewDocument()
{
    if (m_workspace.isManaging())
        return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
    m_workspace.newTab();
    m_window.raise();
    m_window.activateWindow();
    return {};
}

QString AgentBridge::showPanel(const QString &panel)
{
    m_window.raise();
    m_window.activateWindow();
    if (panel == QLatin1String("swatches"))
        showSwatchesPanel();
    else if (panel == QLatin1String("variations"))
        showVariationsPanel();
    else if (panel == QLatin1String("roast"))
        showRoastPanel();
    else if (panel == QLatin1String("connectAgent"))
        // A sheet has its own event loop; open it after this call returns.
        QMetaObject::invokeMethod(this, [this] { AgentSheets::connectAgent(*this, &m_window); }, Qt::QueuedConnection);
    else
        return QStringLiteral("There is no panel “%1”.").arg(panel);
    return {};
}

QString AgentBridge::startAi(const AiRequest &request)
{
    auto forward = [this] {
        m_window.raise();
        m_window.activateWindow();
    };
    // Sheets run their own loop; they open after this call answers.
    auto openSheet = [this, forward](QDialog *(*open)(AgentBridge &, QWidget *)) {
        forward();
        QMetaObject::invokeMethod(this, [this, open] { open(*this, &m_window); }, Qt::QueuedConnection);
        return QString();
    };
    if (m_workspace.isManaging())
        return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
    if (request.flow == QLatin1String("cancel")) {
        stopWaiting();
        return {};
    }
    if (request.flow == QLatin1String("handoff"))
        return openSheet(&AgentSheets::handoff);
    if (request.flow == QLatin1String("roast")) {
        forward();
        return roast();
    }
    if (request.flow == QLatin1String("generate")) {
        if (request.prompt.isEmpty())
            return openSheet([](AgentBridge &bridge, QWidget *window) { return AgentSheets::generate(bridge, window); });
        return generate(request.prompt, request.count, request.fitToSelection);
    }
    if (request.flow == QLatin1String("edit")) {
        if (request.prompt.isEmpty())
            return openSheet(&AgentSheets::editWithInstruction);
        return editWithInstruction(request.prompt);
    }
    if (request.flow == QLatin1String("vectorize")) {
        const auto mode = request.sketch ? AgentLauncher::TraceMode::sketch : AgentLauncher::TraceMode::logo;
        if (session()->hasDocument() && session()->selectedImage())
            return vectorize(mode);
        if (m_tools.pendingCapture())
            return vectorizeCapture(mode);
        return QStringLiteral("Select one placed image, or take a screenshot in Capture mode first.");
    }
    return QStringLiteral("There is no AI flow “%1”.").arg(request.flow);
}

QString AgentBridge::vectorizeCapture(AgentLauncher::TraceMode mode)
{
    const auto capture = m_tools.pendingCapture();
    if (!capture)
        return QStringLiteral("The traced screenshot is gone. Take another in Capture mode.");
    if (session()->isInteracting())
        return QStringLiteral("Finish the edit or proposal in front first.");
    const QString requestId = newRequestId();
    return launch(requestId, Task::vectorize,
                  AgentLauncher::smartTracePrompt(requestId, capture->group.toString(QUuid::WithoutBraces), capture->imagePath, mode));
}

EditorSession *AgentBridge::session()
{
    return &m_workspace.current().session;
}

QString AgentBridge::quietly(const std::function<bool()> &run)
{
    QString failure;
    m_workspace.errorHandler = [&](const QString &title, const QString &message) { failure = title + QStringLiteral(": ") + message; };
    const bool ok = run();
    m_workspace.errorHandler = nullptr;
    if (!ok && failure.isEmpty())
        failure = QStringLiteral("It could not be done.");
    return ok ? QString() : failure;
}

QString AgentBridge::openFile(const QString &path)
{
    if (m_workspace.isManaging())
        return QStringLiteral("Omastrator is showing a dialog. Try again when the user has answered it.");
    return quietly([&] { return m_workspace.openFile(path); });
}

QString AgentBridge::saveFile(const QString &path)
{
    ProjectTab &tab = m_workspace.current();
    const QString target = path.isEmpty() ? tab.path.value_or(QString()) : path;
    if (target.isEmpty())
        return QStringLiteral("This document has no file yet. Pass a path ending in .omai.");
    return quietly([&] { return m_workspace.saveTo(tab, target); });
}

void AgentBridge::showVariations(const QString &requestId, const std::vector<AgentVariation> &variations)
{
    auto round = std::find_if(m_rounds.begin(), m_rounds.end(), [&](const Round &each) { return each.requestId == requestId; });
    if (round == m_rounds.end()) {
        // An agent the user connected themselves: its answer is a round of its own.
        m_rounds.push_back(Round{requestId, QStringLiteral("From the agent"), std::nullopt, std::nullopt, {}});
        round = m_rounds.end() - 1;
    }
    round->variations = variations;
    m_resultsUnseen = true;
    if (m_waiting && m_waiting->requestId == requestId)
        m_waiting.reset();
    emit waitingChanged();
    emit variationsChanged();
    showVariationsPanel();
}

void AgentBridge::showRoast(const AgentRoast &roast)
{
    m_roast = roast;
    m_resultsUnseen = true;
    m_panelMessage.clear();
    if (m_waiting && m_waiting->requestId == roast.requestId)
        m_waiting.reset();
    emit waitingChanged();
    emit roastChanged();
    showRoastPanel();
}

void AgentBridge::proposalFinished(const QString &, const QString &summary)
{
    m_summary = summary;
    if (m_waiting && (m_waiting->task == Task::edit || m_waiting->task == Task::vectorize)) {
        m_waiting.reset();
        emit waitingChanged();
    }
    emit proposalChanged();
}
