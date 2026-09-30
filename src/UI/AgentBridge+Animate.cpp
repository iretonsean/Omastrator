#include "Agent/Setup.h"
#include "Live/DevServers.h"
#include "Live/MotionContract.h"
#include "Live/MotionPrompt.h"
#include "Live/MotionStack.h"
#include "UI/AgentBridge.h"
#include "UI/LiveFrames.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <memory>

// Animate (docs/MOTION.md, section 4): the agent writes motion into a worktree, and that worktree is served as a preview. The
// project changes only when the designer saves; a discarded preview leaves nothing on disk but Omastrator's own worktree folder,
// which goes with it.
namespace {
QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}
}

QString AgentBridge::liveAnimate(const AnimateRequest &request, QString *agentRequest)
{
    const QString project = canonical(request.folder);
    if (project.isEmpty() || !QFileInfo(project).isDir())
        return QStringLiteral("This page isn't one of your sites, so there's no code to write motion into.");
    if (m_previews.count(project))
        return QStringLiteral("Keep or discard the motion you're previewing first.");
    if (const QString busy = busyMessage(); !busy.isEmpty())
        return busy;
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    AgentWork work{project, {}, {}, QUuid::createUuid().toString(QUuid::WithoutBraces), true};
    if (const QString failure = work.prepare(); !failure.isEmpty())
        return failure;

    MotionPrompt::Brief brief;
    brief.instruction = request.instruction;
    brief.elements = request.elements;
    brief.tokens = request.tokens;
    brief.motion = request.motion;
    brief.keyframeNames = request.keyframeNames;
    brief.stack = MotionStack::detect(project);
    brief.width = request.width;
    brief.url = request.url;
    brief.command = Setup::shellQuote(QCoreApplication::applicationFilePath());
    brief.reducedMotion = request.reducedMotion;
    brief.together = request.elements.size() > 1;
    brief.prefix = MotionPrompt::keyframePrefix(request.siteName);
    if (!request.picture.isNull()) {
        const QString path = QDir::temp().filePath(QStringLiteral("omastrator-motion-%1.png").arg(work.requestId));
        if (request.picture.save(path))
            brief.screenshot = path;
    }
    error = launchProject(work.requestId, work.worktree, QStringLiteral("live"), MotionPrompt::animate(work, brief));
    if (!error.isEmpty()) {
        work.cleanup();
        return error;
    }
    m_liveJobs[work.requestId] = work;
    Animation animation;
    animation.frame = request.frame;
    animation.folder = project;
    animation.title = request.title;
    animation.reducedMotion = request.reducedMotion;
    m_animations[work.requestId] = animation;
    if (agentRequest)
        *agentRequest = work.requestId;
    m_waiting = Waiting{work.requestId, Task::live, agent};
    emit waitingChanged();
    return {};
}

QString AgentBridge::busyMessage() const
{
    if (!m_waiting)
        return {};
    const auto writing = m_animations.constFind(m_waiting->requestId);
    return QStringLiteral("The agent is still working on %1.")
        .arg(writing != m_animations.constEnd()                  ? QStringLiteral("“%1”").arg(writing->title)
                 : m_builds.contains(m_waiting->requestId) ? QStringLiteral("“%1”").arg(m_builds.value(m_waiting->requestId).title)
                                                           : QStringLiteral("another change"));
}

QUuid AgentBridge::animatingFrame() const
{
    if (!m_waiting)
        return {};
    const auto found = m_animations.constFind(m_waiting->requestId);
    return found == m_animations.constEnd() ? QUuid() : found->frame;
}

std::optional<AgentBridge::Preview> AgentBridge::previewOf(const QString &folder) const
{
    const auto found = m_previews.find(canonical(folder));
    if (found == m_previews.end())
        return std::nullopt;
    return found->second.info;
}

std::optional<AgentBridge::Preview> AgentBridge::previewOfFrame(const QUuid &frame) const
{
    for (const auto &[folder, state] : m_previews)
        if (state.info.frame == frame)
            return state.info;
    return std::nullopt;
}

