#include "UI/LiveFrames.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include <QFileInfo>
#include <map>

namespace {
QList<LiveFrames *> &instances()
{
    static QList<LiveFrames *> all;
    return all;
}

std::map<QString, std::vector<LiveEdit>> &heldEdits()
{
    static std::map<QString, std::vector<LiveEdit>> held;
    return held;
}

bool alive(const QPointer<BrowserPool> &pool)
{
    return pool && pool->poolThread() && pool->poolThread()->isRunning();
}
}

LiveFrames *LiveFrames::of(EditorSession &session)
{
    if (auto *existing = session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly))
        return existing;
    return new LiveFrames(session);
}

LiveFrames::LiveFrames(EditorSession &session) : QObject(&session), m_session(session)
{
    instances().append(this);
}

LiveFrames::~LiveFrames()
{
    // A session posts to this object, so they end first; the edits stay for Deploy.
    const QList<QUuid> all = m_frames.keys();
    for (const QUuid &frame : all)
        teardown(frame);
    instances().removeAll(this);
}

QString LiveFrames::canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}

QString LiveFrames::start(const QUuid &frame, const QString &folder)
{
    if (m_frames.contains(frame))
        return {};
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return QStringLiteral("That isn't a Browser View.");
    if (!folder.isEmpty() && !QFileInfo(folder).isDir())
        return QStringLiteral("%1 isn't a folder.").arg(folder);
    const QUuid key = BrowserViews::of(m_session)->poolKey(frame);
    if (key.isNull())
        return QStringLiteral("The page isn't open yet. Show the frame, then try again.");
    BrowserPool *pool = BrowserViews::pool();

    auto *session = new LiveSession;
    session->moveToThread(pool->poolThread());
    Frame entry;
    entry.key = key;
    entry.pool = pool;
    entry.session = session;
    entry.snapshot.state = LiveSession::State::starting;
    entry.snapshot.message = QStringLiteral("Waiting for the page…");
    m_frames.insert(frame, entry);

    // The session and these slots share the pool's thread, so a change is read where it happened.
    const auto report = [this, frame, session] { publish(frame, capture(*session), session); };
    connect(session, &LiveSession::changed, session, report);
    connect(session, &LiveSession::geometryChanged, session, report);

    LiveSession::Target target;
    target.frame = key;
    target.pool = pool;
    target.folder = folder.isEmpty() ? QString() : canonical(folder);
    pool->run([session = QPointer<LiveSession>(session), target] {
        if (session)
            session->start(target);
    });
    emit changed(frame);
    return {};
}

LiveFrames::Snapshot LiveFrames::capture(const LiveSession &session)
{
    Snapshot snapshot;
    snapshot.state = session.state();
    snapshot.message = session.message();
    snapshot.project = session.project();
    snapshot.mockup = session.isMockup();
    snapshot.url = session.url();
    snapshot.selection = session.selection();
    snapshot.geometry = session.geometry();
    snapshot.edits = session.edits();
    snapshot.tokens = session.tokens().toJson();
    snapshot.canUndo = session.canUndoEdit();
    snapshot.canRedo = session.canRedoEdit();
    snapshot.pageEditing = session.pageEditing();
    snapshot.serverCommand = session.serverCommand().description;
    snapshot.serverUrl = session.serverUrl();
    snapshot.startingServer = session.startingServer();
    snapshot.original = session.showingOriginal();
    return snapshot;
}

// Runs on the pool's thread.
void LiveFrames::publish(const QUuid &frame, const Snapshot &snapshot, LiveSession *from)
{
    QMetaObject::invokeMethod(this, [this, frame, snapshot, from] {
        const auto found = m_frames.find(frame);
        // A session that was stopped may still have one last change in flight.
        if (found == m_frames.end() || found->session != from)
            return;
        const QUrl before = found->snapshot.serverUrl;
        found->snapshot = snapshot;
        // The tab moves to the dev server, or back to the production page, before anyone reads the change.
        if (before != snapshot.serverUrl) {
            if (BrowserViews *views = m_session.findChild<BrowserViews *>(QString(), Qt::FindDirectChildrenOnly))
                views->useDevServer(frame, snapshot.serverUrl);
        }
        emit changed(frame);
    }, Qt::QueuedConnection);
}

LiveFrames::Snapshot LiveFrames::snapshot(const QUuid &frame) const
{
    const auto found = m_frames.constFind(frame);
    return found == m_frames.constEnd() ? Snapshot{} : found->snapshot;
}

void LiveFrames::run(const QUuid &frame, std::function<QString(LiveSession &)> command, Done done)
{
    const auto found = m_frames.constFind(frame);
    if (found == m_frames.constEnd() || !alive(found->pool)) {
        if (done)
            QMetaObject::invokeMethod(this, [done] { done(QStringLiteral("Live isn't running on that frame.")); }, Qt::QueuedConnection);
        return;
    }
    found->pool->run([session = QPointer<LiveSession>(found->session), command = std::move(command), done = std::move(done), owner = QPointer<LiveFrames>(this)] {
        if (!session)
            return;
        const QString result = command(*session);
        if (done && owner)
            QMetaObject::invokeMethod(owner.data(), [done, result] { done(result); }, Qt::QueuedConnection);
    });
}

