#pragma once
#include "Live/Browser.h"
#include "Live/BrowserLink.h"
#include "Live/BrowserPool.h"
#include "Live/DevServers.h"
#include "Live/EditSets.h"
#include "Live/Tokens.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QUuid>
#include <optional>
#include <vector>

// One change made on the page, as write-back (Phase 6) needs it: where, what
// it was, what it became, and the token and class swap it snapped to.
struct LiveEdit {
    QString selector;
    // A CSS property, or "text".
    QString property;
    QString before;
    QString after;
    QString token;
    QString removeClass;
    QString addClass;
    QString classesBefore;
    QString classesAfter;
    // The element as it was before its first edit.
    QJsonObject element;
    // The page it was made on (EditSets::pathOf), and the site (EditSets::originOf), so a frame that
    // browses on never shows or keeps an edit on another site's page.
    QString path;
    QString origin;
    QJsonObject toJson() const;
    friend bool operator==(const LiveEdit &, const LiveEdit &) = default;
};

// Live mode (docs/OS-SUITE.md): a page in Omastrator's own Chromium with the
// overlay injected. Edits apply to the page at once, snapped to the page's
// tokens, and are kept for write-back. Pages without a registered project
// folder are mock-ups: they change in the browser only.
class LiveSession : public QObject {
    Q_OBJECT
public:
    enum class State { off, starting, running, failed };
    struct Target {
        // A page to open; with `folder`, the user has confirmed it is that site's code.
        QUrl url;
        QString folder;
        bool headless = false;
        // An Omarchy web app: the page opens as an app window.
        bool app = false;
        // An Electron app's command line, relaunched with debugging in a dedicated profile.
        QString command;
        QString profile;
        // The user's own Chromium, through Omastrator's extension: this tab, or 0 for the
        // active tab of its last focused window. The page is used as it is: nothing is opened or reloaded.
        int tab = -1;
        // A Browser View's tab in the pool (docs/LIVE-IN-FRAME.md): the session lives on the pool's thread, opens nothing
        // and waits for the tab, and again after the tab is replaced. `url` is ignored; the tab's page is the page.
        QUuid frame;
        BrowserPool *pool = nullptr;
    };

    explicit LiveSession(QObject *parent = nullptr);
    ~LiveSession() override;

    // Checks the target, then starts in the background. Returns why it can't start, or empty.
    QString start(const Target &target);
    void stop();
    // Deletes the session once none of its own DevTools waits is on the stack: a wait runs the thread's events,
    // and a delete from elsewhere would run inside it. Call it on the session's thread, after stop().
    void deleteWhenIdle();

    State state() const { return m_state; }
    QString message() const { return m_message; }
    QUrl url() const { return m_url; }
    QString project() const { return m_project; }
    bool isMockup() const { return m_project.isEmpty(); }
    const std::vector<LiveEdit> &edits() const { return m_edits; }
    const QJsonArray &selection() const { return m_selection; }
    const TokenSet &tokens() const { return m_tokens; }
    // The project's dev server, while this session holds it.
    const DevCommand &serverCommand() const { return m_serverCommand; }
    QUrl serverUrl() const { return m_serverUrl; }
    // A frame's project is starting its dev server; the tab still shows the production page.
    bool startingServer() const { return m_serving; }
    // Show Original is on: the page is as the site made it.
    bool showingOriginal() const { return m_original; }
    bool inFrame() const { return m_pool != nullptr; }
    QUuid frame() const { return m_frame; }
    // The last hover and selection boxes the overlay reported (frame host): {hover, selection, scroll, viewport}.
    const QJsonObject &geometry() const { return m_geometry; }
    // Edit Page: the page takes the pointer and reports what is under it. Off, the page is plain.
    void setPageEditing(bool on);
    bool pageEditing() const { return m_pageEditing; }
    Browser &browser() { return m_browser; }
    // The user's Chromium; Live in a tab needs it connected.
    void setBrowserLink(BrowserLink *link);
    bool inUserBrowser() const { return m_inTab; }
    // The process whose windows show the page: Omastrator's browser, or the user's Chromium. 0 when not running.
    qint64 browserProcessId() const;
    // The page's title, which its window's title starts with.
    QString pageTitle() const { return m_title; }
    // The page's DevTools session, for input the tests and native apps send.
    QString pageSession() const { return m_page ? m_page->sessionId : QString(); }
    // For the status stream's "live" key.
    QJsonObject status() const;

    // Runs `expression` in the page and returns its value.
    QJsonValue evaluate(const QString &expression, QString *error = nullptr);
    // The bar's path for one edit: snap, apply, record. Returns why it failed, or empty.
    QString edit(const QString &selector, const QString &property, const QString &value);
    // Live's own undo: each edit put back as it was, per session. Both return why they couldn't, or empty.
    bool canUndoEdit() const { return !m_undo.empty(); }
    bool canRedoEdit() const { return !m_redo.empty(); }
    // Changes every selected element, as one undo step: `properties` all take `value`.
    QString editSelection(const QStringList &properties, const QString &value);
    // Shows a value on the selection without recording it, for scrubs; the edit that follows takes it off first.
    QString previewSelection(const QStringList &properties, const QString &value);
    QString undoEdit();
    QString redoEdit();
    // Forgets the recorded edits, or keeps only `edits` (the ones write-back left for the agent).
    void clearEdits();
    void setEdits(std::vector<LiveEdit> edits);
    // Forgets exactly these edits (by identity, every field): one made after the caller read the list stays.
    void removeEdits(const std::vector<LiveEdit> &edits);
    void removeEdit(int index);
    // Shows a line in the overlay's bar.
    void notice(const QString &text);
    // A PNG of the page, or of one element with some room around it. Returns why it failed, or empty.
    QString screenshot(const QString &path, const QString &selector = QString());

