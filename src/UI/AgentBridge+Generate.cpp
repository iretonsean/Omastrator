#include "Live/AgentWork.h"
#include "UI/AgentBridge.h"
#include "Agent/Setup.h"
#include <QCoreApplication>
#include <QDir>

// Generate a page's side of the bridge (docs/MOTION.md, section 4): a headless agent writes a new project into a staging
// folder and says so with agentDone. Nothing here reads or writes a project; the caller shows the files and confirms.

namespace {
QString endReason(const AgentRun &run)
{
    const QString last = run.lastLine();
    return last.isEmpty() ? QStringLiteral("%1 stopped without writing the page.").arg(AgentBridge::displayName(run.agent()))
                          : QStringLiteral("%1 stopped without writing the page: %2").arg(AgentBridge::displayName(run.agent()), last);
}
}

QString AgentBridge::generatePage(const PageRequest &request, PageDone done)
{
    if (m_waiting)
        return QStringLiteral("The agent is still working on another change.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    AgentWork::PageBrief brief;
    brief.requestId = id;
    brief.staging = request.staging;
    brief.description = request.description;
    brief.stack = request.stack;
    brief.files = request.files;
    brief.tokenFile = request.tokenFile;
    brief.command = Setup::shellQuote(QCoreApplication::applicationFilePath());
    // The job is filed first: a fast agent can answer before launchProject returns.
    m_pages[id] = {request, std::move(done)};
    error = launchProject(id, request.staging, QStringLiteral("page"), AgentWork::pagePrompt(brief));
    if (!error.isEmpty()) {
        m_pages.erase(id);
        return error;
    }
    m_waiting = Waiting{id, Task::live, agent};
    m_liveMessage.clear();
    m_liveLog.clear();
    emit waitingChanged();
    return {};
}

QString AgentBridge::pageAgentDone(const QString &id, const QString &summary)
{
    auto found = m_pages.find(id);
    PageDone done = std::move(found->second.done);
    m_pages.erase(found);
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    PageResult result;
    result.summary = summary;
    done(result);
    return {};
}

void AgentBridge::pageRunFinished(const QString &id, AgentRun &run)
{
    auto found = m_pages.find(id);
    PageJob job = std::move(found->second);
    m_pages.erase(found);
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    // The run has ended, so nothing writes here any more.
    QDir(job.request.staging).removeRecursively();
    PageResult result;
    result.cancelled = run.end() == AgentRun::End::cancelled;
    if (!result.cancelled) {
        result.error = endReason(run);
        m_liveMessage = result.error;
        m_liveLog = run.logPath();
        emit liveReviewChanged();
    }
    job.done(result);
}

void AgentBridge::stopPageJob(const QString &id)
{
    auto found = m_pages.find(id);
    if (found == m_pages.end())
        return;
    PageJob job = std::move(found->second);
    m_pages.erase(found);
    const QString staging = job.request.staging;
    auto run = m_runs.find(id);
    if (run != m_runs.end() && run->second && run->second->isRunning()) {
        AgentRun *stopping = run->second;
        m_runs.erase(run);
        // The agent may still be writing until it has stopped.
        connect(stopping, &AgentRun::finished, stopping, [staging] { QDir(staging).removeRecursively(); });
        stopping->cancel();
        if (m_run == stopping)
            m_run = nullptr;
    } else {
        QDir(staging).removeRecursively();
    }
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    PageResult result;
    result.cancelled = true;
    job.done(result);
}

void AgentBridge::setActivity(std::vector<ActivityLine> lines)
{
    m_activity = std::move(lines);
    emit activityChanged();
}
