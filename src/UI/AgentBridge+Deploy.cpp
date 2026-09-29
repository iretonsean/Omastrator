#include "Agent/Setup.h"
#include "UI/AgentBridge.h"
#include "UI/LiveFrames.h"
#include "UI/AgentSheets.h"
#include "UI/LiveHistoryPanel.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

// Deploy-first Live (docs/OS-SUITE.md): one press writes the live edits back,
// commits them, pushes, and deploys with the project's own setup. The diff is
// a record for later, never a step in the way.
namespace {
QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}
}

void AgentBridge::wireDeploy()
{
    m_reviewPanel.onClose = [this] { m_reviewPanel.close(); };
    m_historyPanel.onClose = [this] { m_historyPanel.close(); };
    connect(&m_live, &LiveSession::changed, this, [this] {
        if (!m_live.project().isEmpty())
            m_lastProject = canonical(m_live.project());
    });
    connect(&m_job, &DeployJob::stageChanged, this, [this] {
        switch (m_job.stage()) {
        case DeployJob::Stage::github:
            setStage(QStringLiteral("github"), QStringLiteral("Creating the GitHub repository…"));
            break;
        case DeployJob::Stage::pushing:
            setStage(QStringLiteral("pushing"), QStringLiteral("Pushing…"));
            break;
        case DeployJob::Stage::deploying:
            setStage(QStringLiteral("deploying"), QStringLiteral("Deploying…"));
            break;
        default:
            break;
        }
    });
    connect(&m_job, &DeployJob::agentNeeded, this, &AgentBridge::launchDeployAgent);
    connect(&m_job, &DeployJob::finished, this, [this](bool ok) {
        const bool deploy = m_pipeline.deploy;
        const bool failedDeploying = !ok && m_deployState.stage == QLatin1String("deploying") && m_job.failure() != QLatin1String("Cancelled.");
        m_pipeline.active = false;
        m_deployState.running = false;
        m_deployState.log = m_job.log();
        if (m_waiting && m_waiting->task == Task::deploy) {
            m_waiting.reset();
            emit waitingChanged();
        }
        if (!m_pipeline.github.isEmpty())
            m_github.reset();
        if (!ok) {
            QString message = failedDeploying ? QStringLiteral("Deploy failed: %1").arg(m_job.failure()) : m_job.failure();
            // One dry line may follow the plain one, never in its place.
            if (failedDeploying)
                if (const QString dry = Deploy::dryLine(); !dry.isEmpty())
                    message += QLatin1Char(' ') + dry;
            m_deployState.stage = QStringLiteral("failed");
            m_deployState.message = message;
        } else if (deploy) {
            m_deployState.url = m_job.url();
            m_deployState.deployed = true;
            m_deployState.stage = QStringLiteral("done");
            m_deployState.message = m_job.url().isEmpty() ? QStringLiteral("Deployed with %1").arg(m_pipeline.command.viaAgent() ? QStringLiteral("your agent") : m_pipeline.command.command)
                                                          : QStringLiteral("Live at %1").arg(m_job.url());
        } else {
            m_deployState.stage = QStringLiteral("done");
            QString message = m_pipeline.doneMessage;
            if (message.isEmpty())
                message = !WriteBack::isGitRepository(m_pipeline.folder) ? QStringLiteral("Saved")
                          : m_job.pushNote().isEmpty()                    ? QStringLiteral("Saved and pushed")
                                                                          : QStringLiteral("Saved. Not pushed: %1").arg(m_job.pushNote());
            m_deployState.message = message;
        }
        m_deployState.finishedAt = QDateTime::currentMSecsSinceEpoch();
        emit liveReviewChanged();
    });
}

QString AgentBridge::deployProject() const
{
    return m_live.project().isEmpty() ? m_lastProject : canonical(m_live.project());
}

void AgentBridge::rememberProject(const QString &folder)
{
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        m_lastProject = canonical(folder);
}

QString AgentBridge::panelProject()
{
    if (!m_panelProject.isEmpty())
        return m_panelProject;
    const QString framed = LiveFrames::selectedProject(session());
    return framed.isEmpty() ? deployProject() : framed;
}

void AgentBridge::followFrame(const QString &folder)
{
    if (!folder.isEmpty())
        m_panelProject = canonical(folder);
    else if (!m_reviewPanel.isVisible() && !m_historyPanel.isVisible())
        m_panelProject.clear();
}

std::vector<LiveEdit> AgentBridge::pendingEdits(const QString &folder) const
{
    std::vector<LiveEdit> edits = LiveFrames::pendingEdits(folder);
    if (!m_live.project().isEmpty() && canonical(m_live.project()) == canonical(folder))
        edits.insert(edits.begin(), m_live.edits().begin(), m_live.edits().end());
    return edits;
}

