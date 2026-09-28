#pragma once
#include <array>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

class QFile;
class QProcess;
class QProcessEnvironment;
class QTimer;

// What a headless agent may do without asking: Omastrator's own flows only read
// what they're given and call the Omastrator CLI; project tasks edit code in their folder.
enum class AgentAccess { omastrator, project };

// One headless agent run: a background process, its log, and how it ended.
class AgentRun : public QObject {
    Q_OBJECT
public:
    enum class End { running, exited, crashed, timedOut, cancelled };
    ~AgentRun() override;

    QString agent() const { return m_agent; }
    QString task() const { return m_task; }
    QString logPath() const { return m_logPath; }
    qint64 elapsedSeconds() const { return m_clock.elapsed() / 1000; }
    int timeoutSeconds() const { return m_timeoutMs / 1000; }
    End end() const { return m_end; }
    int exitCode() const { return m_exitCode; }
    bool isRunning() const { return m_end == End::running; }
    // The last line on stderr worth showing, trimmed; stdout's when the run failed quietly.
    QString lastLine() const;
    // Stops the run and everything it started.
    void cancel();

signals:
    void finished();

private:
    friend struct AgentRunStarter;
    AgentRun() = default;
    void append(const QByteArray &bytes, bool error);
    void stop(End end);
    void close(End end);

    QString m_agent;
    QString m_task;
    QString m_logPath;
    QProcess *m_process = nullptr;
    QFile *m_log = nullptr;
    QTimer *m_timeout = nullptr;
    QElapsedTimer m_clock;
    int m_timeoutMs = 0;
    End m_end = End::running;
    // Cancel or the timeout, while the process still stops.
    End m_requested = End::running;
    qint64 m_pid = 0;
    int m_exitCode = -1;
    QString m_stderrTail;
    QString m_stdoutTail;
};

// Hands work to Omarchy's default agent. Claude Code, Codex, opencode and Gemini
// run headless in the background; any other agent (or the showTerminal setting)
// goes through `omarchy agent prompt <prompt>`, in a terminal. The folder the
// agent starts in tells it how to drive Omastrator. $OMASTRATOR_OMARCHY replaces
// `omarchy`, for tests.
namespace AgentLauncher {
// The agent Omarchy launches, or empty with `error` set.
QString defaultAgent(QString *error = nullptr);
// ~/.local/share/omastrator/agent, or under $XDG_DATA_HOME.
QString folder();
// $XDG_STATE_HOME/omastrator/agent-runs: one log per headless run, the last 30 kept.
QString logFolder();
// AGENTS.md and CLAUDE.md in `directory`; an old .mcp.json there is removed. Returns why it failed, or empty.
QString writeInstructions(const QString &directory, const QString &binary, const QString &socket);
// The instructions AGENTS.md and CLAUDE.md hold.
QString instructions(const QString &binary);

// QSettings agent/showTerminal: open the agent in a terminal while it works.
bool showTerminal();
void setShowTerminal(bool show);
// QSettings agent/timeoutSeconds, else 5 minutes for Omastrator's flows and 20 for project tasks.
int timeoutSeconds(AgentAccess access);

// How a headless agent is started: the program, its arguments, and variables to add.
struct Command {
    QString program;
    QStringList arguments;
    // NAME=value pairs added to the agent's environment.
    QStringList environment;
};
// The headless command for `agent`, or nothing when it only runs in a terminal. `binary` is Omastrator's own.
std::optional<Command> headlessCommand(const QString &agent, AgentAccess access, const QString &prompt, const QString &binary,
                                      const QString &model = QString());
// The model small, watched edits use (overlay asks): $OMASTRATOR_QUICK_MODEL, else "sonnet"; "default" is the agent's own.
QString quickModel();

struct LaunchOptions {
    AgentAccess access = AgentAccess::omastrator;
    // Where the agent starts; empty is folder(), with its AGENTS.md. Nothing is written into another folder.
    QString workingDirectory;
    // Names the log: roast, generate, edit, vectorize, live…
    QString task = QStringLiteral("task");
    // 0 is timeoutSeconds(access).
    int timeoutSeconds = 0;
    // Called once when a headless run ends, however it ended; the run is deleted afterwards.
    std::function<void(AgentRun &)> finished;
    // A model for the agent's own --model, where it has one (Claude); empty is the agent's default.
    QString model = QString();
};
// Writes the folder, then launches the agent. `run` gets the headless run, or null in a terminal. Returns why it failed, or empty.
QString launch(const QString &taskPrompt, const QString &socket, const LaunchOptions &options, QPointer<AgentRun> *run = nullptr);
// Omastrator's own flows: AgentAccess::omastrator, in folder().
QString launch(const QString &taskPrompt, const QString &socket = QString());
// A project task in `directory` (a worktree): AgentAccess::project, writing nothing there.
QString launchIn(const QString &directory, const QString &taskPrompt, const QString &socket = QString());

// One round of Generate: what was asked, and the SVG the user picked from it.
struct Round {
    QString instruction;
    QString chosenSvg;
};

// The four flows in docs/AI-DESIGN.md; each names its request id and the methods that deliver it.
QString generatePrompt(const QString &requestId, const QString &brief, int count, std::optional<QRectF> fitTo,
                       const std::vector<Round> &history = {});
QString editPrompt(const QString &requestId, const QString &instruction, bool hasSelection);
// Name Layers: rename the selection's objects (or the document's) for what they are, or by the user's `convention`.
QString namePrompt(const QString &requestId, const QString &convention, bool hasSelection);
// Ask in design mode's floating bar (docs/ANYWHERE.md): the agent works on the overlay drawn over a surface.
// `context` describes the surface and what is pointed at; `screenshot` is a PNG of it, or empty.
QString surfacePrompt(const QString &requestId, const QString &instruction, const QString &context, const QString &screenshot,
                      bool hasSelection);
enum class TraceMode { logo, sketch };
QString smartTracePrompt(const QString &requestId, const QString &traceGroupId, const QString &imagePath, TraceMode mode);
// How hard Roast My Design hits; the user picks it and it is remembered.
enum class RoastHeat { friendly, spicy, savage, unhinged };
inline constexpr std::array allRoastHeats{RoastHeat::friendly, RoastHeat::spicy, RoastHeat::savage, RoastHeat::unhinged};
QString title(RoastHeat heat);
std::optional<RoastHeat> roastHeat(const QString &title);
RoastHeat savedRoastHeat();
void saveRoastHeat(RoastHeat heat);
// The heat's aim, limits and calibration lines, as the roast task carries them.
QString roastHeatGuide(RoastHeat heat);
QString roastPrompt(const QString &requestId, const QString &renderPath, bool selectionOnly, RoastHeat heat = RoastHeat::savage);
}

// Starts headless runs for AgentLauncher::launch, which alone makes them.
struct AgentRunStarter {
    static AgentRun *start(const QString &agent, const QString &task, const AgentLauncher::Command &command, const QString &prompt,
                           const QProcessEnvironment &environment, const QString &directory, int timeoutMs, QString *error);
};
