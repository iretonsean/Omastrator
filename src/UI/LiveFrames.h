#pragma once
#include "Live/LiveSession.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUuid>
#include <functional>
#include <vector>

class EditorSession;

// Live inside the Browser Views of one document (docs/LIVE-IN-FRAME.md, section 1): a LiveSession per frame, each on the
// browser pool's thread, attached to the frame's tab. The UI never calls a session: it reads a snapshot the session
// publishes after each change, and sends commands that run on the pool's thread. A session outlives its tab; only stop(),
// deleting the frame, closing the document and reset end it.
class LiveFrames : public QObject {
    Q_OBJECT
public:
    // What the UI may read of a frame's session, as of its last change.
    struct Snapshot {
        LiveSession::State state = LiveSession::State::off;
        QString message;
        QString project;
        bool mockup = true;
        QUrl url;
        QJsonArray selection;
        // {hover, selection, scroll, viewport} in page CSS px.
        QJsonObject geometry;
        std::vector<LiveEdit> edits;
        QJsonObject tokens;
        bool canUndo = false;
        bool canRedo = false;
        bool pageEditing = false;
        QString serverCommand;
        QUrl serverUrl;
        // The dev server is starting, and the tab is still on the production page.
        bool startingServer = false;
        bool original = false;
    };
    using Done = std::function<void(const QString &error)>;

    // One per session; made on first use and owned by it.
    static LiveFrames *of(EditorSession &session);
    explicit LiveFrames(EditorSession &session);
    ~LiveFrames() override;

    // Starts Live on the frame's tab, for the project in `folder` (else the registry's for the page's address).
    // Returns why it can't, or empty; the session reports the rest through its snapshot.
    QString start(const QUuid &frame, const QString &folder = QString());
    // Ends the frame's Live. Its own-site edits become held edits.
    void stop(const QUuid &frame);
    bool active(const QUuid &frame) const { return m_frames.contains(frame); }
    QList<QUuid> frames() const { return m_frames.keys(); }
    Snapshot snapshot(const QUuid &frame) const;

    // Runs `command` on the session, on the pool's thread; `done` gets its result on this thread.
    void run(const QUuid &frame, std::function<QString(LiveSession &)> command, Done done = {});
    void edit(const QUuid &frame, const QString &selector, const QString &property, const QString &value, Done done = {});
    void undo(const QUuid &frame, Done done = {});
    void redo(const QUuid &frame, Done done = {});
    void setPageEditing(const QUuid &frame, bool on);

    // Across every document. Pending edits of a project are its frames' and the held ones; the window's are the bridge's.
    static std::vector<LiveEdit> pendingEdits(const QString &folder);
    // What was written or kept: every frame's edits for the project, and the held ones, are gone.
    static void clearPending(const QString &folder);
    // Edits from a host that stopped, kept for the project until they are written or the app quits.
    static void hold(const QString &folder, std::vector<LiveEdit> edits);
    static std::vector<LiveEdit> held(const QString &folder);
    // The first frame among `session`'s selection that runs Live on one of the user's own sites, or empty.
    static QString selectedProject(EditorSession *session);
    // Any frame in any document runs Live on this project.
    static bool projectInUse(const QString &folder);
    // The address of the first frame running Live on this project, for the agent's brief.
    static QUrl urlOf(const QString &folder);
    // Reset, and the pool shutting down: every frame's Live stops, the servers are released, nothing restarts.
    static void stopAll();

signals:
    void changed(const QUuid &frame);

private:
    struct Frame {
        QUuid key;
        QPointer<BrowserPool> pool;
        // Lives on the pool's thread; only touched there once started.
        LiveSession *session = nullptr;
        Snapshot snapshot;
    };

    void publish(const QUuid &frame, const Snapshot &snapshot, LiveSession *from);
    void teardown(const QUuid &frame);
    static Snapshot capture(const LiveSession &session);
    static QString canonical(const QString &folder);

    EditorSession &m_session;
    QHash<QUuid, Frame> m_frames;
};
