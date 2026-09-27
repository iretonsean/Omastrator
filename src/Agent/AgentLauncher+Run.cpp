#include "Agent/AgentLauncher.h"
#include "Agent/Setup.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <csignal>
#include <unistd.h>

namespace {
constexpr int keptLogs = 30;
// How much of each stream is kept to find the last line in.
constexpr qsizetype tailSize = 8 * 1024;

QString keepTail(QString tail, const QString &more)
{
    tail += more;
    return tail.size() > tailSize ? tail.right(tailSize) : tail;
}

// The last line with a word in it, without terminal colours.
QString lastMeaningful(const QString &text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[A-Za-z]"));
    static const QRegularExpression word(QStringLiteral("\\w{2,}"));
    QStringList lines = QString(text).remove(ansi).split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
    for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
        const QString trimmed = line->trimmed();
        if (word.match(trimmed).hasMatch())
            return trimmed.size() > 200 ? trimmed.left(199) + QChar(0x2026) : trimmed;
    }
    return {};
}

// The command as the log shows it: the task itself is left out.
QString described(const AgentLauncher::Command &command, const QString &prompt)
{
    QStringList words{command.program};
    for (const QString &argument : command.arguments)
        words << (argument == prompt ? QStringLiteral("<the task, %1 characters>").arg(prompt.size()) : Setup::shellQuote(argument));
    return words.join(QLatin1Char(' '));
}

void signalGroup(qint64 pid, int signal)
{
    if (pid > 0)
        ::kill(-pid_t(pid), signal);
}
}

namespace AgentLauncher {
QString logFolder()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || QDir::isRelativePath(state))
        state = QDir::home().filePath(QStringLiteral(".local/state"));
    return QDir(state).filePath(QStringLiteral("omastrator/agent-runs"));
}

std::optional<Command> headlessCommand(const QString &agent, AgentAccess access, const QString &prompt, const QString &binary)
{
    const bool project = access == AgentAccess::project;
    // The one command Omastrator's own flows may run: its CLI, by the absolute path the prompt gives.
    const QString cli = Setup::shellQuote(binary) + QStringLiteral(" agent");
    const QString name = QFileInfo(agent).fileName();
    if (name == QLatin1String("claude")) {
        QStringList arguments{QStringLiteral("-p"), prompt, QStringLiteral("--output-format"), QStringLiteral("text"),
                              QStringLiteral("--no-session-persistence"),
                              // No MCP servers at all, so nothing asks to be approved.
                              QStringLiteral("--strict-mcp-config"), QStringLiteral("--mcp-config"), QStringLiteral(R"({"mcpServers":{}})"),
                              QStringLiteral("--permission-mode"), QStringLiteral("acceptEdits"), QStringLiteral("--tools")};
        if (project) {
            arguments << QStringLiteral("Bash,Read,Edit,Write,Glob,Grep") << QStringLiteral("--allowedTools") << QStringLiteral("Bash")
                      << QStringLiteral("Read") << QStringLiteral("Edit") << QStringLiteral("Write") << QStringLiteral("Glob")
                      << QStringLiteral("Grep");
        } else {
            arguments << QStringLiteral("Bash,Read") << QStringLiteral("--allowedTools") << QStringLiteral("Bash(%1 *)").arg(cli)
                      << QStringLiteral("Read");
        }
        return Command{agent, arguments, {}};
    }
    if (name == QLatin1String("codex")) {
        return Command{agent,
                       {QStringLiteral("exec"), QStringLiteral("--skip-git-repo-check"), QStringLiteral("--ephemeral"), QStringLiteral("--color"),
                        QStringLiteral("never"), QStringLiteral("-c"), QStringLiteral("approval_policy=\"never\""), QStringLiteral("--sandbox"),
                        project ? QStringLiteral("workspace-write") : QStringLiteral("read-only"), QStringLiteral("--"), prompt},
                       {}};
    }
    if (name == QLatin1String("opencode")) {
        // opencode reads permissions from OPENCODE_CONFIG_CONTENT; each is decided here so nothing waits on a question.
        const QJsonObject permission =
            project ? QJsonObject{{"edit", "allow"}, {"bash", "allow"}, {"webfetch", "deny"}, {"external_directory", "allow"}, {"doom_loop", "deny"}}
                    : QJsonObject{{"edit", "deny"},
                                  {"bash", QJsonObject{{"*", "deny"}, {cli + QStringLiteral(" *"), "allow"}}},
                                  {"webfetch", "deny"},
                                  {"external_directory", "allow"},
                                  {"doom_loop", "deny"}};
        return Command{agent, {QStringLiteral("run"), prompt},
                       {QStringLiteral("OPENCODE_CONFIG_CONTENT=")
                        + QString::fromUtf8(QJsonDocument(QJsonObject{{"permission", permission}}).toJson(QJsonDocument::Compact))}};
    }
    if (name == QLatin1String("gemini")) {
        QStringList arguments{QStringLiteral("-p"), prompt, QStringLiteral("--output-format"), QStringLiteral("text"),
                              QStringLiteral("--skip-trust"), QStringLiteral("--approval-mode")};
        if (project)
            arguments << QStringLiteral("auto_edit") << QStringLiteral("--allowed-tools") << QStringLiteral("run_shell_command");
        else
            arguments << QStringLiteral("default") << QStringLiteral("--allowed-tools") << QStringLiteral("read_file")
                      << QStringLiteral("run_shell_command(%1)").arg(cli);
        return Command{agent, arguments, {}};
    }
    return std::nullopt;
}
}

