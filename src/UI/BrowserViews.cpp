#include "UI/BrowserViews.h"
#include "Canvas/EditorCanvas.h"
#include "Document/BrowserAddress.h"
#include "Document/EditorSession.h"
#include "UI/LiveFrames.h"
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {
BrowserPool::Options &poolOptions()
{
    static BrowserPool::Options options;
    return options;
}

BrowserPool *&poolInstance()
{
    static BrowserPool *pool = nullptr;
    return pool;
}

QList<BrowserViews *> &instances()
{
    static QList<BrowserViews *> all;
    return all;
}

// Live's window and the sign-in window each hold the profile; frames wait while either does.
bool &liveWindow()
{
    static bool open = false;
    return open;
}

bool &signInWindow()
{
    static bool open = false;
    return open;
}

bool &profileWasBusy()
{
    static bool busy = false;
    return busy;
}

bool liveIsOpen()
{
    return liveWindow() || signInWindow();
}

// Below this on screen a frame is a thumbnail, and its page isn't worth running.
constexpr double minimumShownWidth = 160;
constexpr int castLimit = 2560;
constexpr qint64 castSettleMs = 150;
constexpr qint64 scaleSettleMs = 300;

int &pausedCloseMs()
{
    static int ms = 5 * 60 * 1000;
    return ms;
}
}

void BrowserViews::setPausedCloseMs(int ms)
{
    pausedCloseMs() = ms;
}

BrowserViews *BrowserViews::of(EditorSession &session)
{
    if (auto *existing = session.findChild<BrowserViews *>(QString(), Qt::FindDirectChildrenOnly))
        return existing;
    return new BrowserViews(session);
}

BrowserViews::BrowserViews(EditorSession &session) : QObject(&session), m_session(session)
{
    instances().append(this);
    // Once per process, before any frame can start the headless browser on a profile a window still holds.
    static const bool adopted = adoptSignInWindow();
    Q_UNUSED(adopted)
    m_clock.start();
    m_decoder.setMaxThreadCount(1);
    for (QTimer *timer : {&m_reconcile, &m_repaint})
        timer->setSingleShot(true);
    m_settle.setSingleShot(true);
    m_reconcile.setInterval(0);
    m_repaint.setInterval(16);
    m_flush.setInterval(1000);
    connect(&m_reconcile, &QTimer::timeout, this, &BrowserViews::reconcile);
    connect(&m_settle, &QTimer::timeout, this, &BrowserViews::reconcile);
    connect(&m_flush, &QTimer::timeout, this, &BrowserViews::flushLocations);
    m_pausedClose.setSingleShot(true);
    connect(&m_pausedClose, &QTimer::timeout, this, &BrowserViews::closeLongPaused);
    connect(&m_repaint, &QTimer::timeout, this, [this] {
        if (m_canvas && !m_dirty.isNull())
            m_canvas->update(m_dirty.toAlignedRect().adjusted(-2, -2, 2, 2));
        m_dirty = QRectF();
    });
    connect(&session, &EditorSession::changed, this, &BrowserViews::scheduleReconcile);
}

BrowserViews::~BrowserViews()
{
    instances().removeAll(this);
    // A decode in flight posts its result to this object, so it ends first.
    m_decoder.waitForDone();
    // The session may be going too, so nothing of it is touched here.
    if (BrowserPool *pool = poolInstance()) {
        for (const Entry &entry : std::as_const(m_entries)) {
            if (entry.state == State::live || entry.state == State::paused || entry.state == State::opening)
                pool->close(entry.key);
        }
    }
}

void BrowserViews::attach(EditorCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    detach(m_canvas);
    m_canvas = canvas;
    if (!canvas)
        return;
    canvas->setBrowserViewHost(this);
    canvas->installEventFilter(this);
    watchWindow();
    scheduleReconcile();
}

bool BrowserViews::onScreen() const
{
    if (!m_canvas || !m_canvas->isVisible())
        return false;
    const QWindow *window = m_canvas->window()->windowHandle();
    return window && window->isExposed();
}

