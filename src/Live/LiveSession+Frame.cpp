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
    cdp().callAndWait(QStringLiteral("Runtime.enable"), {}, tab.sessionId, &error);
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
    frameProject();
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
    QString folder = m_targetFolder;
    if (!folder.isEmpty() && (m_url.scheme() == QLatin1String("http") || m_url.scheme() == QLatin1String("https")))
        ProjectRegistry::remember(m_url, folder);
    else if (folder.isEmpty() && !m_url.isEmpty())
        folder = ProjectRegistry::folderFor(m_url).value_or(QString());
    const QString project = folder.isEmpty() ? QString() : QFileInfo(folder).canonicalFilePath();
    if (project == m_project)
        return;
    m_project = project;
    if (m_state == State::running)
        setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
    else
        emit changed();
}

void LiveSession::tabGone()
{
    // The session outlives its tab: it waits, keeps its edits, and re-attaches when the frame has a tab again.
    m_page.reset();
    m_selection = {};
    m_geometry = {};
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
        // Short waits: leaving never hangs on a browser that has stopped answering.
        pool.callAndWait(QStringLiteral("Runtime.evaluate"), {{"expression", "window.__oma && window.__oma.leave()"}}, m_page->sessionId, nullptr, 2000);
        if (!m_scriptId.isEmpty())
            pool.call(QStringLiteral("Page.removeScriptToEvaluateOnNewDocument"), {{"identifier", m_scriptId}}, m_page->sessionId);
    }
    m_scriptId.clear();
    m_pool = nullptr;
}