// The agent said it is done. It may still write or format before it exits, so when its process still runs the preview waits for it
// to end (at most as long as a page run does, then it is stopped); an agent in a terminal has no process to wait for.
QString AgentBridge::animationAnswered(const QString &id, const QString &summary)
{
    const auto run = m_runs.find(id);
    if (run == m_runs.end() || !run->second || !run->second->isRunning())
        return finishAnimation(id, summary);
    Animation &animation = m_animations[id];
    animation.answered = true;
    animation.summary = summary;
    const QPointer<AgentRun> watched = run->second;
    QTimer::singleShot(pageDrainMs(), this, [this, id, watched] {
        const auto again = m_animations.constFind(id);
        if (again != m_animations.constEnd() && again->answered && watched && watched->isRunning())
            watched->cancel();
    });
    return {};
}

// What the agent wrote is looked at and served, and nothing is written to the project.
QString AgentBridge::finishAnimation(const QString &id, const QString &summary)
{
    const auto job = m_liveJobs.find(id);
    if (job == m_liveJobs.end())
        return QStringLiteral("No Live task is waiting with the id “%1”.").arg(id);
    AgentWork work = job->second;
    m_liveJobs.erase(job);
    const Animation animation = m_animations.take(id);
    const QString agent = m_waiting ? displayName(m_waiting->agent) : QString();
    if (m_waiting && m_waiting->requestId == id) {
        m_waiting.reset();
        emit waitingChanged();
    }
    m_liveLog.clear();
    QString error;
    const std::vector<WriteBack::FileChange> changes = work.collect(&error);
    if (!error.isEmpty() || changes.empty()) {
        m_liveMessage = !error.isEmpty() ? error
            : summary.isEmpty()          ? QStringLiteral("The agent wrote no motion.")
                                         : QStringLiteral("The agent wrote no motion: %1").arg(summary);
        work.cleanup();
        emit liveReviewChanged();
        emit previewChanged(animation.folder);
        return {};
    }
    PreviewState state;
    state.work = work;
    state.pendingBefore = pendingEdits(animation.folder);
    Preview &info = state.info;
    info.frame = animation.frame;
    info.folder = animation.folder;
    info.title = animation.title.isEmpty() ? QStringLiteral("Animate") : animation.title;
    info.summary = summary.isEmpty() ? QStringLiteral("The motion Claude wrote") : summary;
    info.branch = work.branch;
    info.agent = agent;
    QStringList changed;
    for (const WriteBack::FileChange &change : changes) {
        const QString relative = QDir(animation.folder).relativeFilePath(change.path);
        info.files << relative;
        changed << relative;
    }
    const MotionContract::Report report = MotionContract::check(animation.folder, work.worktree, changed, animation.reducedMotion);
    info.problems = report.problems;
    info.notice = report.notice();
    info.blocks = report.blocks;
    m_liveMessage.clear();
    m_previews[animation.folder] = std::move(state);
    startPreviewServer(animation.folder);
    emit liveReviewChanged();
    emit previewChanged(animation.folder);
    return {};
}

// The worktree, served: its own dev server (or Omastrator's static one), with the project's node_modules as hard links, so no
// install and no network are needed. The copy runs beside the window, not in it, and the server never installs packages: a
// project without them fails to preview and says so, instead of rewriting its lockfile in the copy.
void AgentBridge::startPreviewServer(const QString &folder)
{
    const auto found = m_previews.find(folder);
    if (found == m_previews.end())
        return;
    const QString worktree = found->second.work.worktree;
    const QString modules = QDir(folder).filePath(QStringLiteral("node_modules"));
    const QString linked = QDir(worktree).filePath(QStringLiteral("node_modules"));
    const auto serve = [this, folder, worktree] {
        const auto preview = m_previews.find(folder);
        // Discarded while the copy ran: nothing is left to serve.
        if (preview == m_previews.end())
            return;
        preview->second.lease = DevServers::shared().acquire(
            worktree, this,
            [this, folder](const DevServers::Result &result) {
                const auto again = m_previews.find(folder);
                if (again == m_previews.end())
                    return;
                if (!result.error.isEmpty()) {
                    again->second.info.failure = result.error.section(QLatin1Char('\n'), 0, 0);
                    again->second.lease = 0;
                } else {
                    again->second.info.url = result.url;
                    again->second.info.ready = true;
                }
                emit previewChanged(folder);
            },
            false);
    };
    if (!QFileInfo(modules).isDir() || QFileInfo::exists(linked)) {
        serve();
        return;
    }
    auto *copy = new QProcess(this);
    const auto finished = std::make_shared<bool>(false);
    const auto next = [copy, finished, linked, serve](bool ok) {
        if (*finished)
            return;
        *finished = true;
        copy->deleteLater();
        if (!ok)
            QDir(linked).removeRecursively();
        serve();
    };
    connect(copy, &QProcess::finished, this, [next](int code, QProcess::ExitStatus status) { next(status == QProcess::NormalExit && code == 0); });
    connect(copy, &QProcess::errorOccurred, this, [next](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            next(false);
    });
    copy->start(QStringLiteral("cp"), {QStringLiteral("-al"), modules, linked});
}