void LiveFrames::edit(const QUuid &frame, const QString &selector, const QString &property, const QString &value, Done done)
{
    run(frame, [=](LiveSession &live) { return live.edit(selector, property, value); }, std::move(done));
}

void LiveFrames::undo(const QUuid &frame, Done done)
{
    run(frame, [](LiveSession &live) { return live.undoEdit(); }, std::move(done));
}

void LiveFrames::redo(const QUuid &frame, Done done)
{
    run(frame, [](LiveSession &live) { return live.redoEdit(); }, std::move(done));
}

void LiveFrames::setPageEditing(const QUuid &frame, bool on)
{
    run(frame, [on](LiveSession &live) {
        live.setPageEditing(on);
        return QString();
    });
}

void LiveFrames::stop(const QUuid &frame)
{
    if (!m_frames.contains(frame))
        return;
    teardown(frame);
    if (BrowserViews *views = m_session.findChild<BrowserViews *>(QString(), Qt::FindDirectChildrenOnly))
        views->useDevServer(frame, {});
    emit changed(frame);
}

// Ends the session on its own thread and waits, so nothing of it is left running or posting afterwards.
void LiveFrames::teardown(const QUuid &frame)
{
    const auto found = m_frames.find(frame);
    if (found == m_frames.end())
        return;
    LiveSession *session = found->session;
    const QPointer<BrowserPool> pool = found->pool;
    Snapshot last = found->snapshot;
    m_frames.erase(found);
    if (alive(pool)) {
        QMetaObject::invokeMethod(session, [&] {
            last = capture(*session);
            session->disconnect();
            session->stop();
        }, Qt::BlockingQueuedConnection);
        session->deleteLater();
    } else {
        // The pool's thread is gone, and so is anything it could still say.
        delete session;
    }
    if (!last.mockup && !last.project.isEmpty() && !last.edits.empty())
        hold(last.project, last.edits);
}

void LiveFrames::hold(const QString &folder, std::vector<LiveEdit> edits)
{
    if (folder.isEmpty() || edits.empty())
        return;
    std::vector<LiveEdit> &kept = heldEdits()[canonical(folder)];
    kept.insert(kept.end(), std::make_move_iterator(edits.begin()), std::make_move_iterator(edits.end()));
}

std::vector<LiveEdit> LiveFrames::held(const QString &folder)
{
    const auto found = heldEdits().find(canonical(folder));
    return found == heldEdits().end() ? std::vector<LiveEdit>{} : found->second;
}

std::vector<LiveEdit> LiveFrames::pendingEdits(const QString &folder)
{
    std::vector<LiveEdit> edits = held(folder);
    const QString project = canonical(folder);
    for (LiveFrames *frames : std::as_const(instances())) {
        for (auto it = frames->m_frames.constBegin(); it != frames->m_frames.constEnd(); ++it) {
            const Snapshot &snapshot = it->snapshot;
            if (snapshot.mockup || snapshot.project.isEmpty() || canonical(snapshot.project) != project)
                continue;
            edits.insert(edits.end(), snapshot.edits.begin(), snapshot.edits.end());
        }
    }
    return edits;
}

void LiveFrames::clearPending(const QString &folder)
{
    const QString project = canonical(folder);
    heldEdits().erase(project);
    for (LiveFrames *frames : std::as_const(instances())) {
        for (auto it = frames->m_frames.begin(); it != frames->m_frames.end(); ++it) {
            Snapshot &snapshot = it->snapshot;
            if (snapshot.mockup || snapshot.project.isEmpty() || canonical(snapshot.project) != project)
                continue;
            // The panel reads this at once; the session catches up on its own thread.
            snapshot.edits.clear();
            snapshot.canUndo = false;
            snapshot.canRedo = false;
            frames->run(it.key(), [](LiveSession &live) {
                live.setEdits({});
                return QString();
            });
            emit frames->changed(it.key());
        }
    }
}

bool LiveFrames::projectInUse(const QString &folder)
{
    const QString project = canonical(folder);
    for (LiveFrames *frames : std::as_const(instances()))
        for (const Frame &frame : std::as_const(frames->m_frames))
            if (!frame.snapshot.mockup && !frame.snapshot.project.isEmpty() && canonical(frame.snapshot.project) == project)
                return true;
    return false;
}

QUrl LiveFrames::urlOf(const QString &folder)
{
    const QString project = canonical(folder);
    for (LiveFrames *frames : std::as_const(instances()))
        for (const Frame &frame : std::as_const(frames->m_frames))
            if (!frame.snapshot.mockup && !frame.snapshot.project.isEmpty() && canonical(frame.snapshot.project) == project)
                return frame.snapshot.url;
    return {};
}

QString LiveFrames::selectedProject(EditorSession *session)
{
    if (!session)
        return {};
    LiveFrames *frames = session->findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (!frames)
        return {};
    for (const QUuid &id : session->selection()) {
        const auto found = frames->m_frames.constFind(id);
        if (found != frames->m_frames.constEnd() && !found->snapshot.mockup && !found->snapshot.project.isEmpty())
            return canonical(found->snapshot.project);
    }
    return {};
}

void LiveFrames::stopAll()
{
    const QList<LiveFrames *> all = instances();
    for (LiveFrames *frames : all) {
        const QList<QUuid> ids = frames->m_frames.keys();
        for (const QUuid &frame : ids)
            frames->stop(frame);
    }
}