AgentRun *AgentRunStarter::start(const QString &agent, const QString &task, const AgentLauncher::Command &command, const QString &prompt,
                                 const QProcessEnvironment &base, const QString &directory, int timeoutMs, QString *error)
{
    const QString logs = AgentLauncher::logFolder();
    if (!QDir().mkpath(logs)) {
        *error = QStringLiteral("Could not create %1.").arg(logs);
        return nullptr;
    }
    // The newest 30 stay, this one included; names sort by time.
    QStringList old = QDir(logs).entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Name);
    while (old.size() >= keptLogs)
        QFile::remove(QDir(logs).filePath(old.takeFirst()));
    QString slug = task;
    slug.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
    const QString logPath =
        QDir(logs).filePath(QStringLiteral("%1-%2.log").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")), slug));

    auto *run = new AgentRun;
    // Owned by the application, so quitting stops what is still running.
    run->setParent(QCoreApplication::instance());
    run->m_agent = agent;
    run->m_task = task;
    run->m_logPath = logPath;
    run->m_timeoutMs = timeoutMs;
    run->m_log = new QFile(logPath, run);
    if (!run->m_log->open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("Could not write %1: %2").arg(logPath, run->m_log->errorString());
        delete run;
        return nullptr;
    }
    // The environment is never logged, and neither is the task.
    run->m_log->write(QStringLiteral("Omastrator agent run\nagent: %1\ntask: %2\nfolder: %3\ncommand: %4\nstarted: %5\n\n")
                          .arg(agent, task, directory, described(command, prompt),
                               QDateTime::currentDateTime().toString(Qt::ISODate))
                          .toUtf8());
    run->m_log->flush();

    QProcessEnvironment environment = base;
    for (const QString &variable : command.environment)
        environment.insert(variable.section(QLatin1Char('='), 0, 0), variable.section(QLatin1Char('='), 1));
    run->m_process = new QProcess(run);
    run->m_process->setProcessEnvironment(environment);
    run->m_process->setWorkingDirectory(directory);
    run->m_process->setStandardInputFile(QProcess::nullDevice());
    // Its own process group, so Cancel and the timeout stop the tools it started too.
    run->m_process->setChildProcessModifier([] { ::setsid(); });
    QObject::connect(run->m_process, &QProcess::readyReadStandardOutput, run,
                     [run] { run->append(run->m_process->readAllStandardOutput(), false); });
    QObject::connect(run->m_process, &QProcess::readyReadStandardError, run,
                     [run] { run->append(run->m_process->readAllStandardError(), true); });
    QObject::connect(run->m_process, &QProcess::finished, run, [run](int code, QProcess::ExitStatus status) {
        run->m_exitCode = code;
        run->close(status == QProcess::NormalExit ? AgentRun::End::exited : AgentRun::End::crashed);
    });
    run->m_clock.start();
    run->m_process->start(command.program, command.arguments);
    if (!run->m_process->waitForStarted(5000)) {
        *error = QStringLiteral("Could not launch %1: %2").arg(agent, run->m_process->errorString());
        run->m_log->write(error->toUtf8() + '\n');
        run->m_process->disconnect(run);
        delete run;
        return nullptr;
    }
    run->m_pid = run->m_process->processId();
    run->m_timeout = new QTimer(run);
    run->m_timeout->setSingleShot(true);
    QObject::connect(run->m_timeout, &QTimer::timeout, run, [run] { run->stop(AgentRun::End::timedOut); });
    run->m_timeout->start(timeoutMs);
    qCInfo(lcApp).noquote() << "running" << agent << "headless for" << task << "in" << directory << "logging to" << logPath;
    return run;
}

AgentRun::~AgentRun()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        signalGroup(m_pid, SIGKILL);
        m_process->waitForFinished(1000);
    }
}

void AgentRun::append(const QByteArray &bytes, bool error)
{
    m_log->write(bytes);
    m_log->flush();
    const QString text = QString::fromUtf8(bytes);
    if (error)
        m_stderrTail = keepTail(m_stderrTail, text);
    else
        m_stdoutTail = keepTail(m_stdoutTail, text);
}

QString AgentRun::lastLine() const
{
    const QString fromError = lastMeaningful(m_stderrTail);
    if (!fromError.isEmpty())
        return fromError;
    // A quiet failure may say why on stdout; a clean exit's stdout is just the agent signing off.
    return m_end == End::exited && m_exitCode == 0 ? QString() : lastMeaningful(m_stdoutTail);
}

void AgentRun::cancel()
{
    stop(End::cancelled);
}

void AgentRun::stop(End end)
{
    if (m_end != End::running || m_requested != End::running || !m_process)
        return;
    m_requested = end;
    signalGroup(m_pid, SIGTERM);
    // Whatever ignores SIGTERM gets SIGKILL.
    QTimer::singleShot(2000, this, [this] {
        if (m_end == End::running)
            signalGroup(m_pid, SIGKILL);
    });
}

void AgentRun::close(End end)
{
    if (m_end != End::running)
        return;
    m_end = m_requested != End::running ? m_requested : end;
    if (m_timeout)
        m_timeout->stop();
    // What the pipes still hold.
    append(m_process->readAllStandardOutput(), false);
    append(m_process->readAllStandardError(), true);
    QString how;
    switch (m_end) {
    case End::exited:
        how = QStringLiteral("exited with code %1").arg(m_exitCode);
        break;
    case End::crashed:
        how = QStringLiteral("crashed");
        break;
    case End::timedOut:
        how = QStringLiteral("timed out after %1 s and was stopped").arg(timeoutSeconds());
        break;
    case End::cancelled:
        how = QStringLiteral("cancelled");
        break;
    case End::running:
        break;
    }
    m_log->write(QStringLiteral("\n\nended: %1, after %2 s\n").arg(how).arg(elapsedSeconds()).toUtf8());
    m_log->close();
    // Anything it left behind in its group goes too.
    signalGroup(m_pid, SIGKILL);
    emit finished();
    deleteLater();
}
