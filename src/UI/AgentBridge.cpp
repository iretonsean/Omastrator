#include "UI/AgentBridge.h"
#include "Agent/AgentProtocol.h"
#include "Logging.h"
#include "UI/AgentPanels.h"
#include "UI/ProjectWorkspace.h"
#include <QJsonArray>

namespace {
QString newRequestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

AgentBridge::AgentBridge(ProjectWorkspace &workspace, QWidget &window) : QObject(&window), m_workspace(workspace), m_window(window)
{
    connect(&m_tools, &AgentTools::proposalChanged, this, &AgentBridge::proposalChanged);
    m_variationsPanel.onClose = [this] { m_variationsPanel.close(); };
    m_roastPanel.onClose = [this] { m_roastPanel.close(); };
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
    if (EditorSession *open = m_tools.proposalSession())
        open->commitInteraction();
    m_summary.clear();
    m_variationProposed = false;
    emit proposalChanged();
}

void AgentBridge::discardProposal()
{
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
    if (m_waiting && m_waiting->requestId == requestId)
        m_waiting.reset();
    emit waitingChanged();
    emit variationsChanged();
    showVariationsPanel();
}

void AgentBridge::showRoast(const AgentRoast &roast)
{
    m_roast = roast;
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
