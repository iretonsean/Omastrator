#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include <QJsonArray>

// What a page may not do (docs/BROWSER-VIEW.md, section 5): it is drawn on a canvas, so it can't take the screen, the
// pointer, a dialog, a file or a window of its own.

namespace {
// Fullscreen and pointer lock have no window to fill; asking for them fails as it does in a background tab.
constexpr auto blockScript = R"js(
(() => {
  const refuse = () => Promise.reject(new DOMException('Not allowed in Browser View', 'NotAllowedError'));
  for (const name of ['requestFullscreen', 'webkitRequestFullscreen', 'webkitRequestFullScreen'])
    if (Element.prototype[name]) Element.prototype[name] = refuse;
  if (Element.prototype.requestPointerLock) Element.prototype.requestPointerLock = function () {};
})();
)js";

// The permissions a canvas has no way to ask about.
const char *const deniedPermissions[] = {"geolocation", "notifications", "midi", "midiSysex", "audioCapture", "videoCapture", "displayCapture",
                                         "clipboardReadWrite", "idleDetection", "wakeLockScreen", "wakeLockSystem", "sensors", "backgroundSync",
                                         "automaticFullscreen", "storageAccess", "windowManagement", "localFonts", "paymentHandler"};
}

void BrowserViews::limitPage(const Entry &entry)
{
    BrowserPool *pool = BrowserViews::pool();
    call(entry, QStringLiteral("Page.setInterceptFileChooserDialog"), {{"enabled", true}});
    call(entry, QStringLiteral("Page.addScriptToEvaluateOnNewDocument"), {{"source", QString::fromLatin1(blockScript)}});
    // The browser's own session: a permission is denied for every tab, errors (a name this Chromium lacks) ignored.
    for (const char *name : deniedPermissions)
        pool->call(QUuid(), QStringLiteral("Browser.setPermission"), {{"permission", QJsonObject{{"name", QString::fromLatin1(name)}}}, {"setting", "denied"}});
}

void BrowserViews::onPageLimit(Entry &entry, const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String("Page.javascriptDialogOpening")) {
        // A leave-page prompt is let through so the page can go; the rest are dismissed.
        const bool leaving = params["type"].toString() == QLatin1String("beforeunload");
        call(entry, QStringLiteral("Page.handleJavaScriptDialog"), {{"accept", leaving}});
        const QString message = params["message"].toString().simplified();
        if (!leaving && !message.isEmpty())
            emit notice(QStringLiteral("%1 says: %2").arg(QUrl(params["url"].toString()).host(), message.left(160)));
    } else if (method == QLatin1String("Page.fileChooserOpened")) {
        emit notice(QStringLiteral("Uploads aren't supported in Browser View yet."));
    }
}

void BrowserViews::onPopup(const QUuid &key, const QUrl &url)
{
    const auto found = m_entries.find(frameOf(key));
    if (found == m_entries.end() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return;
    // The page goes where it was sending the window; the address it lands on is written back as any navigation is.
    call(*found, QStringLiteral("Page.navigate"), {{"url", url.toString(QUrl::FullyEncoded)}});
}

bool BrowserViews::dispatch(const QUuid &frame, const QString &method, const QJsonObject &params)
{
    const auto found = m_entries.find(frame);
    if (found == m_entries.end())
        return false;
    if (found->state == State::resetPaused) {
        resume(frame);
        return false;
    }
    if (found->state != State::live && found->state != State::paused)
        return false;
    if (!m_browsed.contains(frame) && m_session.tool() == Tool::browse) {
        m_browsed.insert(frame);
        scheduleReconcile();
    }
    BrowserViews::pool()->call(found->key, method, params);
    return true;
}
