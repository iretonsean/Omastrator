#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"

// The frame's Browser View switch (docs/BROWSER-VIEW.md, "The Browser View switch"): on runs the project's dev server and
// streams the page, off freezes the server and keeps the last picture, and quitting stops every server.

namespace {
bool isLoopback(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1") || host == QLatin1String("[::1]");
}
}

void BrowserViews::setBrowserViewOn(const QUuid &frame, bool on)
{
    const bool before = m_session.browserViewOn(frame);
    m_session.setBrowserViewOn(frame, on);
    // A locked document, or not a frame: nothing changed, so nothing starts.
    if (m_session.browserViewOn(frame) != before)
        browserViewSwitched(frame, on);
}

bool BrowserViews::browserViewOn(const QUuid &frame) const
{
    return m_session.browserViewOn(frame);
}

void BrowserViews::browserViewSwitched(const QUuid &frame, bool on)
{
    if (on)
        m_serveWanted.insert(frame);
    else
        m_serveWanted.remove(frame);
    scheduleReconcile();
}

BrowserViews::Server BrowserViews::server(const QUuid &frame) const
{
    const LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (!live || !live->active(frame))
        return m_serveWanted.contains(frame) && browserViewOn(frame) ? Server::starting : Server::none;
    const LiveFrames::Snapshot snapshot = live->snapshot(frame);
    if (snapshot.state == LiveSession::State::failed)
        return Server::failed;
    if (snapshot.serverPaused)
        return Server::paused;
    if (snapshot.startingServer || snapshot.state == LiveSession::State::starting)
        return Server::starting;
    return snapshot.serverUrl.isEmpty() ? Server::none : Server::running;
}

void BrowserViews::followSwitch(const QUuid &frame, bool on)
{
    m_wasOn.insert(frame, on);
    // A page Generate made holds its dev server on a lease of this object's own: the switch freezes and wakes that too.
    if (const auto generated = m_generated.constFind(frame); generated != m_generated.constEnd() && generated->lease != 0)
        DevServers::shared().setPaused(generated->lease, !on);
    LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (!on) {
        m_serveWanted.remove(frame);
        // Frozen, not stopped: turning it on again wakes the same process.
        if (live && live->active(frame))
            live->setServerPaused(frame, true);
    } else if (live && live->active(frame)) {
        live->setServerPaused(frame, false);
    } else {
        m_serveWanted.insert(frame);
    }
    emit browserViewChanged(frame, on);
    emit frameChanged(frame);
}

void BrowserViews::serveWaiting()
{
    if (m_serveWanted.isEmpty() || !m_session.hasDocument())
        return;
    // Live waits for the tab, and the tab only opens where the frame can be seen.
    if (!onScreen())
        return;
    const QList<QUuid> waiting(m_serveWanted.begin(), m_serveWanted.end());
    for (const QUuid &frame : waiting) {
        const VectorObject *object = m_session.document()->find(frame);
        if (!object || !object->showsPage()) {
            m_serveWanted.remove(frame);
            continue;
        }
        // The address field is still asking for the page.
        if (object->browser->url.isEmpty())
            continue;
        m_serveWanted.remove(frame);
        const QUrl url = object->browser->url;
        // Somebody else's site, or a local server the user runs: there is nothing of ours to start.
        if (!ProjectRegistry::owns(url) || isLoopback(url))
            continue;
        LiveFrames *live = LiveFrames::of(m_session);
        connect(live, &LiveFrames::changed, this, &BrowserViews::onLiveChanged, Qt::UniqueConnection);
        if (live->active(frame)) {
            live->setServerPaused(frame, false);
            continue;
        }
        if (const QString failure = live->start(frame); !failure.isEmpty())
            emit notice(failure);
    }
}

void BrowserViews::stopServers()
{
    for (BrowserViews *views : everyone()) {
        const LiveFrames *live = views->m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
        if (!live)
            continue;
        // A frame still switched on starts again when it's next seen; one switched off stays off.
        for (const QUuid &frame : live->frames()) {
            if (views->browserViewOn(frame))
                views->m_serveWanted.insert(frame);
        }
    }
    // Every frame's Live lets its server go (the last lease stops it, frozen or not), and the headless browser stops.
    shutdownPool();
}
