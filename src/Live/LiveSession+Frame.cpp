#include "Live/LiveSession.h"
#include "Agent/Capture.h"
#include "Live/OverlayScript.h"
#include "Live/Registry.h"
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <utility>

namespace {
bool isWeb(const QUrl &url)
{
    return url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
}

bool isLoopback(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("[::1]") || host == QLatin1String("::1");
}

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host() == b.host() && a.port(-1) == b.port(-1);
}
}

// Live in a Browser View's tab (docs/LIVE-IN-FRAME.md).
void LiveSession::runFrame(const Target &target)
{
    Q_UNUSED(target)
    // Everything from the pool arrives on this thread, its own.
    connect(m_pool, &BrowserPool::tabEvent, this, [this](const QUuid &frame, const QString &method, const QJsonObject &params) {
        if (frame == m_frame && m_page)
            onEvent(method, params, m_page->sessionId);
    });
    connect(m_pool, &BrowserPool::opened, this, [this](const QUuid &frame) {
        if (frame != m_frame || m_page)
            return;
        const int generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] {
            if (generation == m_generation)
                attachFrame();
        });
    });
    connect(m_pool, &BrowserPool::closed, this, [this](const QUuid &frame) {
        if (frame == m_frame)
            tabGone();
    });
    attachFrame();
}

void LiveSession::attachFrame()
{
    if (!m_pool || m_page)
        return;
    const BrowserPool::Session tab = m_pool->session(m_frame);
    if (tab.sessionId.isEmpty() || !m_pool->cdp()) {
        setState(State::starting, QStringLiteral("Waiting for the page…"));
        return;
    }
    const int generation = m_generation;
    m_page = Browser::Page{tab.targetId, tab.sessionId};
    QString error;
    call(cdp(), QStringLiteral("Runtime.enable"), {}, tab.sessionId, &error);
    if (generation != m_generation || !m_page)
        return;
    if (!error.isEmpty())
        return fail(QStringLiteral("Couldn't prepare the page: %1").arg(error));
    if (const QString failure = prepare(); !failure.isEmpty())
        return generation != m_generation ? void() : fail(failure);
    if (generation != m_generation || !m_page)
        return;
    const QStringList where = evaluate(QStringLiteral("location.href + '\\n' + document.readyState"), &error).toString().split(QLatin1Char('\n'));
    if (generation != m_generation || !m_page)
        return;
    const bool loaded = where.size() == 2 && where[0] != QLatin1String("about:blank") && where[1] == QLatin1String("complete");
    if (where.size() == 2 && where[0] != QLatin1String("about:blank"))
        m_url = QUrl(where[0]);
    // A folder given without a page to name its site belongs to the first site the tab shows, once.
    if (!m_targetFolder.isEmpty() && m_targetOrigin.isEmpty() && isWeb(m_url))
        m_targetOrigin = EditSets::originOf(m_url);
    frameProject();
    if (m_serving)
        return;
    setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
    if (!loaded)
        return;
    // The page was there before the overlay: it goes in now, and the script brings it back after each navigation.
    evaluate(frameOverlayScript(), &error);
    if (generation != m_generation || !m_page)
        return;
    pageLoaded();
}

void LiveSession::frameProject()
{
    QString folder;
    // On the dev server the address is the server's, which is nobody's to register.
    const bool served = m_lease && !m_serverProject.isEmpty() && sameOrigin(m_url, m_serverUrl);
    // The folder the frame was opened with is for the site it was opened on; browsing to another site looks it up.
    const bool targeted = !m_targetFolder.isEmpty() && (m_targetOrigin.isEmpty() || (isWeb(m_url) && EditSets::originOf(m_url) == m_targetOrigin));
    // Motion previewed from a worktree (docs/MOTION.md, section 4): the page is the project's page, on a server of its own.
    const bool previewed = !m_previewOrigin.isEmpty() && !m_project.isEmpty() && isWeb(m_url) && sameOrigin(m_url, m_previewOrigin);
    if (served) {
        folder = m_serverProject;
    } else if (previewed) {
        folder = m_project;
    } else if (targeted) {
        folder = m_targetFolder;
        if (isWeb(m_url) && m_targetRemember)
            ProjectRegistry::remember(m_url, folder);
    } else if (isWeb(m_url)) {
        folder = ProjectRegistry::folderFor(m_url).value_or(QString());
    } else {
        folder = m_project;
    }
    const QString project = folder.isEmpty() ? QString() : QFileInfo(folder).canonicalFilePath();
    const bool moved = project != m_project;
    if (moved)
        leaveProject();
    m_project = project;
    // A dev server for the project the frame has left is let go; the new one starts below.
    // Letting go mid-start drops the answer, so the start ends here.
    bool abandoned = false;
    if (m_lease && !served && project != m_serverProject) {
        releaseServer(false);
        abandoned = std::exchange(m_serving, false);
    }
    if (m_serving) {
        if (moved)
            emit changed();
        return;
    }
    if (needsServer()) {
        m_serving = true;
        const int generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] { serveProject(generation); });
        return;
    }
    if (!moved && !abandoned)
        return;
    if (m_state == State::running || abandoned)
        setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
    else
        emit changed();
}