void AgentBridge::setStage(const QString &stage, const QString &message)
{
    m_deployState.stage = stage;
    m_deployState.message = message;
    emit liveReviewChanged();
}

GitHub::Auth AgentBridge::githubAuth(bool refresh)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (refresh || !m_github || now - m_github->first > 60'000)
        m_github = std::pair{now, GitHub::status()};
    return m_github->second;
}

QString AgentBridge::connectGitHub()
{
    m_github.reset();
    return GitHub::connect();
}

AgentBridge::DeployQuestion AgentBridge::deployQuestion(const QString &given)
{
    DeployQuestion question;
    question.folder = given.isEmpty() ? deployProject() : canonical(given);
    if (question.folder.isEmpty())
        return question;
    question.command = Deploy::resolve(question.folder);
    const Deploy::Settings settings = Deploy::settings(question.folder);
    question.confirm = !settings.confirmed;
    if (!settings.githubDeclined && WriteBack::isGitRepository(question.folder) && History::remotes(question.folder).isEmpty() && githubAuth().loggedIn)
        question.github = GitHub::suggestedName(question.folder);
    if (question.command.viaAgent()) {
        QString error;
        const QString agent = AgentLauncher::defaultAgent(&error);
        question.agent = agent.isEmpty() ? QString() : displayName(agent);
    }
    return question;
}

QString AgentBridge::liveDeploy(const DeployRequest &request, bool *needsAnswer)
{
    if (needsAnswer)
        *needsAnswer = false;
    if (m_pipeline.active)
        return QStringLiteral("A deploy is already running. Wait for it, or cancel it.");
    const QString folder = request.folder.isEmpty() ? deployProject() : canonical(request.folder);
    if (folder.isEmpty())
        return m_live.state() == LiveSession::State::running
                   ? QStringLiteral("This page is a mock-up, so there's no code to deploy. Start Live with the page's project folder.")
                   : QStringLiteral("Open a project in Live first: Deploy works on a page whose code is on this machine.");
    if (!QFileInfo(folder).isDir())
        return QStringLiteral("%1 doesn't exist.").arg(folder);
    const DeployQuestion question = deployQuestion(folder);
    const bool unanswered = (request.deploy && question.confirm && !request.confirm) || (!question.github.isEmpty() && !request.github);
    if (unanswered) {
        if (needsAnswer) {
            *needsAnswer = true;
            return {};
        }
        return QStringLiteral("Confirm deploying %1 to production first.").arg(QDir(folder).dirName());
    }
    Deploy::Settings settings = Deploy::settings(folder);
    const Deploy::Settings before = settings;
    if (request.deploy && request.confirm && request.remember)
        settings.confirmed = true;
    if (!question.github.isEmpty() && request.github && request.github->trimmed().isEmpty())
        settings.githubDeclined = true;
    if (settings.confirmed != before.confirmed || settings.githubDeclined != before.githubDeclined)
        Deploy::saveSettings(folder, settings);

    // Only the island's and the window's deploys move it; a Browser View's leaves it alone.
    const bool fromIsland = request.folder.isEmpty();
    if (!request.fromFrame)
        rememberProject(folder);
    m_pipeline = Pipeline{true, folder, request.deploy, question.command,
                          question.github.isEmpty() ? QString() : request.github.value_or(QString()).trimmed(), {}, {}, {}};
    m_deployState = DeployState{};
    m_deployState.running = true;
    m_deployState.folder = folder;
    m_deployState.fromFrame = request.fromFrame;
    setStage(QStringLiteral("writing"), QStringLiteral("Writing…"));
    // The agent's write-backs already running for this project; one the write-back starts joins by itself.
    for (const auto &[id, work] : m_liveJobs)
        if (canonical(work.project) == folder)
            m_pipeline.waitingFor << id;
    // The island (no folder) writes back only what the window edited, as before; a frame's action names its folder
    // and writes everything pending for it, the held edits included.
    const bool writeBack = fromIsland ? m_live.state() == LiveSession::State::running && canonical(m_live.project()) == folder && !m_live.edits().empty()
                                      : !pendingEdits(folder).empty();
    if (writeBack) {
        QString agentRequest;
        if (const QString failure = liveWriteBack(&agentRequest, fromIsland ? QString() : folder); !failure.isEmpty()) {
            if (m_pipeline.active)
                pipelineFailed(failure);
            return failure;
        }
    }
    // The write-back waits on the page, so a quick agent may already have answered or stopped.
    if (!m_pipeline.active || m_deployState.stage != QLatin1String("writing"))
        return {};
    if (m_pipeline.waitingFor.isEmpty())
        finishWriting();
    else
        setStage(QStringLiteral("writing"), QStringLiteral("Writing… %1 is changing the code").arg(m_waiting ? displayName(m_waiting->agent) : QStringLiteral("the agent")));
    return {};
}

