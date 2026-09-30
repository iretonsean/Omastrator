#include "Live/DevServers.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <algorithm>

DevServers::DevServers(QObject *parent) : QObject(parent)
{
}

DevServers::~DevServers()
{
    stopAll();
}

DevServers &DevServers::shared()
{
    static DevServers *servers = [] {
        auto *made = new DevServers;
        if (QCoreApplication::instance())
            made->moveToThread(QCoreApplication::instance()->thread());
        // A server must not outlive the app.
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, made, [made] { made->stopAll(); });
        return made;
    }();
    return *servers;
}

QString DevServers::keyFor(const QString &folder)
{
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(folder).absoluteFilePath() : canonical;
}

void DevServers::deliver(const Waiter &waiter, const Result &result)
{
    if (!waiter.context || !waiter.done)
        return;
    QMetaObject::invokeMethod(waiter.context.data(), [done = waiter.done, result] { done(result); }, Qt::QueuedConnection);
}

quint64 DevServers::acquire(const QString &folder, QObject *context, Done done, bool allowInstall)
{
    const quint64 lease = m_nextLease++;
    const QString key = keyFor(folder);
    QMutexLocker lock(&m_mutex);
    reap(false);
    const auto found = m_entries.find(key);
    if (found != m_entries.end()) {
        const EntryPtr entry = found->second;
        entry->leases.push_back(lease);
        m_leases.insert(lease, entry);
        // A new holder wants a server that answers.
        applyPause(entry);
        const Waiter waiter{lease, context, std::move(done)};
        if (entry->ready)
            deliver(waiter, entry->result);
        else
            entry->waiters.push_back(waiter);
        return lease;
    }
    auto entry = std::make_shared<Entry>();
    entry->folder = key;
    entry->allowInstall = allowInstall;
    entry->result.folder = key;
    entry->leases.push_back(lease);
    entry->waiters.push_back({lease, context, std::move(done)});
    entry->thread = new QThread;
    entry->thread->setObjectName(QStringLiteral("dev-server"));
    entry->worker = new QObject;
    entry->worker->moveToThread(entry->thread);
    entry->thread->start();
    m_entries[key] = entry;
    m_leases.insert(lease, entry);
    runStart(entry);
    return lease;
}

void DevServers::runStart(const EntryPtr &entry)
{
    QMetaObject::invokeMethod(entry->worker, [this, entry] {
        // Made here, so its process and sockets belong to this thread.
        entry->starting = true;
        auto *server = new DevServer;
        {
            QMutexLocker lock(&m_mutex);
            entry->server = server;
        }
        connect(server, &DevServer::step, this, [this, entry](const QString &message) { emit step(entry->folder, message); });
        connect(server, &DevServer::exited, this, [this, entry] {
            {
                QMutexLocker lock(&m_mutex);
                const auto found = m_entries.find(entry->folder);
                // Stopped on purpose, or never up: nobody is told.
                if (found == m_entries.end() || found->second != entry || !entry->ready)
                    return;
                retire(entry);
            }
            emit exited(entry->folder);
        });
        server->setInstallAllowed(entry->allowInstall);
        const QString failure = server->start(entry->folder);
        entry->starting = false;
        // Released while it started: the server was stopped, and nobody wants the answer.
        if (entry->stopRequested) {
            delete server;
            entry->server = nullptr;
            entry->thread->quit();
            return;
        }
        Result result;
        result.folder = entry->folder;
        result.error = failure;
        result.url = server->url();
        result.command = server->command();
        result.output = server->output();
        std::vector<Waiter> waiters;
        {
            QMutexLocker lock(&m_mutex);
            const auto found = m_entries.find(entry->folder);
            if (found == m_entries.end() || found->second != entry)
                return;
            entry->result = result;
            waiters = std::exchange(entry->waiters, {});
            if (failure.isEmpty()) {
                entry->ready = true;
                entry->pid = server->processId();
                // Its frame was turned off while it started.
                applyPause(entry);
            } else {
                retire(entry);
            }
        }
        for (const Waiter &waiter : waiters)
            deliver(waiter, result);
    }, Qt::QueuedConnection);
}