// The frame is about to show another project or a site that isn't the user's. What it had edited on the project goes to
// its owner, who holds it for Deploy; a mock-up's edits that weren't kept are left behind, and so are the undo steps.
void LiveSession::leaveProject()
{
    if (!m_project.isEmpty() && !m_edits.empty())
        emit editsLeft(m_project, m_edits);
    m_edits.clear();
    forgetSteps();
}

bool LiveSession::needsServer() const
{
    return m_pool && !m_serving && !m_lease && !m_project.isEmpty() && isWeb(m_url) && !isLoopback(m_url);
}

void LiveSession::serveProject(int generation)
{
    if (generation != m_generation || !m_pool) {
        m_serving = false;
        return;
    }
    const QString folder = m_project;
    if (folder.isEmpty()) {
        m_serving = false;
        return;
    }
    setState(State::starting, QStringLiteral("Starting the project…"));
    m_serverFolder = DevServers::keyFor(folder);
    m_serverProject = folder;
    // Carries on from the answer instead of waiting for it: this is the pool's thread, and it has every tab to serve.
    m_lease = DevServers::shared().acquire(folder, this, [this, generation, folder](const DevServers::Result &result) {
        if (generation != m_generation)
            return;
        m_serving = false;
        if (!result.error.isEmpty()) {
            m_lease = 0;
            m_serverProject.clear();
            return fail(QStringLiteral("Couldn't start the project: %1").arg(result.error.section(QLatin1Char('\n'), 0, 0).trimmed()));
        }
        if (m_project != folder) {
            // The frame went to another site while the server started.
            releaseServer(false);
            frameProject();
            if (!m_serving)
                setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
            return;
        }
        m_serverUrl = result.url;
        m_serverCommand = result.command;
        // The frame's tab loads the dev server next; the overlay comes with that page.
        setState(State::running);
    });
    // Turned off before the server was asked for: it freezes as soon as it answers.
    if (m_serverPaused && m_lease)
        DevServers::shared().setPaused(m_lease, true);
}

void LiveSession::tabGone()
{
    // The session outlives its tab: it waits, keeps its edits, and re-attaches when the frame has a tab again.
    m_page.reset();
    m_selection = {};
    m_geometry = {};
    m_forced.clear();
    m_forcedNodes.clear();
    m_agentsOn = false;
    m_scriptId.clear();
    if (m_state == State::running || m_state == State::starting)
        setState(State::starting, QStringLiteral("Waiting for the page…"));
    emit geometryChanged();
}

void LiveSession::leaveFrame()
{
    if (!m_pool)
        return;
    m_pool->disconnect(this);
    if (m_page && m_pool->cdp()) {
        CdpConnection &pool = *m_pool->cdp();
        // The page plays on and its forced states end, as they were.
        motionLetGo(2000);
        // Short waits: leaving never hangs on a browser that has stopped answering.
        call(pool, QStringLiteral("Runtime.evaluate"), {{"expression", "window.__oma && window.__oma.leave()"}}, m_page->sessionId, nullptr, 2000);
        if (!m_scriptId.isEmpty())
            pool.call(QStringLiteral("Page.removeScriptToEvaluateOnNewDocument"), {{"identifier", m_scriptId}}, m_page->sessionId);
    }
    m_scriptId.clear();
    m_pool = nullptr;
}