void BrowserViews::watchWindow()
{
    QWindow *window = m_canvas ? m_canvas->window()->windowHandle() : nullptr;
    if (window == m_watchedWindow)
        return;
    if (m_watchedWindow)
        m_watchedWindow->removeEventFilter(this);
    m_watchedWindow = window;
    if (window)
        window->installEventFilter(this);
}

void BrowserViews::detach(EditorCanvas *canvas)
{
    if (!canvas || m_canvas != canvas)
        return;
    if (canvas->browserViewHost() == this)
        canvas->setBrowserViewHost(nullptr);
    canvas->removeEventFilter(this);
    if (m_watchedWindow)
        m_watchedWindow->removeEventFilter(this);
    m_watchedWindow = nullptr;
    m_canvas = nullptr;
    scheduleReconcile();
}

bool BrowserViews::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas) {
        switch (event->type()) {
        case QEvent::Show:
            watchWindow();
            scheduleReconcile();
            break;
        case QEvent::Hide:
        case QEvent::Resize:
            scheduleReconcile();
            break;
        default:
            break;
        }
    } else if (watched == m_watchedWindow && (event->type() == QEvent::Expose || event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        // Minimizing, or a switch of workspace, exposes or hides the window without a widget event.
        scheduleReconcile();
    }
    return QObject::eventFilter(watched, event);
}

BrowserViews::State BrowserViews::state(const QUuid &frame) const
{
    const auto found = m_entries.constFind(frame);
    return found == m_entries.constEnd() ? State::closed : found->state;
}

QUuid BrowserViews::poolKey(const QUuid &frame) const
{
    const auto found = m_entries.constFind(frame);
    return found == m_entries.constEnd() ? QUuid() : found->key;
}

QImage BrowserViews::picture(const QUuid &frame) const
{
    const auto found = m_entries.constFind(frame);
    return found == m_entries.constEnd() ? QImage() : found->image;
}

QString BrowserViews::message(const QUuid &frame) const
{
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return {};
    switch (state(frame)) {
    case State::resetPaused:
        return QStringLiteral("Paused by reset. Click to resume.");
    case State::liveOpen:
        if (isSigningIn())
            return QStringLiteral("Signing in…");
        return QStringLiteral("Omastrator's browser is open for Live. Frames resume when it closes.");
    case State::unavailable:
        return QStringLiteral("Chromium isn't installed, so this shows the last picture.");
    case State::failed:
        return m_entries.value(frame).failure;
    default:
        break;
    }
    // Live's own words over the last picture: the dev server starting, or why it didn't.
    if (const LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly); live && live->active(frame)) {
        const LiveFrames::Snapshot snapshot = live->snapshot(frame);
        if (snapshot.startingServer || snapshot.state == LiveSession::State::failed)
            return snapshot.message;
    }
    return object->browser->url.isEmpty() ? QStringLiteral("No page yet.") : QString();
}

void BrowserViews::resume(const QUuid &frame)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end() || found->state != State::resetPaused)
        return;
    found->state = State::closed;
    found->lost = 0;
    found->evicted = false;
    note(frame, State::closed);
    scheduleReconcile();
}

// Pool ---------------------------------------------------------------------------

void BrowserViews::setPoolOptions(const BrowserPool::Options &options)
{
    poolOptions() = options;
}

const BrowserPool::Options &BrowserViews::poolSettings()
{
    return poolOptions();
}

BrowserPool *BrowserViews::pool()
{
    BrowserPool *&instance = poolInstance();
    if (!instance) {
        // No parent: an object with a parent can't move to the pool's thread.
        instance = new BrowserPool(poolOptions());
        static const bool hooked = QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { shutdownPool(); }) != nullptr;
        Q_UNUSED(hooked)
    }
    return instance;
}

void BrowserViews::shutdownPool()
{
    BrowserPool *&instance = poolInstance();
    if (!instance)
        return;
    // Their sessions live on the pool's thread, which is about to end.
    LiveFrames::stopAll();
    delete instance;
    instance = nullptr;
    for (BrowserViews *views : std::as_const(instances()))
        views->forgetTabs();
}

