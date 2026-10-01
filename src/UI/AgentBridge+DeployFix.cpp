#include "Agent/Setup.h"
#include "Live/DeployFix.h"
#include "UI/AgentBridge.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUuid>

// Fix with <agent> (docs/LIVE-IN-FRAME.md, Deploy): a failed deploy's log goes to the default agent, which repairs what it can
// in the project's own folder, where node_modules and the build are. It never deploys or commits. What it changed is read back
// from git as one write-back record, so Review Changes shows it, Discard undoes it, and the next Deploy commits it.
namespace {
// More changed files than this and the folder isn't a project in order (no .gitignore for node_modules): nothing is recorded.
constexpr int mostFiles = 400;

QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}

// Keys and tokens stay out of the record, and out of any commit.
bool isEnvFile(const QString &relative)
{
    return QFileInfo(relative).fileName().startsWith(QLatin1String(".env"));
}

// The files git reports as changed or untracked, relative; none outside git. `ok` is false when there are too many.
QStringList changedFiles(const QString &folder, bool *ok)
{
    *ok = true;
    QProcess git;
    git.setWorkingDirectory(folder);
    git.start(QStringLiteral("git"), {QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z"), QStringLiteral("--untracked-files=all")});
    if (!git.waitForStarted(5000) || !git.waitForFinished(60'000) || git.exitCode() != 0)
        return {};
    QStringList files;
    const QList<QByteArray> entries = git.readAllStandardOutput().split('\0');
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const QByteArray &entry = entries[i];
        if (entry.size() < 4)
            continue;
        files << QString::fromUtf8(entry.mid(3));
        // A rename lists the old path next.
        if (entry[0] == 'R' || entry[0] == 'C')
            ++i;
    }
    if (files.size() > mostFiles) {
        *ok = false;
        return {};
    }
    return files;
}

QString oneLine(const QString &text, int most)
{
    const QString line = text.simplified();
    return line.size() > most ? line.left(most - 1) + QStringLiteral("…") : line;
}
}

bool AgentBridge::deployFailed(const QString &folder, const QString &log) const
{
    if (log.isEmpty())
        return false;
    // An agent's deploy that failed keeps the agent's own log, which has no "Failed:" line.
    if (m_deployState.stage == QLatin1String("failed") && m_deployState.deploy && m_deployState.log == log && canonical(m_deployState.folder) == canonical(folder))
        return true;
    const DeployFix::Failure failure = DeployFix::readFile(log);
    return failure.failed && failure.deploy;
}

QString AgentBridge::fixDeploy(const QString &given)
{
    const QString folder = canonical(given.isEmpty() ? (m_deployState.folder.isEmpty() ? deployProject() : m_deployState.folder) : given);
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return QStringLiteral("Open the project first.");
    if (m_fixRun)
        return QStringLiteral("A fix is already running.");
    if (m_pipeline.active)
        return QStringLiteral("Wait for the deploy to finish.");
    if (m_waiting)
        return QStringLiteral("The agent is still working on another change.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;

    // The log of the project's last failed deploy: this session's, else the newest record.
    QString log = canonical(m_deployState.folder) == folder ? m_deployState.log : QString();
    QString commit;
    if (const std::vector<Deploy::Record> records = Deploy::records(folder); !records.empty()) {
        if (log.isEmpty() && !records.back().ok)
            log = records.back().log;
        commit = records.back().commit;
    }
    if (log.isEmpty() || !QFileInfo::exists(log) || !deployFailed(folder, log))
        return QStringLiteral("There's no failed deploy to fix.");

    DeployFix::Brief brief;
    brief.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    brief.folder = folder;
    brief.commit = commit;
    brief.deployCommand = Deploy::resolve(folder).command;
    brief.binary = Setup::shellQuote(QCoreApplication::applicationFilePath());
    brief.failure = DeployFix::readFile(log);
    if (brief.failure.line.isEmpty() && canonical(m_deployState.folder) == folder)
        brief.failure.line = m_deployState.message;

    FixRun run;
    run.requestId = brief.requestId;
    bool listed = true;
    for (const QString &relative : changedFiles(folder, &listed))
        if (!isEnvFile(relative))
            run.base[relative] = WriteBack::readFile(QDir(folder).filePath(relative));
    // The run is filed first: a fast agent can answer before launchProject returns.
    m_fixRun = std::move(run);
    m_fix = DeployFixState{};
    m_fix.folder = folder;
    m_fix.running = true;
    m_fix.agent = displayName(agent);
    error = launchProject(brief.requestId, folder, QStringLiteral("deployfix"), DeployFix::prompt(brief));
    if (!error.isEmpty()) {
        m_fixRun.reset();
        m_fix = DeployFixState{};
        return error;
    }
    m_waiting = Waiting{brief.requestId, Task::fix, agent};
    emit waitingChanged();
    emit deployFixChanged();
    return {};
}

QString AgentBridge::fixAgentDone(const QString &id, const QString &summary)
{
    const auto run = m_runs.find(id);
    if (run == m_runs.end() || !run->second || !run->second->isRunning()) {
        finishFix(summary, {}, false);
        return {};
    }
    // The agent says it is done, but it may exit after one more write: the folder is read when it has ended, or when it has had
    // this long, and then it is stopped.
    m_fixRun->answered = true;
    m_fixRun->summary = summary;
    const QPointer<AgentRun> watched = run->second;
    QTimer::singleShot(pageDrainMs(), this, [this, id, watched] {
        if (m_fixRun && m_fixRun->requestId == id && m_fixRun->answered && watched && watched->isRunning())
            watched->cancel();
    });
    return {};
}

void AgentBridge::fixRunFinished(AgentRun &run)
{
    // By value: finishing takes the run's own strings.
    if (m_fixRun->answered)
        return finishFix(QString(m_fixRun->summary), {}, false);
    if (run.end() == AgentRun::End::cancelled)
        return finishFix({}, QStringLiteral("Stopped."), true);
    const QString last = run.lastLine();
    finishFix({}, last.isEmpty() ? QStringLiteral("%1 stopped without an answer.").arg(displayName(run.agent()))
                                 : QStringLiteral("%1 stopped without an answer: %2").arg(displayName(run.agent()), last), false);
}

void AgentBridge::stopFix()
{
    if (!m_fixRun)
        return;
    const auto run = m_runs.find(m_fixRun->requestId);
    if (run != m_runs.end() && run->second && run->second->isRunning()) {
        // The run's end finishes it, after the agent has stopped writing.
        run->second->cancel();
        return;
    }
    // An agent in a terminal can't be stopped from here; what it wrote so far is kept.
    finishFix({}, QStringLiteral("Stopped."), true);
}

void AgentBridge::finishFix(const QString &summary, const QString &error, bool stopped)
{
    const FixRun run = std::move(*m_fixRun);
    m_fixRun.reset();
    if (m_waiting && m_waiting->requestId == run.requestId) {
        m_waiting.reset();
        emit waitingChanged();
    }
    // Whatever the agent changed is read from git, however it ended: a stopped run's edits are on disk too.
    std::vector<WriteBack::FileChange> changes;
    bool listed = true;
    QStringList now = changedFiles(m_fix.folder, &listed);
    if (listed) {
        for (const auto &[relative, bytes] : run.base)
            if (!now.contains(relative))
                now << relative;
        for (const QString &relative : std::as_const(now)) {
            if (isEnvFile(relative))
                continue;
            const QString path = QDir(m_fix.folder).filePath(relative);
            const auto was = run.base.find(relative);
            const std::optional<QByteArray> before = was != run.base.end() ? was->second : WriteBack::committed(m_fix.folder, relative);
            const std::optional<QByteArray> after = WriteBack::readFile(path);
            if (before != after)
                changes.push_back({path, before, after});
        }
    }
    m_fix.running = false;
    m_fix.finished = true;
    m_fix.stopped = stopped;
    m_fix.summary = summary.trimmed();
    m_fix.error = error;
    for (const WriteBack::FileChange &change : changes)
        m_fix.files << QDir(m_fix.folder).relativeFilePath(change.path);
    if (!changes.empty())
        record(QStringLiteral("Fix"), QStringLiteral("Fix the failed deploy: %1").arg(oneLine(m_fix.summary.isEmpty() ? QStringLiteral("the agent's change") : m_fix.summary, 50)),
               changes, m_fix.folder, run.requestId);
    emit deployFixChanged();
}