QString AgentBridge::liveSave(const QString &folder, bool fromFrame)
{
    DeployRequest request;
    request.deploy = false;
    request.folder = folder;
    request.fromFrame = fromFrame;
    bool needsAnswer = false;
    const QString failure = liveDeploy(request, &needsAnswer);
    if (needsAnswer)
        QMetaObject::invokeMethod(this, [this, folder, fromFrame] { AgentSheets::deploy(*this, &m_window, folder, false, fromFrame); }, Qt::QueuedConnection);
    return failure;
}

QString AgentBridge::startSave(const QString &folder, const QString &doneMessage)
{
    if (m_pipeline.active)
        return QStringLiteral("Wait for the deploy to finish.");
    m_pipeline = Pipeline{true, canonical(folder), false, {}, {}, {}, {}, doneMessage};
    m_deployState = DeployState{};
    m_deployState.running = true;
    m_deployState.folder = m_pipeline.folder;
    commitAndShip();
    return {};
}

void AgentBridge::finishWriting()
{
    commitAndShip();
}

void AgentBridge::commitAndShip()
{
    setStage(QStringLiteral("committing"), QStringLiteral("Committing…"));
    const QString folder = m_pipeline.folder;
    const bool git = WriteBack::isGitRepository(folder);
    QString head;
    if (git) {
        std::vector<WriteBack::Own> own;
        QStringList lines;
        std::vector<WriteBack::Review *> pending;
        for (WriteBack::Review &review : m_reviews) {
            if (!review.commit.isEmpty() || canonical(review.folder) != folder)
                continue;
            pending.push_back(&review);
            lines << review.summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            // The file as it was before Omastrator's first write since the last commit.
            for (const auto &change : review.changes)
                if (std::none_of(own.begin(), own.end(), [&](const auto &each) { return each.path == change.path; }))
                    own.push_back({change.path, change.before});
        }
        QString sha;
        if (!own.empty()) {
            if (const QString failure = WriteBack::commit(folder, own, WriteBack::commitMessage(lines), &sha); !failure.isEmpty())
                return pipelineFailed(failure);
        }
        QString error;
        head = WriteBack::git(folder, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("HEAD")}, &error).trimmed();
        for (WriteBack::Review *review : pending)
            review->commit = sha.isEmpty() ? head : sha;
        if (head.isEmpty() && m_pipeline.deploy)
            return pipelineFailed(QStringLiteral("Nothing is committed in %1 yet, so there's no version to deploy.").arg(QDir(folder).dirName()));
    }
    emit liveReviewChanged();
    m_job.start({folder, head, git, m_pipeline.github, m_pipeline.deploy, m_pipeline.command});
}

void AgentBridge::pipelineFailed(const QString &line, const QString &log)
{
    m_pipeline.active = false;
    m_deployState.running = false;
    m_deployState.stage = QStringLiteral("failed");
    m_deployState.message = line;
    m_deployState.finishedAt = QDateTime::currentMSecsSinceEpoch();
    if (!log.isEmpty())
        m_deployState.log = log;
    emit liveReviewChanged();
}