void BrowserViews::forgetTabs()
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        it->casting = false;
        it->frozen = false;
        it->applied = {};
        it->navigated = QUrl();
        it->decoding = false;
        it->pendingAck = -1;
        if (it->state == State::live || it->state == State::paused || it->state == State::opening)
            it->state = State::closed;
    }
    scheduleReconcile();
}

void BrowserViews::connectPool()
{
    BrowserPool *pool = BrowserViews::pool();
    if (m_connectedPool == pool)
        return;
    m_connectedPool = pool;
    connect(pool, &BrowserPool::opened, this, &BrowserViews::onOpened);
    connect(pool, &BrowserPool::openFailed, this, &BrowserViews::onOpenFailed);
    connect(pool, &BrowserPool::closed, this, &BrowserViews::onClosed);
    connect(pool, &BrowserPool::tabEvent, this, &BrowserViews::onTabEvent);
    connect(pool, &BrowserPool::popup, this, &BrowserViews::onPopup);
}

void BrowserViews::call(const Entry &entry, const QString &method, const QJsonObject &params)
{
    BrowserViews::pool()->call(entry.key, method, params);
}

QUuid BrowserViews::frameOf(const QUuid &key) const
{
    return m_frameOfKey.value(key);
}

void BrowserViews::resetAll()
{
    // Every frame's Live ends first, so nothing re-attaches to a tab the reset closes.
    LiveFrames::stopAll();
    for (BrowserViews *views : std::as_const(instances())) {
        for (auto it = views->m_entries.begin(); it != views->m_entries.end(); ++it) {
            views->savePicture(it.key(), *it);
            it->casting = false;
            it->frozen = false;
            it->applied = {};
            it->navigated = QUrl();
            views->note(it.key(), State::resetPaused);
        }
    }
    // Never created means nothing to close, and a reset doesn't start one.
    if (BrowserPool *pool = poolInstance())
        pool->closeAll();
}

void BrowserViews::setLiveOpen(bool open)
{
    liveWindow() = open;
    profileHoldChanged();
}

bool BrowserViews::liveWindowIsOpen()
{
    return liveWindow();
}

void BrowserViews::setSignInWindow(bool open)
{
    signInWindow() = open;
    profileHoldChanged();
}

bool BrowserViews::isSigningIn()
{
    return signInWindow();
}

void BrowserViews::profileHoldChanged()
{
    const bool open = liveIsOpen();
    if (profileWasBusy() == open)
        return;
    profileWasBusy() = open;
    if (open) {
        // The profile is free before Live starts its own Chromium on it.
        if (BrowserPool *pool = poolInstance())
            pool->closeAll(true);
    }
    for (BrowserViews *views : std::as_const(instances())) {
        for (auto it = views->m_entries.begin(); it != views->m_entries.end(); ++it) {
            if (open) {
                views->savePicture(it.key(), *it);
                it->casting = false;
                it->frozen = false;
                it->applied = {};
                it->navigated = QUrl();
                if (it->state != State::resetPaused)
                    views->note(it.key(), State::liveOpen);
            } else if (it->state == State::liveOpen) {
                views->note(it.key(), State::closed);
            }
        }
        views->scheduleReconcile();
    }
}

// Reconciling ---------------------------------------------------------------------

void BrowserViews::scheduleReconcile()
{
    if (!m_reconcile.isActive())
        m_reconcile.start();
}

bool BrowserViews::sameAddress(const QUrl &a, const QUrl &b)
{
    const auto normal = [](QUrl url) {
        url = url.adjusted(QUrl::NormalizePathSegments);
        // A bare host and a host with "/" are the same page.
        if (url.path().isEmpty() && !url.host().isEmpty())
            url.setPath(QStringLiteral("/"));
        return url;
    };
    return normal(a) == normal(b);
}

namespace {
bool sameOrigin(const QUrl &a, const QUrl &b)
{
    return a.scheme() == b.scheme() && a.host() == b.host() && a.port(-1) == b.port(-1);
}

QUrl moved(const QUrl &url, const QUrl &to)
{
    QUrl result = url;
    result.setScheme(to.scheme());
    result.setHost(to.host());
    result.setPort(to.port(-1));
    return result;
}
}

