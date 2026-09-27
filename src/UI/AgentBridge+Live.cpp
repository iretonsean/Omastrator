#include "Agent/AgentProtocol.h"
#include "Agent/Setup.h"
#include "Document/EditorSession.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/LivePanel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

// Live mode's side of the bridge (docs/OS-SUITE.md): starting it, write-back,
// the agent path, the record of write-backs, and the `live` method.
namespace {
QString requestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}

QString jsonArgument(const QJsonValue &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
}

QString AgentBridge::startLive(const QUrl &url, const QString &folder, const QString &command, bool app)
{
    LiveSession::Target target;
    target.url = url;
    target.folder = folder;
    target.command = command;
    target.app = app;
    target.headless = qEnvironmentVariableIsSet("OMASTRATOR_LIVE_HEADLESS");
    return m_live.start(target);
}

void AgentBridge::record(const QString &title, const QString &summary, const std::vector<WriteBack::FileChange> &changes, const QString &folder,
                         const QString &id)
{
    m_reviews.push_back({id.isEmpty() ? requestId() : id, title, summary, changes, folder, QString(), QDateTime::currentDateTime()});
    emit liveReviewChanged();
}

QString AgentBridge::liveWriteBack(QString *agentRequest)
{
    if (m_live.project().isEmpty())
        return QStringLiteral("This page is a mock-up, so its changes stay in the browser. Start Live with the page's project folder to write them back.");
    if (m_live.edits().empty())
        return QStringLiteral("There are no live edits to write back.");
    const QString project = m_live.project();
    // The plan reads the files as they are, so the user's uncommitted edits stay in them.
    const WriteBack::Plan plan = WriteBack::plan(project, m_live.edits());
    if (const QString failure = WriteBack::apply(plan.changes); !failure.isEmpty()) {
        WriteBack::restore(plan.changes);
        return failure;
    }
    if (!plan.changes.empty())
        record(QStringLiteral("Live edits"), plan.done.join(QLatin1Char('\n')), plan.changes, project);
    m_live.setEdits(plan.unresolved);
    m_liveMessage.clear();
    m_liveLog.clear();
    if (plan.unresolved.empty()) {
        emit liveReviewChanged();
        return {};
    }
    QJsonArray elements;
    for (const LiveEdit &edit : plan.unresolved)
        if (!elements.contains(edit.element))
            elements.append(edit.element);
    const QString agentFailure = liveAsk(QString(), elements, agentRequest);
    if (!agentFailure.isEmpty())
        m_liveMessage = QStringLiteral("%1 edits weren't certain enough to write directly, and the agent couldn't take them: %2")
                            .arg(plan.unresolved.size())
                            .arg(agentFailure);
    emit liveReviewChanged();
    return agentFailure.isEmpty() ? QString() : m_liveMessage;
}

