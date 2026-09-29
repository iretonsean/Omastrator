#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Registry.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMenu>

// Deploy, Save, Review Changes and History from a Browser View (docs/LIVE-IN-FRAME.md, section 4): the bar's Deploy
// button and its stages, and the menu's project actions. They all go through the window's AgentBridge, which is the
// one pipeline the Live window uses, given the frame's project.

namespace {
constexpr qint64 resultShownMs = 8000;

QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}

QString stageWord(const QString &stage)
{
    if (stage == QLatin1String("committing"))
        return QStringLiteral("Committing…");
    if (stage == QLatin1String("github"))
        return QStringLiteral("Creating repository…");
    if (stage == QLatin1String("pushing"))
        return QStringLiteral("Pushing…");
    if (stage == QLatin1String("deploying"))
        return QStringLiteral("Deploying…");
    return QStringLiteral("Writing…");
}
}

void BrowserViews::setAgent(AgentBridge *agent)
{
    if (m_agent == agent)
        return;
    if (m_agent)
        disconnect(m_agent, nullptr, this, nullptr);
    m_agent = agent;
    if (!agent)
        return;
    // The stage and the result show on the bar; the result goes away by itself, so it repaints once more then.
    connect(agent, &AgentBridge::waitingChanged, this, [this] {
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
            scheduleRepaint(it.key());
    });
    connect(agent, &AgentBridge::liveReviewChanged, this, [this] {
        const AgentBridge::DeployState &state = m_agent->deployState();
        if (state.deployed && !state.running && state.finishedAt > 0)
            m_deployedAt[state.folder] = state.finishedAt;
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
            scheduleRepaint(it.key());
        QTimer::singleShot(resultShownMs + 100, this, [this] {
            for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
                scheduleRepaint(it.key());
        });
    });
}

QString BrowserViews::projectOf(const QUuid &frame) const
{
    if (!owned(frame))
        return {};
    const LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (live && live->active(frame)) {
        const QString project = live->snapshot(frame).project;
        if (!project.isEmpty())
            return canonical(project);
    }
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return {};
    const std::optional<QString> folder = ProjectRegistry::folderFor(object->browser->url);
    return folder ? canonical(*folder) : QString();
}

void BrowserViews::fillDeploy(const QUuid &frame, Bar &bar) const
{
    if (!m_agent)
        return;
    const QString project = projectOf(frame);
    if (project.isEmpty())
        return;
    const AgentBridge::DeployState &state = m_agent->deployState();
    const bool mine = !state.folder.isEmpty() && canonical(state.folder) == project;
    if (mine && state.running) {
        bar.deploy = stageWord(state.stage);
        bar.deployBusy = true;
        bar.deployTip = state.message;
        return;
    }
    if (mine && state.stage == QLatin1String("failed")) {
        bar.deploy = QStringLiteral("Deploy failed");
        bar.deployFailed = true;
        bar.deployTip = QStringLiteral("%1\nClick for details.").arg(state.message);
        return;
    }
    if (mine && state.stage == QLatin1String("done") && QDateTime::currentMSecsSinceEpoch() - state.finishedAt < resultShownMs) {
        const QUrl url(state.url);
        bar.deploy = state.url.isEmpty() ? state.message : QStringLiteral("Live at %1").arg(url.host().isEmpty() ? state.url : url.host());
        bar.deployTip = state.url.isEmpty() ? state.message : QStringLiteral("%1\nClick to open it.").arg(state.url);
        return;
    }
    // Something to send: edits still pending, or a write-back made since this project last deployed.
    bool pending = !m_agent->pendingEdits(project).empty();
    const qint64 deployed = m_deployedAt.value(project, 0);
    for (const WriteBack::Review &review : m_agent->liveReviews()) {
        if (pending)
            break;
        pending = canonical(review.folder) == project && review.time.toMSecsSinceEpoch() > deployed;
    }
    if (!pending)
        return;
    bar.deploy = QStringLiteral("Deploy");
    bar.deployTip = QStringLiteral("Write the edits into the code, commit, push, and deploy to production");
}

void BrowserViews::runProjectAction(const QUuid &frame, Action action)
{
    const auto fail = [this](const QString &why) {
        if (!why.isEmpty())
            emit notice(why);
    };
    if (action == Action::stopLive) {
        LiveFrames::of(m_session)->stop(frame);
        return;
    }
    if (!m_agent) {
        fail(QStringLiteral("Open the project's window to deploy."));
        return;
    }
    const QString project = projectOf(frame);
    if (project.isEmpty()) {
        fail(QStringLiteral("This page isn't one of your sites, so there's no code to deploy."));
        return;
    }
    // Review Changes and History follow the selected frame's project.
    if (!m_session.isSelected(frame))
        m_session.select({frame});
    QWidget *window = m_canvas ? m_canvas->window() : nullptr;
    const AgentBridge::DeployState &state = m_agent->deployState();
    const bool mine = !state.folder.isEmpty() && canonical(state.folder) == project;
    switch (action) {
    case Action::reviewChanges:
        m_agent->useProject(project);
        m_agent->showLivePanel(true);
        return;
    case Action::history:
        m_agent->useProject(project);
        m_agent->showHistoryPanel();
        return;
    case Action::deployButton:
        if (mine && state.running)
            return;
        if (mine && state.stage == QLatin1String("failed")) {
            fail(m_agent->showDeployLog());
            return;
        }
        if (mine && state.stage == QLatin1String("done") && !state.url.isEmpty() && QDateTime::currentMSecsSinceEpoch() - state.finishedAt < resultShownMs) {
            QDesktopServices::openUrl(QUrl(state.url));
            return;
        }
        [[fallthrough]];
    case Action::deploy: {
        AgentBridge::DeployRequest request;
        request.folder = project;
        bool needsAnswer = false;
        fail(m_agent->liveDeploy(request, &needsAnswer));
        if (needsAnswer)
            AgentSheets::deploy(*m_agent, window, project, true);
        return;
    }
    case Action::save:
        fail(m_agent->liveSave(project));
        return;
    default:
        return;
    }
}

void BrowserViews::addProjectActions(const QUuid &frame, QMenu *menu)
{
    const QString project = projectOf(frame);
    const bool busy = m_agent && m_agent->deployState().running;
    const auto item = [this, menu, frame](const QString &title, Action action, bool enabled) {
        QAction *added = menu->addAction(title);
        added->setEnabled(enabled);
        connect(added, &QAction::triggered, menu, [this, frame, action] { runProjectAction(frame, action); });
    };
    const bool haveProject = m_agent && !project.isEmpty();
    item(QStringLiteral("Deploy"), Action::deploy, haveProject && !busy);
    item(QStringLiteral("Save"), Action::save, haveProject && !busy);
    item(QStringLiteral("Review Changes"), Action::reviewChanges, haveProject);
    item(QStringLiteral("History"), Action::history, haveProject);
    menu->addSeparator();
    addBuildActions(frame, menu);
    menu->addSeparator();
    item(QStringLiteral("Stop Live"), Action::stopLive, LiveFrames::of(m_session)->active(frame));
}
