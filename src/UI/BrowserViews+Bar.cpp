#include "Document/EditorSession.h"
#include "Live/Browser.h"
#include "Live/Registry.h"
#include "Canvas/EditorCanvas.h"
#include "UI/BrowserViews.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QSettings>

namespace {
constexpr auto signInKey = "browserView/signInOffered";
constexpr qint64 ownershipCheckMs = 2000;

bool &signingIn()
{
    static bool open = false;
    return open;
}

void windowClosed(QProcess *window)
{
    window->deleteLater();
    signingIn() = false;
    BrowserViews::setLiveOpen(false);
}
}

// The address bar's state ---------------------------------------------------------

BrowserViewHost::Bar BrowserViews::bar(const QUuid &frame) const
{
    const auto found = m_entries.constFind(frame);
    Bar bar;
    if (found == m_entries.constEnd())
        return bar;
    bar.loading = found->loading;
    bar.canGoBack = found->canGoBack;
    bar.canGoForward = found->canGoForward;
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (object && object->browser && !object->browser->url.isEmpty()) {
        // A cache written from a const call: it only spares the registry file a read on every paint.
        auto &entry = const_cast<Entry &>(*found);
        if (entry.ownedFor != object->browser->url || m_clock.elapsed() - entry.ownedAt > ownershipCheckMs) {
            entry.ownedFor = object->browser->url;
            entry.owned = ProjectRegistry::owns(object->browser->url);
            entry.ownedAt = m_clock.elapsed();
        }
        bar.notYours = !entry.owned;
    }
    return bar;
}

void BrowserViews::refreshHistory(const QUuid &frame)
{
    const auto found = m_entries.constFind(frame);
    if (found == m_entries.constEnd())
        return;
    const QUuid key = found->key;
    // The reply arrives on the pool's thread; the answer is taken to the main one by key.
    BrowserViews::pool()->call(key, QStringLiteral("Page.getNavigationHistory"), {}, [this, frame](const QJsonObject &result, const QString &error) {
        if (!error.isEmpty())
            return;
        const int index = result["currentIndex"].toInt();
        const QJsonArray entries = result["entries"].toArray();
        const int count = int(entries.size());
        // The tab's blank start is not a page to go back to.
        int first = 0;
        while (first < count && entries[first]["url"].toString() == QLatin1String("about:blank"))
            ++first;
        QMetaObject::invokeMethod(this, [this, frame, index, count, first] {
            const auto entry = m_entries.find(frame);
            if (entry == m_entries.end())
                return;
            const bool back = index > first;
            const bool forward = index + 1 < count;
            if (entry->canGoBack == back && entry->canGoForward == forward)
                return;
            entry->canGoBack = back;
            entry->canGoForward = forward;
            emit frameChanged(frame);
            scheduleRepaint(frame);
        }, Qt::QueuedConnection);
    });
}

void BrowserViews::act(const QUuid &frame, Action action)
{
    const auto found = m_entries.constFind(frame);
    if (found == m_entries.constEnd() || found->state != State::live)
        return;
    const QUuid key = found->key;
    switch (action) {
    case Action::reload:
    case Action::reloadIgnoringCache:
        BrowserViews::pool()->call(key, QStringLiteral("Page.reload"), {{"ignoreCache", action == Action::reloadIgnoringCache}});
        break;
    case Action::stop:
        BrowserViews::pool()->call(key, QStringLiteral("Page.stopLoading"), {});
        break;
    case Action::back:
    case Action::forward:
        BrowserViews::pool()->call(key, QStringLiteral("Page.getNavigationHistory"), {}, [key, action](const QJsonObject &result, const QString &error) {
            if (!error.isEmpty())
                return;
            const QJsonArray entries = result["entries"].toArray();
            const int target = result["currentIndex"].toInt() + (action == Action::back ? -1 : 1);
            if (target < 0 || target >= entries.size() || entries[target]["url"].toString() == QLatin1String("about:blank"))
                return;
            BrowserViews::pool()->call(key, QStringLiteral("Page.navigateToHistoryEntry"), {{"entryId", entries[target]["id"].toInt()}});
        });
        break;
    }
}

// Signing in -------------------------------------------------------------------------

bool BrowserViews::signInAnswered()
{
    return QSettings().value(QLatin1String(signInKey), false).toBool();
}

void BrowserViews::setSignInAnswered(bool answered)
{
    QSettings().setValue(QLatin1String(signInKey), answered);
}

bool BrowserViews::isSigningIn()
{
    return signingIn();
}

bool BrowserViews::signInOffered() const
{
    return !signInAnswered() && !isSigningIn() && !Browser::executable().isEmpty();
}

void BrowserViews::dismissSignIn()
{
    setSignInAnswered(true);
    if (m_canvas)
        m_canvas->update();
}

void BrowserViews::signIn()
{
    setSignInAnswered(true);
    if (isSigningIn() || Browser::executable().isEmpty())
        return;
    // A profile opens in one process at a time: the headless browser stops before the window starts.
    setLiveOpen(true);
    const BrowserPool::Options &options = poolSettings();
    const QString profile = options.profile.isEmpty() ? Browser::defaultProfile() : options.profile;
    QDir().mkpath(profile);
    Browser::Options windowed;
    windowed.cache = options.cache;
    windowed.profile = profile;
    QStringList arguments = Browser::chromiumArguments(windowed, profile);
    // The window is the user's to use; nothing here drives it.
    arguments.removeIf([](const QString &argument) { return argument.startsWith(QLatin1String("--remote-debugging")); });
    arguments << QStringLiteral("about:blank");
    auto *window = new QProcess(qApp);
    signingIn() = true;
    connect(window, &QProcess::finished, qApp, [window] { windowClosed(window); });
    connect(window, &QProcess::errorOccurred, qApp, [window](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            windowClosed(window);
    });
    window->setStandardOutputFile(QProcess::nullDevice());
    window->setStandardErrorFile(QProcess::nullDevice());
    window->start(Browser::executable(), arguments);
    if (m_canvas)
        m_canvas->update();
}