void AgentBridge::releasePreview(const QString &folder, bool waitForServer)
{
    const auto found = m_previews.find(folder);
    if (found == m_previews.end())
        return;
    PreviewState state = std::move(found->second);
    m_previews.erase(found);
    if (state.lease)
        DevServers::shared().release(state.lease, waitForServer);
    state.work.cleanup();
}

QString AgentBridge::discardPreview(const QString &folder)
{
    const QString project = canonical(folder);
    const auto found = m_previews.find(project);
    if (found == m_previews.end())
        return QStringLiteral("There's no motion being previewed.");
    // What was tuned while the preview showed is dropped with it: it was made against motion that is gone.
    const std::vector<LiveEdit> before = found->second.pendingBefore;
    std::vector<LiveEdit> made;
    for (const LiveEdit &edit : pendingEdits(project))
        if (std::find(before.begin(), before.end(), edit) == before.end())
            made.push_back(edit);
    releasePreview(project, false);
    if (!made.empty())
        LiveFrames::clearPending(project, made);
    emit previewChanged(project);
    return {};
}

QString AgentBridge::acceptPreview(const QString &folder)
{
    const QString project = canonical(folder);
    const auto found = m_previews.find(project);
    if (found == m_previews.end())
        return QStringLiteral("There's no motion being previewed.");
    // Save writes, commits and pushes: it does not start while a deploy runs, or the change would be written and left unsaved.
    if (m_pipeline.active)
        return QStringLiteral("A deploy is already running. Save to code again when it has finished.");
    // The agent's change against the project as it is now: what the designer changed meanwhile is merged around it.
    AgentWork &work = found->second.work;
    QString error;
    std::vector<WriteBack::FileChange> changes = work.collect(&error);
    if (!error.isEmpty())
        return error;
    // Only what the agent wrote, as the preview listed it: the preview's server ran in this worktree, and whatever else it
    // changed there (a lockfile, generated routes) was never the designer's to save.
    const QStringList wrote = found->second.info.files;
    changes.erase(std::remove_if(changes.begin(), changes.end(), [&](const WriteBack::FileChange &change) { return !wrote.contains(QDir(project).relativeFilePath(change.path)); }),
                  changes.end());
    if (changes.empty()) {
        releasePreview(project, false);
        emit previewChanged(project);
        return QStringLiteral("The motion is already in the code.");
    }
    if (const QString failure = WriteBack::apply(changes); !failure.isEmpty()) {
        WriteBack::restore(changes);
        return failure;
    }
    const Preview info = found->second.info;
    record(QStringLiteral("Animate: %1").arg(info.title.section(QStringLiteral(": "), -1)), info.summary, changes, project);
    releasePreview(project, false);
    emit previewChanged(project);
    // What was tuned in the preview goes on top, as a Save writes it; then both are committed in one commit.
    if (!pendingEdits(project).empty()) {
        QString request;
        if (const QString failure = liveWriteBack(&request, project); !failure.isEmpty())
            return failure;
    }
    return liveSave(project, true);
}