void DevServers::retire(const EntryPtr &entry)
{
    const auto found = m_entries.find(entry->folder);
    if (found != m_entries.end() && found->second == entry)
        m_entries.erase(found);
    for (const quint64 lease : entry->leases)
        m_leases.remove(lease);
    entry->leases.clear();
    entry->waiters.clear();
    m_retired.push_back(entry);
    // Queued behind (or inside, while it starts) the server's own work; the thread ends after it.
    QMetaObject::invokeMethod(entry->worker, [entry] {
        // Inside start()'s own loops: only stop the process, and start() deletes the server when it returns.
        if (entry->starting) {
            entry->stopRequested = true;
            if (entry->server)
                entry->server->stop();
            return;
        }
        delete entry->server;
        entry->server = nullptr;
        entry->thread->quit();
    }, Qt::QueuedConnection);
}

void DevServers::release(quint64 lease, bool wait)
{
    EntryPtr retired;
    {
        QMutexLocker lock(&m_mutex);
        const EntryPtr entry = m_leases.take(lease);
        if (!entry)
            return;
        std::erase(entry->leases, lease);
        std::erase(entry->pausedLeases, lease);
        std::erase_if(entry->waiters, [&](const Waiter &each) { return each.lease == lease; });
        if (!entry->leases.empty()) {
            applyPause(entry);
            return;
        }
        retire(entry);
        retired = entry;
    }
    QMutexLocker lock(&m_mutex);
    if (wait)
        reapEntry(retired);
    reap(false);
}

void DevServers::setPaused(quint64 lease, bool paused)
{
    QMutexLocker lock(&m_mutex);
    const EntryPtr entry = m_leases.value(lease);
    if (!entry)
        return;
    std::erase(entry->pausedLeases, lease);
    if (paused)
        entry->pausedLeases.push_back(lease);
    applyPause(entry);
}

void DevServers::applyPause(const EntryPtr &entry)
{
    const bool wanted = entry->ready && !entry->leases.empty() && entry->pausedLeases.size() == entry->leases.size();
    if (wanted == entry->paused)
        return;
    entry->paused = wanted;
    // Queued, so it never lands inside start()'s own waiting; it reads the wish when it runs, and the last one wins.
    QMetaObject::invokeMethod(entry->worker, [this, entry] {
        bool freeze = false;
        {
            QMutexLocker lock(&m_mutex);
            freeze = entry->paused;
        }
        if (entry->server && !entry->starting)
            entry->server->setPaused(freeze);
    }, Qt::QueuedConnection);
}

bool DevServers::paused(const QString &folder) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.find(keyFor(folder));
    return found != m_entries.end() && found->second->paused;
}

qint64 DevServers::processId(const QString &folder) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.find(keyFor(folder));
    return found == m_entries.end() || !found->second->ready ? 0 : found->second->pid;
}

void DevServers::stopAll()
{
    QMutexLocker lock(&m_mutex);
    const auto entries = m_entries;
    for (const auto &[key, entry] : entries)
        retire(entry);
    reap(true);
}

// Called with the mutex held, which is let go while a thread is waited for: the server's thread takes it too.
void DevServers::reapEntry(const EntryPtr &entry)
{
    const auto found = std::find(m_retired.begin(), m_retired.end(), entry);
    // Another caller is already on it.
    if (found == m_retired.end())
        return;
    m_retired.erase(found);
    m_mutex.unlock();
    entry->thread->wait();
    delete entry->worker;
    entry->worker = nullptr;
    delete entry->thread;
    entry->thread = nullptr;
    m_mutex.lock();
}

void DevServers::reap(bool all)
{
    for (;;) {
        EntryPtr next;
        for (const EntryPtr &entry : m_retired) {
            if (all || entry->thread->isFinished()) {
                next = entry;
                break;
            }
        }
        if (!next)
            return;
        reapEntry(next);
    }
}

int DevServers::holders(const QString &folder) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.find(keyFor(folder));
    return found == m_entries.end() ? 0 : int(found->second->leases.size());
}

bool DevServers::running(const QString &folder) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.find(keyFor(folder));
    return found != m_entries.end() && found->second->ready;
}