QString AgentBridge::liveAsk(const QString &instruction, const QJsonArray &elements, QString *agentRequest)
{
    if (m_live.project().isEmpty())
        return QStringLiteral("This page is a mock-up: there's no project folder for the agent to change.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    // The agent starts from the last commit; whatever is on disk by the time it's done (the user's edits,
    // Omastrator's own) is merged with its change, and only a real clash stops it.
    AgentWork work{m_live.project(), {}, {}, requestId(), true};
    if (const QString failure = work.prepare(); !failure.isEmpty())
        return failure;
    QString screenshot;
    if (!elements.isEmpty()) {
        screenshot = QDir::temp().filePath(QStringLiteral("omastrator-live-%1.png").arg(work.requestId));
        if (!m_live.screenshot(screenshot, elements.first().toObject()["selector"].toString()).isEmpty())
            screenshot.clear();
    }
    AgentWork::Brief brief{instruction, instruction.isEmpty() ? m_live.edits() : std::vector<LiveEdit>{}, elements, screenshot,
                           m_live.url().toString(), Setup::shellQuote(QCoreApplication::applicationFilePath())};
    error = launchProject(work.requestId, work.worktree, QStringLiteral("live"), work.prompt(brief));
    if (!error.isEmpty()) {
        work.cleanup();
        return error;
    }
    m_liveJobs[work.requestId] = work;
    // A deploy that is writing waits for it, even if it answers before the deploy looks.
    if (m_pipeline.active && m_deployState.stage == QLatin1String("writing") && canonical(work.project) == m_pipeline.folder
        && !m_pipeline.waitingFor.contains(work.requestId))
        m_pipeline.waitingFor << work.requestId;
    if (instruction.isEmpty())
        m_live.setEdits({});
    if (agentRequest)
        *agentRequest = work.requestId;
    m_waiting = Waiting{work.requestId, Task::live, agent};
    emit waitingChanged();
    m_live.notice(QStringLiteral("Asked %1. The change is written to the code when it's done.").arg(displayName(agent)));
    return {};
}

QString AgentBridge::handToAgent(const QString &folder, const QString &instruction)
{
    EditorSession *front = session();
    if (!front || !front->hasDocument())
        return QStringLiteral("Open the mockup in Omastrator first: the agent works from the document in front.");
    if (!QFileInfo(folder).isDir())
        return QStringLiteral("Choose the folder with the app's source.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString project = QFileInfo(folder).canonicalFilePath();
    AgentWork work{project, {}, {}, requestId(), true};
    if (const QString failure = work.prepare(); !failure.isEmpty())
        return failure;
    // The mockup as the agent sees it, and as vectors to measure from.
    const QString stem = QDir::temp().filePath(QStringLiteral("omastrator-handoff-%1").arg(work.requestId));
    try {
        m_tools.call(QStringLiteral("render"), {{"path", stem + QStringLiteral(".png")}, {"scale", 2}});
        m_tools.call(QStringLiteral("export"), {{"path", stem + QStringLiteral(".svg")}, {"format", "svg"}});
    } catch (const AgentProtocol::Error &failure) {
        work.cleanup();
        return failure.message();
    }
    error = launchProject(work.requestId, work.worktree, QStringLiteral("handoff"),
                          work.handoffPrompt(instruction, stem + QStringLiteral(".png"), stem + QStringLiteral(".svg"),
                                             Setup::shellQuote(QCoreApplication::applicationFilePath())));
    if (!error.isEmpty()) {
        work.cleanup();
        return error;
    }
    m_liveJobs[work.requestId] = work;
    m_lastProject = project;
    m_waiting = Waiting{work.requestId, Task::live, agent};
    m_liveMessage.clear();
    m_liveLog.clear();
    emit waitingChanged();
    emit liveReviewChanged();
    return {};
}

QString AgentBridge::liveAgentDone(const QString &id, const QString &summary)
{
    auto job = m_liveJobs.find(id);
    if (job == m_liveJobs.end())
        return QStringLiteral("No Live task is waiting with the id “%1”.").arg(id);
    AgentWork &work = job->second;
    QString error;
    const std::vector<WriteBack::FileChange> changes = work.collect(&error);
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    m_liveLog.clear();
    if (!error.isEmpty()) {
        m_liveMessage = error;
    } else if (changes.empty()) {
        m_liveMessage = summary.isEmpty() ? QStringLiteral("The agent changed nothing.") : QStringLiteral("The agent changed nothing: %1").arg(summary);
    } else if (const QString failure = WriteBack::apply(changes); !failure.isEmpty()) {
        WriteBack::restore(changes);
        error = failure;
        m_liveMessage = failure;
    } else {
        record(QStringLiteral("Agent"), summary.isEmpty() ? QStringLiteral("The agent's change") : summary, changes, work.project, id);
        m_liveMessage.clear();
    }
    work.cleanup();
    m_liveJobs.erase(job);
    emit liveReviewChanged();
    if (m_pipeline.active && m_pipeline.waitingFor.removeAll(id) > 0) {
        if (!error.isEmpty())
            pipelineFailed(QStringLiteral("The agent's change couldn't be written: %1").arg(error));
        else if (m_pipeline.waitingFor.isEmpty())
            finishWriting();
    }
    return {};
}

QString AgentBridge::discardReview(const QString &id)
{
    // Newest first, so each file goes back step by step.
    for (auto it = m_reviews.end(); it != m_reviews.begin();) {
        --it;
        if (id.isEmpty() ? !it->commit.isEmpty() : it->id != id)
            continue;
        QString error;
        const std::vector<WriteBack::FileChange> undo = WriteBack::reverse(it->changes, &error);
        if (!error.isEmpty())
            return error;
        if (!it->commit.isEmpty()) {
            // Already in history: the undo is a new commit of its own, and the site changes when it's deployed again.
            if (m_pipeline.active)
                return QStringLiteral("Wait for the deploy to finish, then discard.");
            if (const QString failure = WriteBack::apply(undo); !failure.isEmpty())
                return failure;
            const QString folder = it->folder;
            const QString title = it->summary.section(QLatin1Char('\n'), 0, 0);
            m_reviews.erase(it);
            record(QStringLiteral("Discarded"), QStringLiteral("Discard: %1").arg(title), undo, folder);
            return startSave(folder, QStringLiteral("Discarded and committed. Deploy to take it off the live site."));
        }
        if (const QString failure = WriteBack::apply(undo); !failure.isEmpty())
            return failure;
        it = m_reviews.erase(it);
        if (!id.isEmpty()) {
            emit liveReviewChanged();
            return {};
        }
    }
    if (!id.isEmpty())
        return QStringLiteral("That change is gone.");
    emit liveReviewChanged();
    return {};
}

int AgentBridge::unsavedFiles() const
{
    QStringList files;
    for (const auto &review : m_reviews)
        if (review.commit.isEmpty())
            for (const auto &change : review.changes)
                if (!files.contains(change.path))
                    files << change.path;
    return int(files.size());
}

void AgentBridge::showLivePanel(bool changes)
{
    if (!m_reviewContent || !m_reviewPanel.isVisible()) {
        m_reviewContent = new LivePanel(*this);
        m_reviewPanel.show(QStringLiteral("Live"), m_reviewContent);
    }
    if (changes)
        static_cast<LivePanel *>(m_reviewContent.data())->showChanges(true);
}

QString AgentBridge::live(const QString &action, const QJsonObject &params, QJsonObject &result)
{
    auto forward = [this] {
        m_window.raise();
        m_window.activateWindow();
    };
    if (action == QLatin1String("start")) {
        const QUrl url = QUrl::fromUserInput(params["url"].toString());
        const QString folder = params["folder"].toString();
        const QString command = params["command"].toString();
        if (params["url"].toString().isEmpty() && folder.isEmpty() && command.isEmpty()) {
            forward();
            QMetaObject::invokeMethod(this, [this] { AgentSheets::live(*this, &m_window); }, Qt::QueuedConnection);
            result["sheet"] = true;
            return {};
        }
        return startLive(params["url"].toString().isEmpty() ? QUrl() : url, folder, command, params["app"].toBool());
    }
    if (action == QLatin1String("handoff")) {
        if (params["folder"].toString().isEmpty()) {
            forward();
            QMetaObject::invokeMethod(this, [this] { AgentSheets::handoff(*this, &m_window); }, Qt::QueuedConnection);
            result["sheet"] = true;
            return {};
        }
        return handToAgent(params["folder"].toString(), params["prompt"].toString());
    }
    if (action == QLatin1String("stop")) {
        m_live.stop();
        return {};
    }
    if (action == QLatin1String("status")) {
        result = m_live.status();
        QJsonArray edits;
        for (const LiveEdit &edit : m_live.edits())
            edits.append(edit.toJson());
        result["editList"] = edits;
        result["selectionList"] = m_live.selection();
        QJsonArray reviews;
        for (const auto &review : m_reviews)
            reviews.append(QJsonObject{{"id", review.id}, {"title", review.title}, {"summary", review.summary}, {"folder", review.folder},
                                       {"commit", review.commit}, {"diff", review.diff()}});
        result["reviews"] = reviews;
        result["unsaved"] = unsavedFiles();
        result["liveMessage"] = m_liveMessage;
        result["deploy"] = QJsonObject{{"stage", m_deployState.stage}, {"message", m_deployState.message}, {"url", m_deployState.url},
                                       {"log", m_deployState.log}, {"running", m_deployState.running}, {"suggested", m_deployState.suggested},
                                       {"project", deployProject()}};
        return {};
    }
    // Write-backs outlive the browser: they're files on disk.
    if (action == QLatin1String("review")) {
        forward();
        showLivePanel(true);
        return {};
    }
    if (action == QLatin1String("discard"))
        return discardReview(params["id"].toString());
    if (action == QLatin1String("save"))
        return liveSave();
    if (action == QLatin1String("agentDone"))
        return liveAgentDone(params["requestId"].toString(), params["summary"].toString());
    if (action == QLatin1String("deploy")) {
        DeployRequest request;
        request.confirm = params["confirm"].toBool();
        request.remember = params["remember"].toBool();
        request.folder = params["folder"].toString();
        if (params.contains("github"))
            request.github = params["github"].toString();
        bool needsAnswer = false;
        const QString failure = liveDeploy(request, &needsAnswer);
        if (needsAnswer) {
            forward();
            QMetaObject::invokeMethod(this, [this, folder = request.folder] { AgentSheets::deploy(*this, &m_window, folder); }, Qt::QueuedConnection);
            result["sheet"] = true;
        }
        return failure;
    }
    if (action == QLatin1String("deployed"))
        return liveDeployed(params["requestId"].toString(), params["url"].toString(), params["command"].toString(), params["error"].toString());
    if (action == QLatin1String("cancel")) {
        cancelDeploy();
        return {};
    }
    if (action == QLatin1String("history")) {
        QJsonArray commits;
        for (const History::Entry &entry : history())
            commits.append(QJsonObject{{"sha", entry.sha}, {"subject", entry.subject}, {"author", entry.author},
                                       {"time", entry.time.toString(Qt::ISODate)}, {"files", QJsonArray::fromStringList(entry.files)},
                                       {"link", entry.link}, {"deployed", entry.deploy.has_value()},
                                       {"url", entry.deploy ? entry.deploy->url : QString()}});
        result["commits"] = commits;
        result["project"] = deployProject();
        if (!params["list"].toBool()) {
            forward();
            showHistoryPanel();
        }
        return {};
    }
    if (action == QLatin1String("restore"))
        return restoreVersion(params["id"].toString());
    if (action == QLatin1String("details")) {
        forward();
        return showDeployLog();
    }
    if (action == QLatin1String("remember"))
        return rememberSuggested();
    if (action == QLatin1String("github")) {
        const GitHub::Auth auth = githubAuth(true);
        result["installed"] = auth.installed;
        result["loggedIn"] = auth.loggedIn;
        result["account"] = auth.account;
        result["page"] = History::githubPage(deployProject());
        if (params["connect"].toBool() && !auth.loggedIn)
            return connectGitHub();
        return {};
    }
    if (m_live.state() != LiveSession::State::running)
        return QStringLiteral("Live isn't running. Start it from the island's Live mode.");
    if (action == QLatin1String("writeBack"))
        return liveWriteBack();
    if (action == QLatin1String("ask"))
        return liveAsk(params["prompt"].toString(), m_live.selection());
    if (action == QLatin1String("select") && params.contains("selector")) {
        QString error;
        const QJsonValue found = m_live.evaluate(QStringLiteral("window.__oma.select(%1, %2)")
                                                     .arg(jsonArgument(params["selector"]), params["add"].toBool() ? QStringLiteral("true") : QStringLiteral("false")),
                                                 &error);
        if (error.isEmpty() && found.isNull())
            return QStringLiteral("Nothing on the page matches %1.").arg(params["selector"].toString());
        return error;
    }
    if (action == QLatin1String("select")) {
        QString error;
        m_live.evaluate(QStringLiteral("window.__oma.enable(%1)").arg(params["on"].toBool(true) ? QStringLiteral("true") : QStringLiteral("false")), &error);
        return error;
    }
    if (action == QLatin1String("edit"))
        return m_live.edit(params["selector"].toString(), params["property"].toString(), params["value"].toString());
    if (action == QLatin1String("screenshot")) {
        const QString path = params["path"].toString();
        if (path.isEmpty())
            return QStringLiteral("Say where to write the screenshot with “path”.");
        result["path"] = path;
        return m_live.screenshot(path, params["selector"].toString());
    }
    return QStringLiteral("There is no Live action “%1”.").arg(action);
}
