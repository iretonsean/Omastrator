#pragma once
#include "Canvas/BrowserViewHost.h"
#include "Live/BrowserPool.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QThreadPool>
#include <QTimer>
#include <QWindow>
#include <QUrl>
#include <QUuid>

class EditorCanvas;
struct VectorObject;
class EditorSession;

// Streams the pages behind a document's Browser Views (docs/BROWSER-VIEW.md, sections 2 and 3): one tab per frame in the
// shared BrowserPool, opened while the frame is on screen, paused when it isn't. It implements the canvas's host, so the
// canvas draws the newest picture and the messages, and it writes the page's address and scroll back to the document.
class BrowserViews : public QObject, public BrowserViewHost {
    Q_OBJECT
public:
    // One per session; made on first use and owned by it.
    static BrowserViews *of(EditorSession &session);
    explicit BrowserViews(EditorSession &session);
    ~BrowserViews() override;

    // The canvas the frames are seen on; without one every frame is paused.
    void attach(EditorCanvas *canvas);
    void detach(EditorCanvas *canvas);

    enum class State { closed, opening, live, paused, failed, unavailable, resetPaused, liveOpen };
    State state(const QUuid &frame) const;
    // A frame that reset paused streams again.
    void resume(const QUuid &frame);
    // The newest picture and address go into the document now, as before a save or an export.
    void flushPictures();
    // A streamed picture as it arrives; it goes into the document at the next flush. Tests use it in place of a page.
    void notePicture(const QUuid &frame, const QImage &image);

    // The pool's name for a frame's tab; tests speak to the pool's signals with it.
    QUuid poolKey(const QUuid &frame) const;

    QImage picture(const QUuid &frame) const override;
    QString message(const QUuid &frame) const override;
    Bar bar(const QUuid &frame) const override;
    void act(const QUuid &frame, Action action) override;
    bool signInOffered() const override;
    void signIn() override;
    void dismissSignIn() override;
    bool dispatch(const QUuid &frame, const QString &method, const QJsonObject &params) override;
    QList<int> breakpoints(const QUuid &frame) const override;
    // Whether the strip has been answered on this machine; tests clear it.
    static bool signInAnswered();
    static void setSignInAnswered(bool answered);
    // Omastrator's browser is open in a normal window on the pool's profile for the user to sign in.
    static bool isSigningIn();
    // The sign-in window holds the profile too, apart from Live: closing one must not free it for the other.
    static void setSignInWindow(bool open);
    // Holds the profile for a sign-in window an earlier Omastrator left open (read from Chromium's SingletonLock).
    static bool adoptSignInWindow();

    // A frame paused this long has its tab closed, so a hidden window doesn't hold Chromium up for good; showing it
    // again reopens the tab. Tests shorten it.
    static void setPausedCloseMs(int ms);

