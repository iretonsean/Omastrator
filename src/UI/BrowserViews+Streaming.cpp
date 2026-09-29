#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>

// The pool's events, the decode worker and the canvas's repaints (docs/BROWSER-VIEW.md, section 3).

void BrowserViews::onOpened(const QUuid &key)
{
    const QUuid frame = frameOf(key);
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    // A new tab is blank and awake, whatever the last one was doing.
    found->casting = false;
    found->frozen = false;
    found->applied = {};
    found->navigated = QUrl();
    note(frame, State::paused);
    scheduleReconcile();
}

void BrowserViews::onOpenFailed(const QUuid &key, const QString &error)
{
    const QUuid frame = frameOf(key);
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    found->failure = error;
    note(frame, State::failed);
}

void BrowserViews::onClosed(const QUuid &key, BrowserPool::CloseReason reason)
{
    const QUuid frame = frameOf(key);
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    Entry &entry = *found;
    entry.casting = false;
    entry.frozen = false;
    entry.applied = {};
    entry.navigated = QUrl();
    entry.loading = false;
    entry.decoding = false;
    entry.pendingAck = -1;
    savePicture(frame, entry);
    if (entry.state == State::resetPaused || entry.state == State::liveOpen)
        return;
    switch (reason) {
    case BrowserPool::CloseReason::reset:
        note(frame, State::resetPaused);
        return;
    case BrowserPool::CloseReason::evicted:
        entry.evicted = true;
        break;
    case BrowserPool::CloseReason::lost:
        ++entry.lost;
        break;
    case BrowserPool::CloseReason::closed:
        break;
    }
    note(frame, State::closed);
    scheduleReconcile();
}

void BrowserViews::onTabEvent(const QUuid &key, const QString &method, const QJsonObject &params)
{
    const QUuid frame = frameOf(key);
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    Entry &entry = *found;
    if (method == QLatin1String("Page.screencastFrame")) {
        onScreencastFrame(entry, frame, params);
    } else if (method == QLatin1String("Page.frameNavigated")) {
        const QJsonObject page = params["frame"].toObject();
        if (!page["parentId"].toString().isEmpty())
            return;
        entry.mainFrame = page["id"].toString();
        const QUrl url(page["url"].toString());
        if (url.isEmpty() || url.toString() == QLatin1String("about:blank"))
            return;
        const VectorObject *object = m_session.document()->find(frame);
        // The address the user gave stays as it was typed when the page merely confirmed it.
        if (object && object->browser && !sameAddress(object->browser->url, url)) {
            entry.navigated = url;
            entry.scroll = {};
            m_session.setBrowserLocation(frame, url, {});
        }
        entry.loading = true;
        emit frameChanged(frame);
    } else if (method == QLatin1String("Page.navigatedWithinDocument")) {
        const QUrl url(params["url"].toString());
        const VectorObject *object = m_session.document()->find(frame);
        if (params["frameId"].toString() == entry.mainFrame && object && object->browser && !sameAddress(object->browser->url, url)) {
            entry.navigated = url;
            m_session.setBrowserLocation(frame, url, entry.scroll);
            emit frameChanged(frame);
        }
    } else if (method == QLatin1String("Page.frameStartedLoading")) {
        entry.loading = true;
        emit frameChanged(frame);
    } else if (method == QLatin1String("Page.loadEventFired")) {
        entry.loading = false;
        if (entry.restoreScroll) {
            entry.restoreScroll = false;
            call(entry, QStringLiteral("Runtime.evaluate"),
                 {{"expression", QStringLiteral("window.scrollTo(%1, %2)").arg(entry.scroll.x()).arg(entry.scroll.y())}});
        }
        emit frameChanged(frame);
    }
}

void BrowserViews::onScreencastFrame(Entry &entry, const QUuid &frame, const QJsonObject &params)
{
    const QJsonObject metadata = params["metadata"].toObject();
    if (!entry.restoreScroll)
        entry.scroll = QPointF(metadata["scrollOffsetX"].toDouble(), metadata["scrollOffsetY"].toDouble());
    const int ack = params["sessionId"].toInt();
    const QByteArray data = params["data"].toString().toLatin1();
    if (entry.decoding) {
        // The older waiting frame is acknowledged unseen, so Chromium goes on sending.
        if (entry.pendingAck >= 0)
            call(entry, QStringLiteral("Page.screencastFrameAck"), {{"sessionId", entry.pendingAck}});
        entry.pendingData = data;
        entry.pendingAck = ack;
        return;
    }
    entry.decoding = true;
    entry.pendingData = data;
    entry.pendingAck = ack;
    decodeNext(frame);
}

void BrowserViews::decodeNext(const QUuid &frame)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end() || found->pendingAck < 0)
        return;
    const QByteArray data = std::exchange(found->pendingData, {});
    const int ack = std::exchange(found->pendingAck, -1);
    found->decoding = true;
    m_decoder.start([this, frame, data, ack] {
        const QImage image = QImage::fromData(QByteArray::fromBase64(data), "JPEG");
        QMetaObject::invokeMethod(this, [this, frame, image, ack] { decoded(frame, image, ack); }, Qt::QueuedConnection);
    });
}

void BrowserViews::decoded(const QUuid &frame, const QImage &image, int ack)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return;
    Entry &entry = *found;
    entry.decoding = false;
    if (!image.isNull()) {
        entry.image = image;
        entry.imageSaved = false;
        entry.lost = 0;
        scheduleRepaint(frame);
    }
    // The ack is the back-pressure: the next frame comes once this one has been turned into pixels.
    if (entry.state == State::live || entry.state == State::paused)
        call(entry, QStringLiteral("Page.screencastFrameAck"), {{"sessionId", ack}});
    if (entry.pendingAck >= 0)
        decodeNext(frame);
}

void BrowserViews::scheduleRepaint(const QUuid &frame)
{
    if (!m_canvas || !m_session.hasDocument() || !m_session.document()->find(frame))
        return;
    m_dirty = m_dirty.united(m_canvas->documentToView().mapRect(m_session.document()->bounds(frame)));
    if (!m_repaint.isActive())
        m_repaint.start();
}

void BrowserViews::savePicture(const QUuid &frame, Entry &entry)
{
    if (entry.image.isNull() || entry.imageSaved || !m_session.hasDocument())
        return;
    m_session.setBrowserPicture(frame, entry.image);
    entry.imageSaved = true;
}

void BrowserViews::flushLocations()
{
    if (!m_session.hasDocument())
        return;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        const VectorObject *object = m_session.document()->find(it.key());
        if (!object || !object->browser)
            continue;
        // The saved scroll waits until the page has been put back to it.
        if (!it->restoreScroll && it->casting && object->browser->scroll != it->scroll)
            m_session.setBrowserLocation(it.key(), object->browser->url, it->scroll);
        savePicture(it.key(), *it);
    }
}

void BrowserViews::flushPictures()
{
    flushLocations();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        savePicture(it.key(), *it);
}
