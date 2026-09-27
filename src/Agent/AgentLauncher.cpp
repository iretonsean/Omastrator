#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
// Linux caps one argument at 128 KiB; longer tasks travel as a file.
constexpr qsizetype maximumPromptArgument = 96 * 1024;
const QString chooseAgent = QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent.");

QString omarchy()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_OMARCHY");
    return overridden.isEmpty() ? QStringLiteral("omarchy") : overridden;
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}

// The old way, and the showTerminal setting: `omarchy agent prompt` opens the agent in a terminal.
QString launchInTerminal(const QString &agent, const QString &prompt, const QString &directory, const QProcessEnvironment &environment)
{
    auto *process = new QProcess;
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(directory);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->start(omarchy(), {QStringLiteral("agent"), QStringLiteral("prompt"), prompt});
    if (!process->waitForStarted(3000)) {
        const QString reason = process->errorString();
        delete process;
        return QStringLiteral("Could not launch %1: %2").arg(agent, reason);
    }
    // An immediate failure is reported; a terminal that stays open is left to run.
    if (process->waitForFinished(200)) {
        const bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
        const QString reason = QString::fromUtf8(process->readAllStandardError()).trimmed();
        delete process;
        return ok ? QString() : QStringLiteral("Could not launch %1: %2").arg(agent, reason.isEmpty() ? QStringLiteral("it exited at once.") : reason);
    }
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    qCInfo(lcApp).noquote() << "launched" << agent << "in a terminal in" << directory;
    return {};
}
}

namespace AgentLauncher {
QString defaultAgent(QString *error)
{
    auto failed = [&](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };
    QProcess process;
    process.start(omarchy(), {QStringLiteral("default"), QStringLiteral("agent")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000))
        return failed(QStringLiteral("Omarchy's agent launcher is unavailable. To connect an agent yourself, run: "
                                     "claude mcp add omastrator -- omastrator --mcp"));
    const QString agent = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || agent.isEmpty())
        return failed(chooseAgent);
    if (QStandardPaths::findExecutable(agent).isEmpty())
        return failed(QStringLiteral("%1 is selected but is not installed. %2").arg(agent, chooseAgent));
    return agent;
}

QString folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("omastrator/agent"));
}

QString writeInstructions(const QString &directory, const QString &binary, const QString &)
{
    if (!QDir().mkpath(directory))
        return QStringLiteral("Could not create %1.").arg(directory);
    const QByteArray text = instructions(binary).toUtf8();
    for (const QString &name : {QStringLiteral("AGENTS.md"), QStringLiteral("CLAUDE.md")}) {
        const QString failure = writeFile(QDir(directory).filePath(name), text);
        if (!failure.isEmpty())
            return failure;
    }
    // An MCP server here made Claude Code ask for approval on every run; headless runs use the CLI.
    QFile::remove(QDir(directory).filePath(QStringLiteral(".mcp.json")));
    return {};
}

bool showTerminal()
{
    return QSettings().value(QStringLiteral("agent/showTerminal"), false).toBool();
}

void setShowTerminal(bool show)
{
    QSettings().setValue(QStringLiteral("agent/showTerminal"), show);
}

int timeoutSeconds(AgentAccess access)
{
    const int saved = QSettings().value(QStringLiteral("agent/timeoutSeconds"), 0).toInt();
    if (saved > 0)
        return saved;
    return access == AgentAccess::project ? 20 * 60 : 5 * 60;
}

QString launch(const QString &taskPrompt, const QString &listening)
{
    return launch(taskPrompt, listening, LaunchOptions{});
}

QString launchIn(const QString &workingDirectory, const QString &taskPrompt, const QString &listening)
{
    LaunchOptions options;
    options.access = AgentAccess::project;
    options.workingDirectory = workingDirectory;
    options.task = QStringLiteral("live");
    return launch(taskPrompt, listening, options);
}

QString launch(const QString &taskPrompt, const QString &listening, const LaunchOptions &options, QPointer<AgentRun> *run)
{
    if (run)
        *run = nullptr;
    if (taskPrompt.trimmed().isEmpty())
        return QStringLiteral("There is nothing to ask the agent.");
    QString error;
    const QString agent = defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString directory = folder();
    const QString binary = QCoreApplication::applicationFilePath();
    // Where this app listens, which a test or second instance may have moved.
    const QString socket = listening.isEmpty() ? AgentProtocol::socketPath() : listening;
    if (const QString failure = writeInstructions(directory, binary, socket); !failure.isEmpty())
        return failure;
    QString prompt = taskPrompt;
    if (prompt.toUtf8().size() > maximumPromptArgument) {
        const QString task = QDir(directory).filePath(QStringLiteral("TASK.md"));
        if (const QString failure = writeFile(task, prompt.toUtf8()); !failure.isEmpty())
            return failure;
        prompt = options.workingDirectory.isEmpty()
                     ? QStringLiteral("Your Omastrator task is in TASK.md in the working directory. Read it, and AGENTS.md, then do it.")
                     : QStringLiteral("Your Omastrator task is in %1. Read it, then do it.").arg(task);
    }

    // Arguments go straight to the process: the prompt never passes through a shell.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OMASTRATOR_BIN"), binary);
    environment.insert(QStringLiteral("OMASTRATOR_SOCKET"), socket);
    // Started from inside a Claude Code session, the agent would think it is nested.
    for (const char *nested : {"CLAUDECODE", "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_SSE_PORT"})
        environment.remove(QString::fromLatin1(nested));
    const QString workingDirectory = options.workingDirectory.isEmpty() ? directory : options.workingDirectory;

    const std::optional<Command> command = showTerminal() ? std::nullopt : headlessCommand(agent, options.access, prompt, binary);
    if (!command)
        return launchInTerminal(agent, prompt, workingDirectory, environment);
    const int timeout = (options.timeoutSeconds > 0 ? options.timeoutSeconds : timeoutSeconds(options.access)) * 1000;
    AgentRun *started = AgentRunStarter::start(agent, options.task, *command, prompt, environment, workingDirectory, timeout, &error);
    if (!started)
        return error;
    if (options.finished) {
        const auto finished = options.finished;
        QObject::connect(started, &AgentRun::finished, started, [started, finished] { finished(*started); });
    }
    if (run)
        *run = started;
    return {};
}
}
