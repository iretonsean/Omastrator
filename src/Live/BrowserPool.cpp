#include "Live/BrowserPool.h"
#include "Agent/BrowserPoolState.h"
#include <QJsonArray>
#include <QMetaObject>
#include <utility>

BrowserPool::BrowserPool(const Options &options, QObject *parent) : QObject(parent), m_options(options)
{
    if (m_options.profile.isEmpty())
        m_options.profile = Browser::defaultProfile();
    qRegisterMetaType<BrowserPool::CloseReason>();
    m_owner = QThread::currentThread();
    // Not our child: a child would move onto the thread it runs.
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("browser-pool"));
    // Created before the move, so it lives on the pool's thread.
    m_idle = new QTimer(this);
    m_idle->setSingleShot(true);
    m_idle->setInterval(m_options.idleMs);
    connect(m_idle, &QTimer::timeout, this, [this] {
        if (m_tabs.isEmpty())
            stopBrowser();
    });
    moveToThread(m_thread);
    m_thread->start();
}

BrowserPool::~BrowserPool()
{
    if (m_thread->isRunning()) {
        // The browser is stopped where it lives, then the thread ends.
        // and the pool goes back to the thread that made it, so its timer and children are deleted from there.
        QMetaObject::invokeMethod(this, [this] {
            stopBrowser();
            moveToThread(m_owner);
        }, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
    }
    delete m_thread;
}

void BrowserPool::open(const QUuid &frame, const QString &context)
{
    QMetaObject::invokeMethod(this, [this, frame, context] { doOpen(frame, context); }, Qt::QueuedConnection);
}

void BrowserPool::close(const QUuid &frame)
{
    QMetaObject::invokeMethod(this, [this, frame] {
        // Not opened yet: the open is dropped.
        m_pending.removeIf([&](const Pending &each) { return each.frame == frame; });
        doClose(frame, CloseReason::closed);
    }, Qt::QueuedConnection);
}

void BrowserPool::closeAll(bool wait)
{
    if (wait && QThread::currentThread() != m_thread && m_thread->isRunning()) {
        QMetaObject::invokeMethod(this, [this] { doCloseAll(CloseReason::closed); }, Qt::BlockingQueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(this, [this] { doCloseAll(CloseReason::reset); }, Qt::QueuedConnection);
}

void BrowserPool::doCloseAll(CloseReason reason)
{
    m_pending.clear();
    // The browser is still starting, in a nested loop of its own: it stops as soon as it is up.
    if (m_starting) {
        m_closeRequested = true;
        m_closeReason = reason;
        return;
    }
    const QList<QUuid> frames = m_tabs.keys();
    for (const QUuid &frame : frames)
        doClose(frame, reason);
    stopBrowser();
}

void BrowserPool::setShown(const QUuid &frame, bool shown)
{
    QMetaObject::invokeMethod(this, [this, frame, shown] {
        const auto found = m_tabs.find(frame);
        if (found == m_tabs.end())
            return;
        if (found->shown != shown || shown)
            found->lastShown = ++m_shownCounter;
        found->shown = shown;
    }, Qt::QueuedConnection);
}

void BrowserPool::call(const QUuid &frame, const QString &method, const QJsonObject &params, Reply reply)
{
    QMetaObject::invokeMethod(this, [this, frame, method, params, reply] {
        const auto found = m_tabs.constFind(frame);
        if (!m_browser || !m_running || (!frame.isNull() && found == m_tabs.constEnd())) {
            if (reply)
                reply({}, QStringLiteral("There is no tab for this frame."));
            return;
        }
        m_browser->cdp().call(method, params, frame.isNull() ? QString() : found->sessionId, reply);
    }, Qt::QueuedConnection);
}

void BrowserPool::run(std::function<void()> work)
{
    QMetaObject::invokeMethod(this, [work] { work(); }, Qt::QueuedConnection);
}

BrowserPool::Session BrowserPool::session(const QUuid &frame) const
{
    const auto found = m_tabs.constFind(frame);
    return found == m_tabs.constEnd() ? Session{} : Session{found->targetId, found->sessionId};
}

void BrowserPool::noteTabs()
{
    m_tabCount = static_cast<int>(m_tabs.size());
    if (m_tabs.isEmpty() && m_opening.isEmpty() && m_pending.isEmpty() && m_running)
        m_idle->start();
    else
        m_idle->stop();
}

void BrowserPool::doOpen(const QUuid &frame, const QString &context)
{
    if (m_tabs.contains(frame) || m_opening.contains(frame))
        return;
    for (const Pending &each : std::as_const(m_pending)) {
        if (each.frame == frame)
            return;
    }
    m_idle->stop();
    // Starting waits in nested event loops, so opens that arrive meanwhile wait their turn.
    if (m_starting) {
        m_pending.append({frame, context});
        return;
    }
    if (!m_browser || !m_browser->isRunning()) {
        m_starting = true;
        delete m_browser;
        m_browser = new Browser(this);
        Browser::Options browser;
        browser.headless = true;
        browser.cache = m_options.cache;
        browser.profile = m_options.profile;
        const QString failure = m_browser->start(browser);
        m_starting = false;
        if (!failure.isEmpty()) {
            m_closeRequested = false;
            m_browser->deleteLater();
            m_browser = nullptr;
            emit openFailed(frame, failure);
            const QList<Pending> waiting = std::exchange(m_pending, {});
            for (const Pending &each : waiting)
                emit openFailed(each.frame, failure);
            noteTabs();
            return;
        }
        m_running = true;
        m_processId = m_browser->processId();
        if (m_options.writeState) {
            BrowserPoolState::write({m_browser->processId(), m_options.profile});
        }
        // A tab that needs a download stops there: files are never saved without being asked.
        m_browser->cdp().call(QStringLiteral("Browser.setDownloadBehavior"), {{"behavior", "deny"}}, QString());
        connect(&m_browser->cdp(), &CdpConnection::event, this,
                [this](const QString &method, const QJsonObject &params, const QString &session) { onEvent(method, params, session); });
        connect(m_browser, &Browser::exited, this, [this] { lostBrowser(); });
        emit started();
        if (std::exchange(m_closeRequested, false)) {
            m_pending.clear();
            stopBrowser();
            emit closed(frame, m_closeReason);
            return;
        }
        const QList<Pending> waiting = std::exchange(m_pending, {});
        for (const Pending &each : waiting)
            doOpen(each.frame, each.context);
    }
    makeRoom();
    finishOpen(frame, context);
}

void BrowserPool::makeRoom()
{
    // Past the cap the paused tab shown least recently goes; if every tab is on screen, the oldest of them.
    while (m_tabs.size() + m_opening.size() >= m_options.maxTabs && !m_tabs.isEmpty()) {
        QUuid victim;
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (victim.isNull()) {
                victim = it.key();
                continue;
            }
            const Tab &best = m_tabs[victim];
            const bool better = it->shown != best.shown ? !it->shown : it->lastShown < best.lastShown;
            if (better)
                victim = it.key();
        }
        doClose(victim, CloseReason::evicted);
    }
}

void BrowserPool::finishOpen(const QUuid &frame, const QString &context)
{
    m_opening.append(frame);
    QJsonObject params{{"url", "about:blank"}};
    if (!context.isEmpty())
        params["browserContextId"] = context;
    auto fail = [this, frame](const QString &error) {
        m_opening.removeAll(frame);
        emit openFailed(frame, QStringLiteral("Could not open a tab in Chromium: %1").arg(error));
        noteTabs();
    };
    m_browser->cdp().call(QStringLiteral("Target.createTarget"), params, QString(), [=, this](const QJsonObject &created, const QString &error) {
        const QString target = created["targetId"].toString();
        if (target.isEmpty()) {
            fail(error);
            return;
        }
        m_browser->cdp().call(QStringLiteral("Target.attachToTarget"), {{"targetId", target}, {"flatten", true}}, QString(),
                              [=, this](const QJsonObject &attached, const QString &attachError) {
            const QString session = attached["sessionId"].toString();
            if (session.isEmpty()) {
                m_browser->cdp().call(QStringLiteral("Target.closeTarget"), {{"targetId", target}}, QString());
                fail(attachError);
                return;
            }
            // Asked to close while it was opening.
            if (!m_opening.contains(frame)) {
                m_browser->cdp().call(QStringLiteral("Target.closeTarget"), {{"targetId", target}}, QString());
                noteTabs();
                return;
            }
            m_opening.removeAll(frame);
            m_tabs.insert(frame, Tab{target, session, context, true, ++m_shownCounter});
            m_browser->cdp().call(QStringLiteral("Page.enable"), {}, session, [=, this](const QJsonObject &, const QString &) {
                if (m_tabs.contains(frame))
                    emit opened(frame);
            });
            noteTabs();
        });
    });
}

void BrowserPool::doClose(const QUuid &frame, CloseReason reason)
{
    if (const int opening = m_opening.removeAll(frame); opening > 0)
        noteTabs();
    const auto found = m_tabs.find(frame);
    if (found == m_tabs.end())
        return;
    const QString target = found->targetId;
    m_tabs.erase(found);
    if (m_browser && m_browser->isRunning())
        m_browser->cdp().call(QStringLiteral("Target.closeTarget"), {{"targetId", target}}, QString());
    emit closed(frame, reason);
    noteTabs();
}

void BrowserPool::stopBrowser(bool later)
{
    m_idle->stop();
    if (!m_browser)
        return;
    const QList<QUuid> frames = m_tabs.keys();
    m_tabs.clear();
    m_opening.clear();
    for (const QUuid &frame : frames)
        emit closed(frame, CloseReason::lost);
    m_browser->disconnect(this);
    m_browser->cdp().disconnect(this);
    if (later)
        m_browser->deleteLater();
    else
        delete m_browser;
    m_browser = nullptr;
    m_running = false;
    m_tabCount = 0;
    m_processId = 0;
    if (m_options.writeState)
        BrowserPoolState::clear();
    emit stopped();
}

void BrowserPool::lostBrowser()
{
    // Chromium went by itself (a crash, or the user closed it from outside): the frames find out and can open again.
    if (!m_browser || m_starting)
        return;
    stopBrowser(true);
}

QUuid BrowserPool::frameOfSession(const QString &sessionId) const
{
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (it->sessionId == sessionId)
            return it.key();
    }
    return {};
}

void BrowserPool::onEvent(const QString &method, const QJsonObject &params, const QString &sessionId)
{
    // A tab that goes by itself (it crashed, or the page closed it) is announced by the browser, not the tab.
    if (method == QLatin1String("Target.detachedFromTarget")) {
        if (const QUuid gone = frameOfSession(params["sessionId"].toString()); !gone.isNull())
            doClose(gone, CloseReason::lost);
        return;
    }
    const QUuid frame = frameOfSession(sessionId);
    if (sessionId.isEmpty() || frame.isNull())
        return;
    emit tabEvent(frame, method, params);
}