QUrl BrowserViews::toTabUrl(const QUuid &frame, const QUrl &document) const
{
    const auto swap = m_swaps.constFind(frame);
    return swap != m_swaps.constEnd() && !swap->retired && sameOrigin(document, swap->production) ? moved(document, swap->dev) : document;
}

QUrl BrowserViews::toDocumentUrl(const QUuid &frame, const QUrl &tab) const
{
    const auto swap = m_swaps.constFind(frame);
    return swap != m_swaps.constEnd() && sameOrigin(tab, swap->dev) ? moved(tab, swap->production) : tab;
}

void BrowserViews::settleSwap(const QUuid &frame, const QUrl &tab)
{
    const auto swap = m_swaps.constFind(frame);
    if (swap != m_swaps.constEnd() && swap->retired && !sameOrigin(tab, swap->dev))
        m_swaps.remove(frame);
}

void BrowserViews::useDevServer(const QUuid &frame, const QUrl &server)
{
    const auto entry = m_entries.find(frame);
    if (server.isEmpty()) {
        const auto swap = m_swaps.find(frame);
        if (swap == m_swaps.end() || swap->retired)
            return;
        // Still on the server's page when Live ends: the tab goes back to production, and its events keep the swap until then.
        if (entry != m_entries.end() && !entry->navigated.isEmpty() && !sameOrigin(entry->navigated, swap->dev))
            m_swaps.erase(swap);
        else
            swap->retired = true;
    } else {
        const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
        if (!object || !object->browser || object->browser->url.isEmpty())
            return;
        m_swaps.insert(frame, DevSwap{server, object->browser->url});
    }
    // The next sync sees an address the tab isn't on, and goes there, keeping the scroll.
    if (entry != m_entries.end()) {
        const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
        if (!object || !object->browser || entry->navigated != toTabUrl(frame, object->browser->url))
            entry->navigated = QUrl();
    }
    scheduleReconcile();
    emit frameChanged(frame);
}

void BrowserViews::note(const QUuid &frame, State state)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    found->state = state;
    scheduleRepaint(frame);
    emit frameChanged(frame);
}

BrowserViews::Want BrowserViews::wanted(const QUuid &frame, const VectorObject &object, int streaming) const
{
    Want want;
    const VectorDocument &document = *m_session.document();
    const QRectF box = document.bounds(frame);
    want.css = QSize(std::max(1, int(std::lround(box.width()))), std::max(1, int(std::lround(box.height()))));
    if (!onScreen() || !document.isOnCurrentPage(frame) || !document.isEffectivelyVisible(frame))
        return want;
    want.view = m_canvas->documentToView().mapRect(box);
    want.shown = want.view.width() >= minimumShownWidth && want.view.intersects(QRectF(m_canvas->rect()));
    const double backing = std::max(1.0, m_session.viewport.backingScale);
    // 1 CSS px per document point, so this is device pixels per CSS pixel.
    const double density = want.view.width() * backing / std::max(1.0, box.width());
    want.scale = density > 1.5 ? 2 : 1;
    const auto rounded = [](double pixels) { return int(std::min<double>(castLimit, std::ceil(std::max(64.0, pixels) / 64) * 64)); };
    want.cast = QSize(rounded(want.view.width() * backing), rounded(want.view.height() * backing));
    want.frameGap = m_session.isSelected(object.id) || m_browsed.contains(object.id) ? 0 : (streaming > 4 ? 66 : 33);
    return want;
}

