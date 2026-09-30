#include "Live/DevServers.h"
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QStandardPaths>
#include <QTimer>
#include <csignal>

// The shared dev servers (docs/LIVE-IN-FRAME.md): one process per folder, held by leases. Static sites, a failing
// override and Python's http.server on 127.0.0.1 stand in for real dev servers, so no Chromium or node is needed.
namespace {
// A process's state letter from /proc ('T' is stopped), or 0 once it's gone or reaped.
char stateOf(qint64 pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (pid <= 0 || !stat.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = stat.readAll();
    const qsizetype close = line.lastIndexOf(')');
    const char state = close > 0 && close + 2 < line.size() ? line.at(close + 2) : 0;
    return state == 'Z' ? 0 : state;
}

qint64 readPid(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toLongLong() : 0;
}
}

#define NEEDS_PYTHON \
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) \
        QSKIP("python3 isn't installed, and the fake dev server is Python's.")

class DevServersTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString site(const QString &name, const QString &dev = QString())
    {
        const QString folder = m_directory.filePath(name);
        QDir().mkpath(folder);
        QFile index(folder + QStringLiteral("/index.html"));
        if (index.open(QIODevice::WriteOnly))
            index.write("<!doctype html><title>x</title><p>hi</p>");
        if (!dev.isEmpty()) {
            QFile config(folder + QStringLiteral("/omastrator.json"));
            if (config.open(QIODevice::WriteOnly))
                config.write(QJsonDocument(QJsonObject{{"dev", dev}}).toJson());
        }
        return folder;
    }

    // Acquires and waits for the answer.
    DevServers::Result acquire(DevServers &servers, const QString &folder, quint64 *lease)
    {
        QEventLoop loop;
        DevServers::Result got;
        bool done = false;
        *lease = servers.acquire(folder, &loop, [&](const DevServers::Result &result) {
            got = result;
            done = true;
            loop.quit();
        });
        if (!done) {
            QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return got;
    }

    // A fake dev server on 127.0.0.1 with a helper process in its group, whose pid lands in `helper.pid`.
    QString pythonSite(const QString &name)
    {
        const QString folder = site(name, QStringLiteral("sleep 300 & echo $! > helper.pid; exec python3 -u -m http.server 0 --bind 127.0.0.1"));
        return folder;
    }

private slots:
    void initTestCase() { QVERIFY(m_directory.isValid()); }

    // Browser View off: the server's whole process group stops, keeps its PID, and wakes as the same process.
    void aFrozenLeaseStopsTheGroupAndWakingKeepsThePid()
    {
        NEEDS_PYTHON;
        DevServers servers;
        const QString folder = pythonSite(QStringLiteral("frozen"));
        quint64 lease = 0;
        const DevServers::Result result = acquire(servers, folder, &lease);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const qint64 pid = servers.processId(folder);
        const qint64 helper = readPid(folder + QStringLiteral("/helper.pid"));
        QVERIFY(pid > 0);
        QVERIFY(helper > 0);
        QVERIFY(stateOf(pid) != 'T');
        servers.setPaused(lease, true);
        QVERIFY(servers.paused(folder));
        QTRY_COMPARE(stateOf(pid), 'T');
        QTRY_COMPARE(stateOf(helper), 'T');
        QVERIFY(!DevServer::answers(result.url, 500));
        QVERIFY(servers.running(folder));
        servers.setPaused(lease, false);
        QTRY_VERIFY(stateOf(pid) != 'T' && stateOf(pid) != 0);
        QTRY_VERIFY(stateOf(helper) != 'T' && stateOf(helper) != 0);
        QCOMPARE(servers.processId(folder), pid);
        QVERIFY(DevServer::answers(result.url, 2000));
        servers.release(lease, true);
        QTRY_COMPARE(stateOf(pid), char(0));
        QTRY_COMPARE(stateOf(helper), char(0));
    }

    // A server shared with a holder that still wants it (Live's window, another frame) never freezes.
    void itFreezesOnlyWhenEveryLeaseIsFrozen()
    {
        NEEDS_PYTHON;
        DevServers servers;
        const QString folder = pythonSite(QStringLiteral("shared"));
        quint64 first = 0, second = 0;
        QVERIFY(acquire(servers, folder, &first).error.isEmpty());
        const DevServers::Result result = acquire(servers, folder, &second);
        QVERIFY(result.error.isEmpty());
        const qint64 pid = servers.processId(folder);
        servers.setPaused(first, true);
        QVERIFY(!servers.paused(folder));
        QVERIFY(DevServer::answers(result.url, 2000));
        servers.setPaused(second, true);
        QTRY_COMPARE(stateOf(pid), 'T');
        // The holder that's still on leaving lets the other's wish stand; a new holder wakes it.
        quint64 third = 0;
        QVERIFY(acquire(servers, folder, &third).error.isEmpty());
        QTRY_VERIFY(stateOf(pid) != 'T');
        servers.release(third, true);
        QTRY_COMPARE(stateOf(pid), 'T');
        // Releasing a frozen lease while another stays frozen keeps it frozen, and the last release ends it.
        servers.release(first, true);
        QCOMPARE(stateOf(pid), 'T');
        servers.release(second, true);
        QTRY_COMPARE(stateOf(pid), char(0));
    }

    // Turned off while it was still starting: it freezes once it answers, not before (the wait would time out).
    void aLeaseFrozenWhileStartingFreezesOnceItAnswers()
    {
        NEEDS_PYTHON;
        DevServers servers;
        const QString folder = pythonSite(QStringLiteral("early"));
        QEventLoop loop;
        DevServers::Result got;
        const quint64 lease = servers.acquire(folder, &loop, [&](const DevServers::Result &result) {
            got = result;
            loop.quit();
        });
        servers.setPaused(lease, true);
        QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
        loop.exec();
        QVERIFY2(got.error.isEmpty(), qPrintable(got.error));
        QTRY_VERIFY(servers.paused(folder));
        QTRY_COMPARE(stateOf(servers.processId(folder)), 'T');
        servers.stopAll();
    }

    // Quitting: every server ends, frozen or running, with what it started, and nothing is left stopped.
    void stopAllEndsFrozenAndRunningServersAndTheirHelpers()
    {
        NEEDS_PYTHON;
        DevServers servers;
        const QString frozen = pythonSite(QStringLiteral("quit-frozen"));
        const QString running = pythonSite(QStringLiteral("quit-running"));
        quint64 a = 0, b = 0;
        QVERIFY(acquire(servers, frozen, &a).error.isEmpty());
        QVERIFY(acquire(servers, running, &b).error.isEmpty());
        const QList<qint64> pids{servers.processId(frozen), readPid(frozen + QStringLiteral("/helper.pid")), servers.processId(running),
                                 readPid(running + QStringLiteral("/helper.pid"))};
        for (const qint64 pid : pids)
            QVERIFY(pid > 0);
        servers.setPaused(a, true);
        QTRY_COMPARE(stateOf(pids[0]), 'T');
        servers.stopAll();
        QVERIFY(!servers.running(frozen));
        QVERIFY(!servers.running(running));
        for (const qint64 pid : pids)
            QTRY_COMPARE(stateOf(pid), char(0));
    }

    void aStaticSiteStopsAnsweringWhileFrozen()
    {
        DevServers servers;
        const QString folder = site(QStringLiteral("static-frozen"));
        quint64 lease = 0;
        const DevServers::Result result = acquire(servers, folder, &lease);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(servers.processId(folder), qint64(0));
        servers.setPaused(lease, true);
        QTRY_VERIFY(!DevServer::answers(result.url, 300));
        servers.setPaused(lease, false);
        QTRY_VERIFY(DevServer::answers(result.url, 1000));
        servers.release(lease, true);
    }

    void twoAcquiresStartOneProcessAndTheLastReleaseStopsIt()
    {
        DevServers servers;
        const QString folder = site(QStringLiteral("plain"));
        quint64 first = 0, second = 0;
        const DevServers::Result a = acquire(servers, folder, &first);
        QVERIFY2(a.error.isEmpty(), qPrintable(a.error));
        QVERIFY(DevServer::answers(a.url, 2000));
        const DevServers::Result b = acquire(servers, folder, &second);
        QVERIFY2(b.error.isEmpty(), qPrintable(b.error));
        QCOMPARE(b.url, a.url);
        QVERIFY(first != second);
        QCOMPARE(servers.holders(folder), 2);
        QVERIFY(servers.running(folder));

        servers.release(first, true);
        QCOMPARE(servers.holders(folder), 1);
        QVERIFY(DevServer::answers(a.url, 2000));
        servers.release(second, true);
        QCOMPARE(servers.holders(folder), 0);
        QVERIFY(!servers.running(folder));
        QVERIFY(!DevServer::answers(a.url, 500));
    }

    void aFailedStartReportsItsLineAndHoldsNothing()
    {
        DevServers servers;
        const QString folder = site(QStringLiteral("broken"), QStringLiteral("echo the dev script blew up; exit 3"));
        quint64 lease = 0;
        const DevServers::Result result = acquire(servers, folder, &lease);
        QVERIFY(!result.error.isEmpty());
        QVERIFY2(result.error.contains(QLatin1String("the dev script blew up")), qPrintable(result.error));
        QCOMPARE(servers.holders(folder), 0);
        QVERIFY(!servers.running(folder));
        // Asking again starts afresh.
        const DevServers::Result again = acquire(servers, folder, &lease);
        QVERIFY(!again.error.isEmpty());
    }

    void aFolderWithNothingToRunFails()
    {
        DevServers servers;
        const QString folder = m_directory.filePath(QStringLiteral("empty"));
        QDir().mkpath(folder);
        quint64 lease = 0;
        const DevServers::Result result = acquire(servers, folder, &lease);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(servers.holders(folder), 0);
    }

    void stopAllEndsEveryServer()
    {
        DevServers servers;
        const QString one = site(QStringLiteral("one"));
        const QString two = site(QStringLiteral("two"));
        quint64 a = 0, b = 0;
        const DevServers::Result first = acquire(servers, one, &a);
        const DevServers::Result second = acquire(servers, two, &b);
        QVERIFY(first.error.isEmpty() && second.error.isEmpty());
        QVERIFY(first.url != second.url);
        servers.stopAll();
        QVERIFY(!servers.running(one));
        QVERIFY(!servers.running(two));
        QVERIFY(!DevServer::answers(first.url, 500));
        QVERIFY(!DevServer::answers(second.url, 500));
    }
};

QTEST_GUILESS_MAIN(DevServersTests)
#include "DevServersTests.moc"
