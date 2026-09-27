#include "Agent/Setup.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/LiveReviewPanel.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

// Live mode's side of the bridge (docs/OS-SUITE.md): starting it, write-back,
// the agent path, reviews, Save and Publish.
namespace {
QString requestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString jsonArgument(const QJsonValue &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
}

QString AgentBridge::startLive(const QUrl &url, const QString &folder)
{
    LiveSession::Target target;
    target.url = url;
    target.folder = folder;
    target.headless = qEnvironmentVariableIsSet("OMASTRATOR_LIVE_HEADLESS");
    return m_live.start(target);
}

QString AgentBridge::liveWriteBack(bool confirm)
{
    if (m_live.project().isEmpty())
        return QStringLiteral("This page is a mock-up, so its changes stay in the browser. Start Live with the page's project folder to write them back.");
    if (m_live.edits().empty())
        return QStringLiteral("There are no live edits to write back.");
    const QString project = m_live.project();
    const WriteBack::Plan plan = WriteBack::plan(project, m_live.edits());
    QStringList paths;
    for (const auto &change : plan.changes)
        paths << QDir(project).relativeFilePath(change.path);
    if (const QStringList dirty = WriteBack::dirtyFiles(project, paths); !dirty.isEmpty() && !confirm) {
        m_liveMessage = QStringLiteral("%1 has changes you haven't committed. Commit or stash them, or go ahead: Discard would still put "
                                       "your version back.")
                            .arg(dirty.join(QStringLiteral(", ")));
        m_confirm = [this] { return liveWriteBack(true); };
        emit liveReviewChanged();
        return m_liveMessage;
    }
    m_confirm = nullptr;
    if (const QString failure = WriteBack::apply(plan.changes); !failure.isEmpty()) {
        WriteBack::restore(plan.changes);
        return failure;
    }
    if (!plan.changes.empty())
        m_reviews.push_back({requestId(), QStringLiteral("Live edits"), plan.done.join(QLatin1Char('\n')), plan.changes});
    m_live.setEdits(plan.unresolved);
    m_liveMessage.clear();
    QString agentFailure;
    if (!plan.unresolved.empty()) {
        QJsonArray elements;
        for (const LiveEdit &edit : plan.unresolved)
            if (!elements.contains(edit.element))
                elements.append(edit.element);
        agentFailure = liveAsk(QString(), elements, confirm);
        if (!agentFailure.isEmpty())
            m_liveMessage = QStringLiteral("%1 edits weren't certain enough to write directly, and the agent couldn't take them: %2")
                                .arg(plan.unresolved.size())
                                .arg(agentFailure);
    }
    emit liveReviewChanged();
    showReviewPanel();
    return plan.changes.empty() && !agentFailure.isEmpty() ? agentFailure : QString();
}

QString AgentBridge::liveAsk(const QString &instruction, const QJsonArray &elements, bool confirm)
{
    if (m_live.project().isEmpty())
        return QStringLiteral("This page is a mock-up: there's no project folder for the agent to change.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString project = m_live.project();
    // The agent starts from the last commit; the user's uncommitted work is theirs to confirm first.
    // Files waiting in a review are Omastrator's own writes, not the user's work.
    QStringList ours;
    for (const auto &review : m_reviews)
        for (const auto &change : review.changes)
            ours << QDir(project).relativeFilePath(change.path);
    QStringList dirty = WriteBack::dirtyFiles(project);
    for (const QString &path : std::as_const(ours))
        dirty.removeAll(path);
    if (!dirty.isEmpty() && !confirm) {
        m_liveMessage = QStringLiteral("The project has changes you haven't committed (%1). Commit or stash them, or go ahead and the "
                                       "agent's changes merge into yours.")
                            .arg(dirty.mid(0, 4).join(QStringLiteral(", ")) + (dirty.size() > 4 ? QStringLiteral("…") : QString()));
        m_confirm = [this, instruction, elements] { return liveAsk(instruction, elements, true); };
        emit liveReviewChanged();
        showReviewPanel();
        return m_liveMessage;
    }
    m_confirm = nullptr;
    // The agent starts from the last commit, so its changes merge with Omastrator's own pending ones.
    AgentWork work{project, {}, {}, requestId(), confirm || !ours.isEmpty()};
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
    error = AgentLauncher::launchIn(work.worktree, work.prompt(brief), m_server.isListening() ? m_server.path() : QString());
    if (!error.isEmpty()) {
        work.cleanup();
        return error;
    }
    if (instruction.isEmpty())
        m_live.setEdits({});
    m_liveJobs[work.requestId] = work;
    m_waiting = Waiting{work.requestId, Task::live, agent};
    emit waitingChanged();
    m_live.notice(QStringLiteral("Asked %1. The change comes back for review when it's done.").arg(displayName(agent)));
    return {};
}

QString AgentBridge::liveAgentDone(const QString &id, const QString &summary, bool confirm)
{
    auto job = m_liveJobs.find(id);
    if (job == m_liveJobs.end())
        return QStringLiteral("No Live task is waiting with the id “%1”.").arg(id);
    AgentWork &work = job->second;
    work.confirmedDirty = work.confirmedDirty || confirm;
    QString error;
    const std::vector<WriteBack::FileChange> changes = work.collect(&error);
    if (!error.isEmpty()) {
        // Kept, so the user can confirm and merge after all.
        m_liveMessage = error;
        m_confirm = [this, id, summary] { return liveAgentDone(id, summary, true); };
        emit liveReviewChanged();
        showReviewPanel();
        return error;
    }
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    if (changes.empty()) {
        m_liveMessage = summary.isEmpty() ? QStringLiteral("The agent changed nothing.") : QStringLiteral("The agent changed nothing: %1").arg(summary);
    } else if (const QString failure = WriteBack::apply(changes); !failure.isEmpty()) {
        WriteBack::restore(changes);
        m_liveMessage = failure;
    } else {
        m_reviews.push_back({id, QStringLiteral("Agent"), summary.isEmpty() ? QStringLiteral("The agent's change") : summary, changes});
        m_liveMessage.clear();
        m_resultsUnseen = true;
    }
    work.cleanup();
    m_liveJobs.erase(job);
    emit liveReviewChanged();
    showReviewPanel();
    return {};
}

QString AgentBridge::keepReview(const QString &id)
{
    bool found = false;
    for (auto it = m_reviews.begin(); it != m_reviews.end();) {
        if (!id.isEmpty() && it->id != id) {
            ++it;
            continue;
        }
        for (const auto &change : it->changes)
            if (!m_keptFiles.contains(change.path))
                m_keptFiles << change.path;
        m_keptLines << it->summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        it = m_reviews.erase(it);
        found = true;
    }
    if (!found)
        return id.isEmpty() ? QStringLiteral("There's nothing to keep.") : QStringLiteral("That review is gone.");
    m_resultsUnseen = false;
    emit liveReviewChanged();
    return {};
}

QString AgentBridge::discardReview(const QString &id)
{
    bool found = false;
    // Newest first, so each file goes back to what it was before this session wrote it.
    for (auto it = m_reviews.end(); it != m_reviews.begin();) {
        --it;
        if (!id.isEmpty() && it->id != id)
            continue;
        if (const QString failure = WriteBack::restore(it->changes); !failure.isEmpty())
            return failure;
        it = m_reviews.erase(it);
        found = true;
    }
    if (!found)
        return id.isEmpty() ? QStringLiteral("There's nothing to discard.") : QStringLiteral("That review is gone.");
    m_resultsUnseen = false;
    emit liveReviewChanged();
    return {};
}

QString AgentBridge::liveSave()
{
    if (!m_reviews.empty())
        return QStringLiteral("Keep or discard the waiting changes first.");
    if (m_keptFiles.isEmpty())
        return QStringLiteral("Nothing kept to save.");
    if (const QString failure = WriteBack::commit(m_live.project(), m_keptFiles, WriteBack::commitMessage(m_keptLines)); !failure.isEmpty())
        return failure;
    m_keptFiles.clear();
    m_keptLines.clear();
    m_liveMessage = QStringLiteral("Saved: committed to %1. Publishing is separate.").arg(QDir(m_live.project()).dirName());
    emit liveReviewChanged();
    return {};
}

std::vector<WriteBack::PublishOption> AgentBridge::publishOptions() const
{
    return m_live.project().isEmpty() ? std::vector<WriteBack::PublishOption>{} : WriteBack::publishOptions(m_live.project());
}

QString AgentBridge::livePublish(const QString &option, bool confirm, QString *output)
{
    const auto options = publishOptions();
    const auto chosen = std::find_if(options.begin(), options.end(), [&](const auto &each) { return each.id == option; });
    if (chosen == options.end())
        return options.empty() ? QStringLiteral("This project has nothing set up to publish with: no git upstream, and no Vercel, Netlify or Cloudflare CLI.")
                               : QStringLiteral("There's no publish option “%1”.").arg(option);
    if (!confirm)
        return QStringLiteral("%1 Confirm to go ahead.").arg(chosen->description);
    if (!m_keptFiles.isEmpty() || !m_reviews.empty())
        return QStringLiteral("Save or discard the Live changes first: publishing sends only what is committed.");
    QString error;
    const QString printed = WriteBack::publish(m_live.project(), *chosen, &error);
    if (output)
        *output = printed;
    m_liveMessage = error.isEmpty() ? QStringLiteral("Published: %1").arg(chosen->label) : error;
    emit liveReviewChanged();
    return error;
}

QString AgentBridge::confirmPending()
{
    const auto confirm = std::exchange(m_confirm, nullptr);
    return confirm ? confirm() : QStringLiteral("Nothing is waiting to be confirmed.");
}

void AgentBridge::showReviewPanel()
{
    if (!m_reviewContent || !m_reviewPanel.isVisible()) {
        m_reviewContent = new LiveReviewPanel(*this);
        m_reviewPanel.show(QStringLiteral("Live Review"), m_reviewContent);
    }
}

QString AgentBridge::live(const QString &action, const QJsonObject &params, QJsonObject &result)
{
    const bool confirm = params["confirm"].toBool();
    if (action == QLatin1String("start")) {
        const QUrl url = QUrl::fromUserInput(params["url"].toString());
        const QString folder = params["folder"].toString();
        if (params["url"].toString().isEmpty() && folder.isEmpty()) {
            m_window.raise();
            m_window.activateWindow();
            QMetaObject::invokeMethod(this, [this] { AgentSheets::live(*this, &m_window); }, Qt::QueuedConnection);
            result["sheet"] = true;
            return {};
        }
        return startLive(params["url"].toString().isEmpty() ? QUrl() : url, folder);
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
            reviews.append(QJsonObject{{"id", review.id}, {"title", review.title}, {"summary", review.summary}, {"diff", review.diff(m_live.project())}});
        result["reviews"] = reviews;
        result["unsaved"] = unsavedFiles();
        result["liveMessage"] = m_liveMessage;
        QJsonArray publish;
        for (const auto &option : publishOptions())
            publish.append(QJsonObject{{"id", option.id}, {"label", option.label}, {"description", option.description}});
        result["publish"] = publish;
        return {};
    }
    // Reviews outlive the browser: they're files on disk.
    if (action == QLatin1String("review")) {
        m_window.raise();
        showReviewPanel();
        return {};
    }
    if (action == QLatin1String("keep"))
        return keepReview(params["id"].toString());
    if (action == QLatin1String("discard"))
        return discardReview(params["id"].toString());
    if (action == QLatin1String("save"))
        return liveSave();
    if (action == QLatin1String("agentDone"))
        return liveAgentDone(params["requestId"].toString(), params["summary"].toString(), confirm);
    if (action == QLatin1String("publish")) {
        if (params["option"].toString().isEmpty()) {
            m_window.raise();
            m_window.activateWindow();
            QMetaObject::invokeMethod(this, [this] { AgentSheets::publish(*this, &m_window); }, Qt::QueuedConnection);
            result["sheet"] = true;
            return {};
        }
        QString output;
        const QString failure = livePublish(params["option"].toString(), confirm, &output);
        result["output"] = output;
        return failure;
    }
    if (m_live.state() != LiveSession::State::running)
        return QStringLiteral("Live isn't running. Start it from the island's Live mode.");
    if (action == QLatin1String("writeBack"))
        return liveWriteBack(confirm);
    if (action == QLatin1String("ask"))
        return liveAsk(params["prompt"].toString(), m_live.selection(), confirm);
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