    // The pool every session shares. Tests give it their own profile and no idle wait; the app uses the defaults.
    static void setPoolOptions(const BrowserPool::Options &options);
    static BrowserPool *pool();
    static const BrowserPool::Options &poolSettings();
    static void watchSignInWindow(qint64 pid);
    static void shutdownPool();
    // The escape hatch: every frame everywhere pauses, the tabs close and the browser stops, and nothing restarts by itself.
    static void resetAll();
    // Live is in Omastrator's own window on the same profile, so frames wait until it closes.
    static void setLiveOpen(bool open);
    static bool liveWindowIsOpen();

signals:
    // A frame's state, address or loading changed.
    void frameChanged(const QUuid &frame);
    // Something the page tried that Browser View refuses, said once to the user.
    void notice(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static void profileHoldChanged();
    struct Applied {
        QSize css;
        int scale = 0;
        QSize cast;
    };
    // A wanted value that keeps changing (a zoom, a pinch) waits until it has held still.
    struct Settling {
        QSize cast;
        int scale = 0;
        qint64 castSince = 0;
        qint64 scaleSince = 0;
    };
    struct Entry {
        QUuid key;
        State state = State::closed;
        // What the tab is showing, so a document address that differs is a navigation.
        QUrl navigated;
        Applied applied;
        Settling settling;
        QString mainFrame;
        bool casting = false;
        bool frozen = false;
        // When it went paused (m_clock), so a long pause can close the tab.
        qint64 pausedAt = 0;
        bool shown = false;
        bool loading = false;
        bool canGoBack = false;
        bool canGoForward = false;
        // The registry is read at most every couple of seconds, not on every paint.
        QUrl ownedFor;
        bool owned = false;
        qint64 ownedAt = -100'000;
        // Evicted for the cap: not reopened until the frame has been hidden and shown again.
        bool evicted = false;
        bool restoreScroll = false;
        QImage image;
        bool imageSaved = true;
        QPointF scroll;
        int lost = 0;
        QString failure;
        // One decode at a time; the newest frame waits, the older ones are acknowledged unseen.
        bool decoding = false;
        // Chromium sends every frame (its every-Nth counts frames, so it can skip the one change a page made); the gap is ours.
        int frameGap = 0;
        qint64 decodedAt = -100'000;
        bool holding = false;
        QByteArray pendingData;
        int pendingAck = -1;
    };
    struct Want {
        bool shown = false;
        QRectF view;
        QSize css;
        int scale = 1;
        QSize cast;
        // The least time between pictures: none for the selected or browsed frame, more for the rest.
        int frameGap = 0;
    };

    // The pool's side.
    void connectPool();
    void onOpened(const QUuid &key);
    void onOpenFailed(const QUuid &key, const QString &error);
    void onClosed(const QUuid &key, BrowserPool::CloseReason reason);
    void onTabEvent(const QUuid &key, const QString &method, const QJsonObject &params);
    void onScreencastFrame(Entry &entry, const QUuid &frame, const QJsonObject &params);
    void refreshHistory(const QUuid &frame);
    // Whether the frame's page is one of the user's own sites, read from the registry at most every couple of seconds.
    bool owned(const QUuid &frame) const;
    void scanBreakpoints(const QUuid &frame);
    // Page limits (BrowserViews+Limits.cpp).
    void limitPage(const Entry &entry);
    void onPageLimit(Entry &entry, const QString &method, const QJsonObject &params);
    void onPopup(const QUuid &key, const QUrl &url);
    void takeFrame(const QUuid &frame);
    void decodeNext(const QUuid &frame);
    void decoded(const QUuid &frame, const QImage &image, int ack);

    // Decides what each frame should be doing, and tells the tabs.
    void scheduleReconcile();
    // Whether the canvas can be seen: shown, and its window exposed (not minimized or on another workspace).
    bool onScreen() const;
    void watchWindow();
    void closeLongPaused();
    void reconcile();
    Want wanted(const QUuid &frame, const VectorObject &object, int streaming) const;
    void sync(const QUuid &frame, Entry &entry, const Want &want);
    void pause(const QUuid &frame, Entry &entry);
    void dropEntry(const QUuid &frame);
    void savePicture(const QUuid &frame, Entry &entry);
    void flushLocations();
    void scheduleRepaint(const QUuid &frame);
    void forgetTabs();
    static bool sameAddress(const QUrl &a, const QUrl &b);
    void call(const Entry &entry, const QString &method, const QJsonObject &params = {});
    QUuid frameOf(const QUuid &key) const;
    void note(const QUuid &frame, State state);

    EditorSession &m_session;
    QPointer<EditorCanvas> m_canvas;
    QUuid m_scope = QUuid::createUuid();
    QHash<QUuid, Entry> m_entries;
    QHash<QUuid, QUuid> m_frameOfKey;
    // Each own site's breakpoints, by origin, read again on every load.
    QHash<QString, QList<int>> m_breakpoints;
    QPointer<BrowserPool> m_connectedPool;
    QThreadPool m_decoder;
    QTimer m_reconcile;
    QTimer m_settle;
    QTimer m_repaint;
    QTimer m_flush;
    QTimer m_pausedClose;
    QPointer<QWindow> m_watchedWindow;
    QRectF m_dirty;
    std::vector<QUuid> m_lastSelection;
    // Frames the Browse tool has sent input to; they stream every frame.
    QSet<QUuid> m_browsed;
    QElapsedTimer m_clock;
};