void BrowserViews::reconcile()
{
    if (!m_session.hasDocument()) {
        const QList<QUuid> gone = m_entries.keys();
        for (const QUuid &frame : gone)
            dropEntry(frame);
        m_flush.stop();
        return;
    }
    const VectorDocument &document = *m_session.document();
    // A click on a frame that reset paused starts it again.
    for (const QUuid &id : m_session.selection()) {
        if (std::find(m_lastSelection.begin(), m_lastSelection.end(), id) == m_lastSelection.end())
            resume(id);
    }
    m_lastSelection = m_session.selection();
    if (m_session.tool() != Tool::browse)
        m_browsed.clear();

    QSet<QUuid> present;
    int streaming = 0;
    for (const VectorObject &object : document.objects) {
        if (!object.browser)
            continue;
        present.insert(object.id);
        if (onScreen() && document.isOnCurrentPage(object.id))
            ++streaming;
    }
    // An override that names nothing runnable counts as no Chromium too.
    // Looked for only when there is a frame to run: reconcile follows every edit, drag moves included.
    bool installed = true;
    if (!present.isEmpty()) {
        const QString chromium = Browser::executable();
        installed = !chromium.isEmpty() && QFileInfo(chromium).isExecutable();
    }
    for (const VectorObject &object : document.objects) {
        if (!object.browser)
            continue;
        const bool added = !m_entries.contains(object.id);
        Entry &entry = m_entries[object.id];
        if (added) {
            entry.key = QUuid::createUuidV5(m_scope, object.id.toString());
            m_frameOfKey.insert(entry.key, object.id);
            entry.scroll = object.browser->scroll;
        }
        const Want want = wanted(object.id, object, streaming);
        if (!installed && entry.state != State::resetPaused && entry.state != State::liveOpen) {
            if (entry.state != State::unavailable)
                note(object.id, State::unavailable);
            continue;
        }
        if (installed && entry.state == State::unavailable)
            note(object.id, State::closed);
        if (liveIsOpen() && entry.state == State::closed) {
            note(object.id, State::liveOpen);
            continue;
        }
        sync(object.id, entry, want);
    }
    const QList<QUuid> known = m_entries.keys();
    for (const QUuid &frame : known) {
        if (!present.contains(frame))
            dropEntry(frame);
    }
    bool casting = false;
    for (const Entry &entry : std::as_const(m_entries))
        casting = casting || entry.casting;
    if (casting && !m_flush.isActive())
        m_flush.start();
    else if (!casting)
        m_flush.stop();
}

