#pragma once
#include "Live/DevServer.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// The dev servers of the open projects (docs/LIVE-IN-FRAME.md): one per folder, shared by whoever holds a lease on it
// (Live's window, a Browser View's Live). The last release stops it. Each server runs on a thread of its own, since
// DevServer::start waits in nested loops for up to two minutes. Every call is safe from any thread.
class DevServers : public QObject {
    Q_OBJECT
public:
    struct Result {
        QUrl url;
        DevCommand command;
        QString error;
        // What the server printed, for Details when it fails.
        QString output;
        QString folder;
    };
    using Done = std::function<void(const Result &)>;

    explicit DevServers(QObject *parent = nullptr);
    // Stops every server and waits for its thread.
    ~DevServers() override;
    static DevServers &shared();

    // The canonical folder a server is filed under.
    static QString keyFor(const QString &folder);

    // Starts the project's server, or joins the one that runs. `done` runs on `context`'s thread with the address, or
    // why it failed (a failed lease holds nothing). Returns the lease, at once.
    quint64 acquire(const QString &folder, QObject *context, Done done);
    // Gives the lease back; the last one stops the server. With `wait`, it returns once the server has stopped.
    // A lease still starting never calls its `done`.
    void release(quint64 lease, bool wait = false);
    // Stops everything and waits.
    void stopAll();

    // A frozen lease (its Browser View is off). The server is frozen while every lease on it is (DevServer::setPaused), and
    // wakes as soon as one isn't, or a new one joins. Frozen while it starts, it freezes once it answers. Releasing a frozen
    // lease, stopAll and quitting wake the server before they stop it, so none is left stopped.
    void setPaused(quint64 lease, bool paused);
    bool paused(const QString &folder) const;
    // The server's process (its group's leader), or 0 for a static site, one still starting, or none.
    qint64 processId(const QString &folder) const;

    int holders(const QString &folder) const;
    bool running(const QString &folder) const;

signals:
    // While a server starts: "Installing packages…". Emitted on the thread that made the manager.
    void step(const QString &folder, const QString &message);
    // A server ended by itself; its holders should find out.
    void exited(const QString &folder);

private:
    struct Waiter {
        quint64 lease = 0;
        QPointer<QObject> context;
        Done done;
    };
    struct Entry {
        QString folder;
        QThread *thread = nullptr;
        QObject *worker = nullptr;
        DevServer *server = nullptr;
        // Only touched on the server's own thread.
        bool starting = false;
        bool stopRequested = false;
        bool ready = false;
        std::vector<quint64> leases;
        std::vector<quint64> pausedLeases;
        // Whether the server is (or is about to be) frozen, under the mutex; the server's thread applies it.
        bool paused = false;
        qint64 pid = 0;
        std::vector<Waiter> waiters;
        Result result;
    };
    using EntryPtr = std::shared_ptr<Entry>;

    void runStart(const EntryPtr &entry);
    // Freezes or wakes the server to match its leases. Called with the mutex held.
    void applyPause(const EntryPtr &entry);
    // Takes the entry out of use and stops its server on its own thread. Called with the mutex held.
    void retire(const EntryPtr &entry);
    void reap(bool all);
    // Removes one retired entry, waits for its thread and deletes it.
    void reapEntry(const EntryPtr &entry);
    static void deliver(const Waiter &waiter, const Result &result);

    mutable QMutex m_mutex;
    std::map<QString, EntryPtr> m_entries;
    QHash<quint64, EntryPtr> m_leases;
    std::vector<EntryPtr> m_retired;
    std::atomic<quint64> m_nextLease{1};
};
