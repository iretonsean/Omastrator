#include "UI/AgentBridge.h"
#include "Agent/AgentProtocol.h"
#include "Logging.h"
#include "UI/AgentPanels.h"
#include "UI/AgentSheets.h"
#include "UI/ProjectWorkspace.h"
#include "UI/SwatchesPanel.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace {
QString newRequestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    m_reviewPanel.onClose = [this] { m_reviewPanel.close(); };
    // "Ask AI…" in the page; a refusal is said in the page's own bar.
    connect(&m_live, &LiveSession::askRequested, this, [this](const QString &prompt, const QJsonArray &elements) {
        if (const QString failure = liveAsk(prompt, elements, false); !failure.isEmpty())
            m_live.notice(failure);
    });
    connect(&m_workspace, &ProjectWorkspace::changed, this, &AgentBridge::watchFront);
    watchFront();
}

void AgentBridge::watchFront()
{
    disconnect(m_frontWatch);
    m_frontWatch = connect(&m_workspace.current().session, &EditorSession::changed, &m_server, &AgentServer::statusMayHaveChanged);
    m_server.statusMayHaveChanged();
}

QJsonObject AgentBridge::statusExtras()
{
    static const QStringList tasks{"generate", "edit", "vectorize", "roast", "live"};
    QJsonObject extras{{"summary", proposalSummary()}, {"waiting", waitingText()}, {"error", m_panelMessage},
                       {"task", m_waiting ? tasks.value(int(m_waiting->task)) : QString()},
                       {"agent", m_waiting ? displayName(m_waiting->agent) : QString()},
                       {"ready", m_tools.hasProposal() || m_resultsUnseen},
                       {"roastId", m_roast ? m_roast->requestId : QString()},
                       {"live", [this] {
                            QJsonObject live = m_live.status();
                            live["reviews"] = int(m_reviews.size());
                            live["unsaved"] = unsavedFiles();
                            live["working"] = m_waiting && m_waiting->task == Task::live;
                            live["liveMessage"] = m_liveMessage;
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
    return m_waiting ? QStringLiteral("Waiting for %1…").arg(displayName(m_waiting->agent)) : QString();
}

void AgentBridge::stopWaiting()
{
    if (!m_waiting)
        return;
    m_waiting.reset();
    emit waitingChanged();
}

QString AgentBridge::launch(const QString &requestId, Task task, const QString &prompt)
{
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    error = AgentLauncher::launch(prompt, m_server.isListening() ? m_server.path() : QString());
    if (!error.isEmpty())
        return error;
    m_waiting = Waiting{requestId, task, agent};
    m_panelMessage.clear();
    emit waitingChanged();
    return {};
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
    error = launch(requestId, Task::roast, AgentLauncher::roastPrompt(requestId, path, selectionOnly));
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
