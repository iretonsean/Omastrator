#pragma once
#include "Live/Browser.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <atomic>
#include <functional>

// The one headless Chromium behind every Browser View (docs/BROWSER-VIEW.md): a tab per frame, started when the first
// frame needs one and stopped 60 s after the last closes. The pool, its Browser and the CDP socket live on their own
// thread, so a slow page never stalls the canvas. The public calls are safe from any thread and queue onto it; the
// signals are emitted on it, so connect with a receiver on another thread for a queued delivery.
class BrowserPool : public QObject {
    Q_OBJECT
public:
    struct Options {
        // Default: Browser::defaultProfile().
        QString profile;
        Browser::Cache cache = Browser::Cache::capped;
        int idleMs = 60'000;
        int maxTabs = 8;
        // Tests write the state file too, in their temporary runtime directory.
        bool writeState = true;
    };
    enum class CloseReason { closed, evicted, reset, lost };
    Q_ENUM(CloseReason)
    // Runs on the pool's thread.
    using Reply = CdpConnection::Reply;

    explicit BrowserPool(const Options &options, QObject *parent = nullptr);
    ~BrowserPool() override;

    // A tab for `frame`, in the default browser context or in `context` (Target.createBrowserContext's id). The tab
    // starts shown. Opening a frame that has a tab does nothing.
    void open(const QUuid &frame, const QString &context = {});
    void close(const QUuid &frame);
    // The escape hatch: every tab closes and the browser stops, and nothing restarts by itself.
    // With `wait`, it returns once they have: Live needs the profile free before it starts its own Chromium.
    void closeAll(bool wait = false);
    // Which tabs are on screen; the paused one shown least recently is the first to go past the cap.
    void setShown(const QUuid &frame, bool shown);
    // A command in a frame's tab, or the browser's own when `frame` is null. `reply` runs on the pool's thread.
    void call(const QUuid &frame, const QString &method, const QJsonObject &params, Reply reply = {});
    // Runs `work` on the pool's thread, where session() and cdp() may be used.
    void run(std::function<void()> work);

    // Any thread.
    bool isRunning() const { return m_running; }
    int tabCount() const { return m_tabCount; }
    QThread *poolThread() const { return m_thread; }
    // The state file's browser, and the Chromium it says is running: for tests and reset.
    qint64 processId() const { return m_processId; }

    // The pool's thread only.
    struct Session {
        QString targetId;
        QString sessionId;
    };
    Session session(const QUuid &frame) const;
    CdpConnection *cdp() { return m_browser ? &m_browser->cdp() : nullptr; }

signals:
    // The tab is attached and Page is enabled; commands may go.
    void opened(const QUuid &frame);
    void openFailed(const QUuid &frame, const QString &error);
    void closed(const QUuid &frame, BrowserPool::CloseReason reason);
    // A protocol event from a frame's tab.
    void tabEvent(const QUuid &frame, const QString &method, const QJsonObject &params);
    // A page opened a window (target=_blank, window.open): its tab is closed and this is where it was going. The
    // frame that opened it should go there instead.
    void popup(const QUuid &frame, const QUrl &url);
    void started();
    void stopped();

private:
    struct Tab {
        QString targetId;
        QString sessionId;
        QString context;
        bool shown = true;
        // Counts up so the order is exact.
        qint64 lastShown = 0;
    };
    struct Pending {
        QUuid frame;
        QString context;
    };

    void doOpen(const QUuid &frame, const QString &context);
    void makeRoom();
    void openNextWaiting();
    void finishOpen(const QUuid &frame, const QString &context);
    void doClose(const QUuid &frame, CloseReason reason);
    void doCloseAll(CloseReason reason);
    bool shutDown();
    void stopBrowser(bool later = false);
    void lostBrowser();
    void noteTabs();
    QUuid frameOfSession(const QString &sessionId) const;
    void onEvent(const QString &method, const QJsonObject &params, const QString &sessionId);
    void onTargetInfo(const QJsonObject &info);
    // Popup targets seen and not yet closed, with the frame that opened each.
    QHash<QString, QUuid> m_popups;

    Options m_options;
    QThread *m_thread = nullptr;
    QThread *m_owner = nullptr;
    Browser *m_browser = nullptr;
    QTimer *m_idle = nullptr;
    QHash<QUuid, Tab> m_tabs;
    // Opens asked for while the browser is starting or a tab is being made.
    QList<Pending> m_pending;
    QList<QUuid> m_opening;
    bool m_starting = false;
    bool m_shutDown = false;
    bool m_closeRequested = false;
    CloseReason m_closeReason = CloseReason::reset;
    qint64 m_shownCounter = 0;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_tabCount{0};
    std::atomic<qint64> m_processId{0};
};