    // A site that isn't the user's (docs/ANYWHERE.md): its edits are kept as named sets for its origin, put back on
    // every visit, never deployed. Each returns why it failed, or empty.
    QString origin() const;
    std::vector<EditSets::Set> editSets() const;
    // Keeps the edits not kept yet in the set `name` (the next "Edits N" when empty) and clears them.
    QString keepEdits(const QString &name, QString *kept = nullptr);
    QString setEditSetEnabled(const QString &name, bool enabled);
    QString removeEditSet(const QString &name);
    // What's on the page now (enabled sets and edits not kept yet), or one set by name.
    std::vector<EditSets::Edit> editsShown(const QString &name = QString()) const;
    // true: the page as the site made it; false: every enabled set and unkept edit back on.
    QString showOriginal(bool original);

    // The overlay's source.
    static QString overlayScript();

signals:
    void changed();
    void geometryChanged();
    void editApplied(const LiveEdit &edit);
    // "Ask AI…" in the bar: the prompt and the selected elements.
    void askRequested(const QString &prompt, const QJsonArray &elements);
    // The page's "Not your site" strip: keep, export, beforeAfter, handoff or toggle, with a set's name.
    void siteRequested(const QString &action, const QJsonObject &params);
    // A frame left one project for another site: the edits it had made on the first, which its owner keeps.
    void editsLeft(const QString &project, const std::vector<LiveEdit> &edits);

private:
    void run(Target target);
    void runFrame(const Target &target);
    // Joins the frame's tab if it has one (else waits for `opened`), and brings the overlay in.
    void attachFrame();
    void tabGone();
    void leaveFrame();
    // Which folder the frame's page is the code of: the one given, else the registry's for its address.
    void frameProject();
    // A frame on one of the user's own sites at a remote address runs from the project's dev server (section 2).
    bool needsServer() const;
    // Joins or starts the project's dev server and carries on from its answer, so the thread is never held.
    void serveProject(int generation);
    // The window's own start: the dev server for `folder`, waited for here. Returns why not.
    QString startServer(const QString &folder, int generation);
    void releaseServer(bool wait);
    static QString frameOverlayScript();
    // Every DevTools wait goes through here, so the session knows it is on the stack.
    QJsonObject call(CdpConnection &connection, const QString &method, const QJsonObject &params, const QString &sessionId,
                     QString *error = nullptr, int timeoutMs = 15'000);
    struct Busy {
        explicit Busy(LiveSession &session) : session(session) { ++session.m_depth; }
        ~Busy();
        LiveSession &session;
    };
    // A frame's edits belong to its project or, on a site that isn't the user's, to that site's origin.
    void leaveProject();
    bool editIsHere(const LiveEdit &edit) const;
    // The binding and the overlay script, before the page loads.
    QString prepare();
    void fail(const QString &message);
    void setState(State state, const QString &message = QString());
    void onEvent(const QString &method, const QJsonObject &params, const QString &sessionId);
    void handle(const QJsonObject &message);
    void rescanTokens();
    // After a load: the address as it is now, the tokens, and on a mock-up its edit sets put back.
    void pageLoaded();
    // Tells the overlay about the edit sets (a mock-up) or that the page is the user's.
    void describeSite();
    void record(const QJsonObject &element, const TokenSet::Resolution &resolution, const QJsonObject &applied, const QString &textBefore);

    // Live in the user's tab: its DevTools messages go through the extension.
    CdpConnection &cdp()
    {
        if (m_pool && m_pool->cdp())
            return *m_pool->cdp();
        return m_inTab && m_link ? m_link->cdp() : m_browser.cdp();
    }
    void runInTab(const Target &target);
    // Takes the overlay out of the user's tab and lets the tab go.
    void leaveTab();

    Browser m_browser;
    QPointer<BrowserLink> m_link;
    bool m_inTab = false;
    QString m_title;
    quint64 m_lease = 0;
    QString m_serverFolder;
    // The project the held lease serves, so coming back to the dev server finds it again.
    QString m_serverProject;
    // The origin the frame's explicit folder was given for.
    QString m_targetOrigin;
    int m_depth = 0;
    bool m_deleteWhenIdle = false;
    DevCommand m_serverCommand;
    QUrl m_serverUrl;
    bool m_serving = false;
    bool m_original = false;
    QPointer<BrowserPool> m_pool;
    QUuid m_frame;
    QString m_targetFolder;
    QString m_scriptId;
    QJsonObject m_geometry;
    bool m_pageEditing = false;
    struct UndoStep {
        QString selector;
        QString property;
        // {style, cls, text}: null where the attribute or text isn't part of the edit.
        QJsonObject was;
        QJsonObject now;
        std::optional<LiveEdit> replaced;
        LiveEdit made;
        // Steps of one change to several properties or elements undo together; 0 is a step alone.
        int group = 0;
    };
    QString applyEdit(const QString &selector, const QString &property, const QString &value);
    QString undoStep();
    QString redoStep();
    void refreshSelection();
    int m_group = 0;
    int m_lastGroup = 0;
    std::vector<UndoStep> m_undo;
    std::vector<UndoStep> m_redo;
    // Both stacks emptied, and counted: an undo or redo waiting on the page sees whether that happened meanwhile.
    void forgetSteps();
    int m_stepsEpoch = 0;
    std::optional<Browser::Page> m_page;
    State m_state = State::off;
    QString m_message;
    QUrl m_url;
    QString m_project;
    TokenSet m_tokens;
    QJsonArray m_selection;
    std::vector<LiveEdit> m_edits;
    // Bumped by stop(), so a start still running notices it was cancelled.
    int m_generation = 0;
};
