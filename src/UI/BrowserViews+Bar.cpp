#include "Agent/BrowserPoolState.h"
#include "Document/EditorSession.h"
#include "Live/Browser.h"
#include "Live/Registry.h"
#include "UI/AgentBridge.h"
#include "Canvas/EditorCanvas.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QSettings>

namespace {
constexpr auto signInKey = "browserView/signInOffered";
constexpr qint64 ownershipCheckMs = 2000;

// A process's start time in ticks, or 0 once it is gone or a zombie; it tells a pid's reuse from the same process.
quint64 startTicks(qint64 pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = stat.readAll();
    // The name is in parentheses and may hold spaces, so the fields are counted after the last ")".
    const QList<QByteArray> fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
    if (fields.size() < 20 || fields.at(0) == "Z" || fields.at(0) == "X")
        return 0;
    return fields.at(19).toULongLong();
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
    if (object && object->browser && !object->browser->url.isEmpty())
        bar.notYours = !owned(frame);
    if (const LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly); live && live->active(frame)) {
        const LiveFrames::Snapshot snapshot = live->snapshot(frame);
        bar.dev = !snapshot.serverUrl.isEmpty() && m_swaps.contains(frame);
        if (bar.dev)
            bar.devTip = snapshot.serverCommand.isEmpty() ? snapshot.serverUrl.toString()
                                                          : QStringLiteral("%1\n%2").arg(snapshot.serverUrl.toString(), snapshot.serverCommand);
    }
    fillDeploy(frame, bar);
    fillBuild(frame, bar);
    return bar;
}

bool BrowserViews::owned(const QUuid &frame) const
{
    const auto found = m_entries.constFind(frame);
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (found == m_entries.constEnd() || !object || !object->browser || object->browser->url.isEmpty())
        return false;
    // A cache written from a const call: it only spares the registry file a read on every paint.
    auto &entry = const_cast<Entry &>(*found);
    if (entry.ownedFor != object->browser->url || m_clock.elapsed() - entry.ownedAt > ownershipCheckMs) {
        entry.ownedFor = object->browser->url;
        entry.owned = ProjectRegistry::owns(object->browser->url);
        entry.ownedAt = m_clock.elapsed();
    }
    return entry.owned;
}

void BrowserViews::refreshHistory(const QUuid &frame)
{
    const auto found = m_entries.constFind(frame);
    if (found == m_entries.constEnd())
        return;
    const QUuid key = found->key;
    // The reply arrives on the pool's thread, possibly after this is gone: it is taken to the main thread through a guard.
    const QPointer<BrowserViews> guard(this);
    BrowserViews::pool()->call(key, QStringLiteral("Page.getNavigationHistory"), {}, [guard, frame](const QJsonObject &result, const QString &error) {
        if (!error.isEmpty())
            return;
        const int index = result["currentIndex"].toInt();
        const QJsonArray entries = result["entries"].toArray();
        const int count = int(entries.size());
        // The tab's blank start is not a page to go back to.
        int first = 0;
        while (first < count && entries[first]["url"].toString() == QLatin1String("about:blank"))
            ++first;
        QMetaObject::invokeMethod(qApp, [guard, frame, index, count, first] {
            if (!guard)
                return;
            const auto entry = guard->m_entries.find(frame);
            if (entry == guard->m_entries.end())
                return;
            const bool back = index > first;
            const bool forward = index + 1 < count;
            if (entry->canGoBack == back && entry->canGoForward == forward)
                return;
            entry->canGoBack = back;
            entry->canGoForward = forward;
            emit guard->frameChanged(frame);
            guard->scheduleRepaint(frame);
        }, Qt::QueuedConnection);
    });
}

void BrowserViews::act(const QUuid &frame, Action action)
{
    if (action == Action::thisIsMySite) {
        chooseMySite(frame);
        return;
    }
    if (action >= Action::buildButton) {
        runBuildAction(frame, action);
        return;
    }
    if (action >= Action::deployButton) {
        if (m_agent)
            m_agent->clearBuilt(frame);
        runProjectAction(frame, action);
        return;
    }
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
    default:
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

bool BrowserViews::signInOffered() const
{
    return !signInAnswered() && !isSigningIn() && !liveWindowIsOpen() && !Browser::executable().isEmpty();
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
    if (isSigningIn() || liveWindowIsOpen() || Browser::executable().isEmpty())
        return;
    // A profile opens in one process at a time: the headless browser stops before the window starts.
    setSignInWindow(true);
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
    // Detached, so quitting Omastrator doesn't close the window the user may be signing in on; its pid is watched.
    QProcess window;
    window.setProgram(Browser::executable());
    window.setArguments(arguments);
    window.setStandardOutputFile(QProcess::nullDevice());
    window.setStandardErrorFile(QProcess::nullDevice());
    qint64 pid = 0;
    if (!window.startDetached(&pid)) {
        setSignInWindow(false);
    } else {
        watchSignInWindow(pid);
    }
    if (m_canvas)
        m_canvas->update();
}

void BrowserViews::watchSignInWindow(qint64 pid)
{
    const quint64 started = startTicks(pid);
    auto *watch = new QTimer(qApp);
    watch->setInterval(500);
    connect(watch, &QTimer::timeout, qApp, [watch, pid, started] {
        if (started != 0 && startTicks(pid) == started)
            return;
        watch->deleteLater();
        setSignInWindow(false);
    });
    watch->start();
}

bool BrowserViews::adoptSignInWindow()
{
    // A sign-in window outlives the Omastrator that opened it; a new one learns of it from Chromium's profile lock.
    const BrowserPool::Options &options = poolSettings();
    const QString profile = options.profile.isEmpty() ? Browser::defaultProfile() : options.profile;
    const QString lock = QFile::symLinkTarget(QDir(profile).filePath(QStringLiteral("SingletonLock")));
    const qsizetype dash = lock.lastIndexOf(QLatin1Char('-'));
    bool isPid = false;
    const qint64 pid = dash < 0 ? 0 : lock.mid(dash + 1).toLongLong(&isPid);
    if (!isPid || pid <= 0 || isSigningIn() || !BrowserPoolState::namesProfile(pid, profile))
        return false;
    setSignInWindow(true);
    watchSignInWindow(pid);
    return true;
}