void AgentBridge::launchDeployAgent()
{
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty()) {
        m_job.agentFinished(QString(), error);
        return;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pipeline.agentRequest = id;
    QString pushedTo, why;
    if (const auto target = History::pushTarget(m_pipeline.folder, &why); target && m_job.pushNote().isEmpty())
        pushedTo = target->remote + QLatin1Char('/') + target->branch;
    const QString subject = m_job.plan().commit.isEmpty()
                                ? QString()
                                : WriteBack::git(m_pipeline.folder, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s"), m_job.plan().commit}, &why).trimmed();
    const QString prompt = Deploy::agentPrompt({id, m_pipeline.folder, m_job.plan().commit, subject, pushedTo,
                                                Setup::shellQuote(QCoreApplication::applicationFilePath())});
    error = launchProject(id, m_pipeline.folder, QStringLiteral("deploy"), prompt);
    if (!error.isEmpty()) {
        m_job.agentFinished(QString(), error);
        return;
    }
    m_waiting = Waiting{id, Task::deploy, agent};
    emit waitingChanged();
    setStage(QStringLiteral("deploying"), QStringLiteral("Deploying with %1…").arg(displayName(agent)));
}

QString AgentBridge::liveDeployed(const QString &requestId, const QString &url, const QString &command, const QString &error)
{
    if (!m_pipeline.active || m_pipeline.agentRequest.isEmpty())
        return QStringLiteral("No deploy is waiting for an agent.");
    if (!requestId.isEmpty() && requestId != m_pipeline.agentRequest)
        return QStringLiteral("No deploy is waiting with the id “%1”.").arg(requestId);
    if (url.trimmed().isEmpty() && error.trimmed().isEmpty())
        return QStringLiteral("Say where it's live with “url”, or why it failed with “error”.");
    // A command is only offered to remember when no .env value is in it.
    const QString suggested = command.trimmed();
    if (error.trimmed().isEmpty() && !suggested.isEmpty() && Deploy::redact(suggested, m_job.variables()) == suggested
        && suggested != Deploy::resolve(m_pipeline.folder).command)
        m_deployState.suggested = suggested;
    m_job.agentFinished(url, error.trimmed());
    return {};
}

QString AgentBridge::rememberSuggested()
{
    const QString folder = m_deployState.folder.isEmpty() ? deployProject() : m_deployState.folder;
    if (m_deployState.suggested.isEmpty() || folder.isEmpty())
        return QStringLiteral("There's no deploy command to remember.");
    if (const QString failure = Deploy::remember(folder, m_deployState.suggested); !failure.isEmpty())
        return failure;
    m_deployState.message = QStringLiteral("Remembered in omastrator.json: deploys run %1 from now on.").arg(m_deployState.suggested);
    m_deployState.suggested.clear();
    emit liveReviewChanged();
    return {};
}

void AgentBridge::cancelDeploy()
{
    if (!m_pipeline.active)
        return;
    // The agents it waits for stop with it.
    for (const QString &id : m_pipeline.waitingFor)
        stopLiveJob(id);
    if (auto run = m_runs.find(m_pipeline.agentRequest); run != m_runs.end() && run->second)
        run->second->cancel();
    if (m_job.running()) {
        // The job's finished handler says it.
        m_job.cancel();
        return;
    }
    pipelineFailed(QStringLiteral("Cancelled."));
}

std::vector<History::Entry> AgentBridge::history(const QString &given)
{
    const QString folder = given.isEmpty() ? deployProject() : canonical(given);
    return folder.isEmpty() ? std::vector<History::Entry>{} : History::list(folder);
}

QString AgentBridge::restoreVersion(const QString &sha, const QString &given)
{
    const QString folder = given.isEmpty() ? deployProject() : canonical(given);
    if (folder.isEmpty())
        return QStringLiteral("Open a project in Live first.");
    if (sha.trimmed().isEmpty())
        return QStringLiteral("Say which version to restore.");
    if (m_pipeline.active)
        return QStringLiteral("Wait for the deploy to finish, then restore.");
    QString error;
    const std::vector<WriteBack::FileChange> changes = History::restore(folder, sha.trimmed(), &error);
    if (!error.isEmpty())
        return error;
    if (changes.empty())
        return QStringLiteral("The project already matches that version.");
    if (const QString failure = WriteBack::apply(changes); !failure.isEmpty()) {
        WriteBack::restore(changes);
        return failure;
    }
    const QString subject = WriteBack::git(folder, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s"), sha.trimmed()}, &error).trimmed();
    const QString shortSha = sha.trimmed().left(7);
    record(QStringLiteral("Restore"), QStringLiteral("Restore %1: %2").arg(shortSha, subject), changes, folder);
    return startSave(folder, QStringLiteral("Restored %1. Deploy to put it live.").arg(shortSha));
}

void AgentBridge::showHistoryPanel(const QString &folder)
{
    followFrame(folder);
    if (!m_historyContent || !m_historyPanel.isVisible()) {
        m_historyContent = new LiveHistoryPanel(*this);
        m_historyPanel.show(QStringLiteral("History"), m_historyContent);
    }
}

QString AgentBridge::showDeployLog()
{
    QString log = m_deployState.log;
    if (log.isEmpty()) {
        const std::vector<Deploy::Record> records = Deploy::records(m_deployState.folder.isEmpty() ? deployProject() : m_deployState.folder);
        if (!records.empty())
            log = records.back().log;
    }
    if (log.isEmpty() || !QFileInfo::exists(log))
        return QStringLiteral("No deploy has run yet, so there's no log.");
    QMetaObject::invokeMethod(this, [this, log] { AgentSheets::deployLog(&m_window, log); }, Qt::QueuedConnection);
    return {};
}