void BrowserViews::sync(const QUuid &frame, Entry &entry, const Want &want)
{
    const VectorObject *object = m_session.document()->find(frame);
    const QUrl url = object->browser->url;
    if (entry.state == State::resetPaused || entry.state == State::liveOpen || entry.state == State::unavailable)
        return;
    if (entry.state == State::opening)
        return;
    entry.shown = want.shown;
    if (!want.shown) {
        // Coming back later opens it again; only the cap's eviction waits for this.
        entry.evicted = false;
        entry.failure.clear();
        if (entry.state == State::failed)
            note(frame, State::closed);
        if (entry.state == State::live || entry.state == State::paused)
            pause(frame, entry);
        return;
    }
    if (!BrowserAddress::allowed(url))
        return;
    if (entry.state == State::failed)
        return;
    if (entry.state == State::closed) {
        if (entry.evicted || entry.lost >= 3)
            return;
        connectPool();
        BrowserViews::pool()->open(entry.key);
        note(frame, State::opening);
        return;
    }

    // A tab, shown: awake, at the frame's size, on its address, streaming.
    if (entry.frozen) {
        call(entry, QStringLiteral("Page.setWebLifecycleState"), {{"state", "active"}});
        // Freezing hid the page and "active" doesn't show it again, so it would paint no more frames; focus emulation
        // shows it.
        call(entry, QStringLiteral("Emulation.setFocusEmulationEnabled"), {{"enabled", true}});
        entry.frozen = false;
    }
    BrowserViews::pool()->setShown(entry.key, true);
    const qint64 now = m_clock.elapsed();
    Settling &wait = entry.settling;
    entry.frameGap = want.frameGap;
    if (wait.cast != want.cast) {
        wait.cast = want.cast;
        wait.castSince = now;
    }
    if (wait.scale != want.scale) {
        wait.scale = want.scale;
        wait.scaleSince = now;
    }
    bool waiting = false;
    Applied &done = entry.applied;
    const bool first = done.css.isEmpty();
    const bool resized = !first && done.css != want.css;
    if (first || done.css != want.css) {
        // The frame's own size follows at once, since a resize is the point of the preview; the density waits.
        const int density = first ? want.scale : done.scale;
        call(entry, QStringLiteral("Emulation.setDeviceMetricsOverride"),
             {{"width", want.css.width()}, {"height", want.css.height()}, {"deviceScaleFactor", density}, {"mobile", false}});
        done.css = want.css;
        done.scale = density;
    }
    if (done.scale != want.scale) {
        if (now - wait.scaleSince >= scaleSettleMs) {
            call(entry, QStringLiteral("Emulation.setDeviceMetricsOverride"),
                 {{"width", want.css.width()}, {"height", want.css.height()}, {"deviceScaleFactor", want.scale}, {"mobile", false}});
            done.scale = want.scale;
        } else {
            waiting = true;
        }
    }
    const QUrl tabUrl = toTabUrl(frame, url);
    if (!sameAddress(entry.navigated, tabUrl) || entry.navigated.isEmpty()) {
        entry.navigated = tabUrl;
        entry.loading = true;
        entry.restoreScroll = !entry.scroll.isNull();
        call(entry, QStringLiteral("Page.navigate"), {{"url", tabUrl.toString()}});
        emit frameChanged(frame);
    }
    const auto cast = [&](QSize size) {
        // A screencast that is running is stopped first: Chromium ignores a second start.
        if (entry.casting)
            call(entry, QStringLiteral("Page.stopScreencast"));
        call(entry, QStringLiteral("Page.startScreencast"),
             {{"format", "jpeg"}, {"quality", 80}, {"maxWidth", size.width()}, {"maxHeight", size.height()}, {"everyNthFrame", 1}});
        entry.casting = true;
        done.cast = size;
    };
    bool started = false;
    if (!entry.casting || done.cast != want.cast) {
        if (!entry.casting || now - wait.castSince >= castSettleMs) {
            cast(want.cast);
            started = true;
        } else {
            waiting = true;
        }
    }
    // Chromium doesn't send the reflowed page after a resize on its own: starting again asks for it, at the size the
    // frame has been casting at until the new one settles.
    if (resized && !started && entry.casting)
        cast(done.cast);
    if (entry.state != State::live)
        note(frame, State::live);
    if (waiting)
        m_settle.start(int(castSettleMs) + 10);
}

void BrowserViews::pause(const QUuid &frame, Entry &entry)
{
    if (entry.casting) {
        call(entry, QStringLiteral("Page.stopScreencast"));
        entry.casting = false;
        entry.applied.cast = {};
    }
    if (!entry.frozen) {
        // A page emulating focus stays visible, and a visible page doesn't freeze.
        call(entry, QStringLiteral("Emulation.setFocusEmulationEnabled"), {{"enabled", false}});
        call(entry, QStringLiteral("Page.setWebLifecycleState"), {{"state", "frozen"}});
        entry.frozen = true;
    }
    BrowserViews::pool()->setShown(entry.key, false);
    savePicture(frame, entry);
    if (entry.state != State::paused) {
        entry.pausedAt = m_clock.elapsed();
        if (!m_pausedClose.isActive())
            m_pausedClose.start(pausedCloseMs());
    }
    note(frame, State::paused);
}

void BrowserViews::closeLongPaused()
{
    const qint64 now = m_clock.elapsed();
    qint64 next = -1;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->state != State::paused)
            continue;
        const qint64 left = it->pausedAt + pausedCloseMs() - now;
        if (left <= 0) {
            // The pool's closed signal puts it back to closed; coming on screen again opens a new tab.
            savePicture(it.key(), *it);
            BrowserViews::pool()->close(it->key);
        } else {
            next = next < 0 ? left : std::min(next, left);
        }
    }
    if (next >= 0)
        m_pausedClose.start(int(next) + 10);
}

void BrowserViews::dropEntry(const QUuid &frame)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    if (LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly))
        live->stop(frame);
    if (found->state == State::live || found->state == State::paused || found->state == State::opening)
        BrowserViews::pool()->close(found->key);
    m_frameOfKey.remove(found->key);
    m_entries.erase(found);
}
