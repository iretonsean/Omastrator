#pragma once
#include "Canvas/BrowserViewHost.h"
#include "Live/BrowserPool.h"
#include "Live/PageTemplates.h"
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QThreadPool>
#include <QTimer>
#include <QWindow>
#include <functional>
#include <memory>
#include <QUrl>
#include <QUuid>

class EditorCanvas;
class QMenu;
class AgentBridge;
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
    // The timeline is open on this frame: it streams every picture, as a browsed frame does (docs/MOTION.md, section 2).
    void setScrubbed(const QUuid &frame, bool scrubbed);

    // Record (docs/MOTION.md, section 8): the frame's page as it paints now, as JPEG bytes. It waits for two animation frames (or
    // a quarter of a second, for a page nobody is looking at), so a seek has painted. The size is the frame's on screen, but at most
    // `longSide` px on the longer side. `done` gets the bytes and the page's CSS size, or why it couldn't, on this thread.
    using CaptureDone = std::function<void(const QByteArray &jpeg, const QSizeF &css, const QString &error)>;
    void capture(const QUuid &frame, int longSide, CaptureDone done);

    // The pool's name for a frame's tab; tests speak to the pool's signals with it.
    QUuid poolKey(const QUuid &frame) const;

    QImage picture(const QUuid &frame) const override;
    QString message(const QUuid &frame) const override;
    Bar bar(const QUuid &frame) const override;
    void act(const QUuid &frame, Action action) override;
    Empty empty(const QUuid &frame) const override;
    bool signInOffered() const override;
    void signIn() override;
    void dismissSignIn() override;
    bool dispatch(const QUuid &frame, const QString &method, const QJsonObject &params) override;
    void extendBarMenu(const QUuid &frame, QMenu *menu) override;
    void browserViewSwitched(const QUuid &frame, bool on) override;
    // The Browser View switch (BrowserViews+Switch.cpp), for the menu, Ctrl+K and the island: one undo step in the document,
    // then on starts the project's dev server (or wakes the frozen one) and streams; off freezes the server and keeps the
    // last picture. Undo and redo follow the same way, so a server is never started or left behind by history.
    void setBrowserViewOn(const QUuid &frame, bool on);
    bool browserViewOn(const QUuid &frame) const;
    // What runs behind a frame whose switch is on, for the island's Live controls.
    enum class Server { none, starting, running, paused, failed };
    Server server(const QUuid &frame) const;
    // The app's last window closed while it keeps running in the background: every frame's dev server stops, as on quit.
    // A frame still switched on starts its server again when its canvas is next seen.
    static void stopServers();
    // The window's bridge, which Deploy, Save, Review Changes and History go through (BrowserViews+Deploy.cpp). Set by
    // the menus when the session's canvas is in front.
    void setAgent(AgentBridge *agent);
    QList<int> breakpoints(const QUuid &frame) const override;
    QString beginEditPage(const QUuid &frame) override;
    void endEditPage(const QUuid &frame) override;
    EditBoxes editBoxes(const QUuid &frame) const override;
    ElementState elementState(const QUuid &frame) const override;
    QString editElements(const QUuid &frame, const QStringList &properties, const QString &value, bool preview) override;
    QString editElementText(const QUuid &frame, const QString &selector, const QString &text) override;
    bool canUndoPageEdit(const QUuid &frame) const override;
    bool canRedoPageEdit(const QUuid &frame) const override;
    void undoPageEdit(const QUuid &frame) override;
    void redoPageEdit(const QUuid &frame) override;
    // Animate (docs/MOTION.md, section 4): asks the agent to write motion for the elements picked on the frame, and previews it
    // from a worktree before anything is saved. Returns why it can't ask; the rest reports through `notice`.
    QString animate(const QUuid &frame, const QString &instruction, bool reducedMotion = true);
    // A second Animate while a preview is open asks; the answer is Keep (nothing starts), Discard (the preview goes, the ask
    // starts) or Cancel (nothing starts). Tests answer it in place of the dialog.
    enum class PreviewAnswer { keep, discard, cancel };
    using PreviewChooser = std::function<PreviewAnswer()>;
    static void setPreviewChooser(PreviewChooser chooser);
    // The frame shows the agent's motion from its worktree. Save to code writes it; Discard drops it.
    bool previewing(const QUuid &frame) const { return m_previewed.contains(frame); }
    QString savePreview(const QUuid &frame);
    void discardPreview(const QUuid &frame);
    AgentBridge *agent() const;
    // The folder of a page Generate a page made, while its frame still shows the dev server it was given; empty otherwise.
    QString generatedProject(const QUuid &frame) const;
    // Live runs the frame's project from its dev server: the tab shows the document's address on `server`, and the
    // document keeps the production address. An empty `server` puts the tab back on the production page.
    void useDevServer(const QUuid &frame, const QUrl &server);
    // Answers This Is My Site…'s folder question in place of its dialog: the page's address in, the folder out (empty
    // cancels). Tests set it; an empty function puts the dialog back.
    using FolderChooser = std::function<QString(const QUrl &page)>;
    static void setFolderChooser(FolderChooser chooser);
    // Answers Build It with a Note…'s question in place of its dialog (empty cancels). Tests set it.
    using NoteChooser = std::function<QString()>;
    static void setNoteChooser(NoteChooser chooser);
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
    // A frame began or stopped showing the agent's motion as a preview.
    void previewStateChanged(const QUuid &frame);
    // Something the page tried that Browser View refuses, said once to the user.
    void notice(const QString &text);
    // A frame's Browser View switch went on or off, by any door (the switch, a command, undo, an agent, deleting it). The
    // island shows Live controls for a selected frame that is on.
    void browserViewChanged(const QUuid &frame, bool on);
    // A new picture of the frame's page arrived (the timeline paces its seeks by it).
    void pictureArrived(const QUuid &frame);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static void profileHoldChanged();
    // Every session's controller.
    static QList<BrowserViews *> everyone();
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
        // The page size, in CSS px, that the waiting frame shows.
        QSizeF pendingCss;
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

    // The switch's servers (BrowserViews+Switch.cpp): follow a change of the document's switch, and start what waits.
    void followSwitch(const QUuid &frame, bool on);
    void serveWaiting();
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
    // A frame's Live changed: its boxes repaint, and Edit Page ends when its Live has.
    void onLiveChanged(const QUuid &frame);
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
    // A document address as the tab shows it, and a tab address as the document keeps it (they differ on the dev server).
    QUrl toTabUrl(const QUuid &frame, const QUrl &document) const;
    QUrl toDocumentUrl(const QUuid &frame, const QUrl &tab) const;
    // A navigation to somewhere other than the dev server ends a retired swap.
    void settleSwap(const QUuid &frame, const QUrl &tab);
    // The bar menu's items for a site that isn't the user's, and This Is My Site… (BrowserViews+Site.cpp).
    void addSiteActions(const QUuid &frame, QMenu *menu);
    void chooseMySite(const QUuid &frame);
    void runSiteAction(const QUuid &frame, Action action);
    void fillEditSets(const QUuid &frame, QMenu *menu);
    // The frame's own-site project, or empty (BrowserViews+Deploy.cpp).
    QString projectOf(const QUuid &frame) const;
    void fillDeploy(const QUuid &frame, Bar &bar) const;
    void runProjectAction(const QUuid &frame, Action action);
    void addProjectActions(const QUuid &frame, QMenu *menu);
    // Build It (BrowserViews+Build.cpp): the bar's button, its actions, and the package handed to the agent.
    void fillBuild(const QUuid &frame, Bar &bar) const;
    void runBuildAction(const QUuid &frame, Action action);
    QString startBuild(const QUuid &frame, const QString &folder, const QString &note);
    void addBuildActions(const QUuid &frame, QMenu *menu);
    bool hasDesign(const QUuid &frame) const;
    // Generate a page in an empty frame, and Build It from one (BrowserViews+Generate.cpp): the sheet, the agent's staging folder,
    // the plan to confirm, and the new project's dev server.
    struct Generation;
    struct Generated {
        QString folder;
        QUrl dev;
        quint64 lease = 0;
    };
    static QString noPage();
    void runGenerateAction(const QUuid &frame, Action action, const QString &note = QString());
    QString startGenerate(const QUuid &frame, const QString &description, PageTemplates::Stack stack, const QString &folder, bool build,
                          const QString &note);
    void generateWritten(const QUuid &frame, bool cancelled, const QString &summary, const QString &error);
    void confirmGenerate(const QUuid &frame, std::vector<PageTemplates::File> files);
    void serveGenerated(const QUuid &frame);
    void endGenerate(const QUuid &frame, const QString &notice);
    void cancelGenerate(const QUuid &frame);
    void stopGenerations();
    // The document is going: a page being written stops, and nothing of the session is touched.
    void abandonGenerations();
    void showActivity(const QUuid &frame);
    void releaseGenerated(const QUuid &frame);
    QString generateLine(const QUuid &frame) const;
    // The bar's Build slot while a page is being made for the frame; false when none is.
    bool fillGenerating(const QUuid &frame, Bar &bar) const;
    void call(const Entry &entry, const QString &method, const QJsonObject &params = {});
    QUuid frameOf(const QUuid &key) const;
    void note(const QUuid &frame, State state);

    EditorSession &m_session;
    QPointer<EditorCanvas> m_canvas;
    QUuid m_scope = QUuid::createUuid();
    QHash<QUuid, Entry> m_entries;
    QHash<QUuid, QUuid> m_frameOfKey;
    // Frames whose tab is on the dev server: {dev origin, production origin}, apart from the entries so a reopened tab
    // still goes to the server.
    // A retired swap no longer sends the tab to the server, but still names the server's pages by their production
    // address until the tab leaves the dev origin, so a dev address never reaches the document.
    struct DevSwap {
        QUrl dev;
        QUrl production;
        bool retired = false;
    };
    QHash<QUuid, DevSwap> m_swaps;
    // Frames that show a preview, and the project each previews.
    QHash<QUuid, QString> m_previewed;
    void onPreviewChanged(const QString &folder);
    void endPreview(const QUuid &frame);
    QPointer<AgentBridge> m_agent;
    QHash<QUuid, std::shared_ptr<Generation>> m_generations;
    QHash<QUuid, Generated> m_generated;
    // When each project last deployed, from the bridge's state (ms since the epoch).
    QHash<QString, qint64> m_deployedAt;
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
    // Each Browser View's switch as last seen, so a change (undo included) is followed once.
    QHash<QUuid, bool> m_wasOn;
    // Frames switched on whose project's server should start once they have an address and are seen.
    QSet<QUuid> m_serveWanted;
    // Frames whose timeline is open; they stream every frame too, until it closes.
    QSet<QUuid> m_scrubbed;
    QElapsedTimer m_clock;
};
