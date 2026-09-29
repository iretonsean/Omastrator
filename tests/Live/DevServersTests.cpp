#include "Live/DevServers.h"
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

// The shared dev servers (docs/LIVE-IN-FRAME.md): one process per folder, held by leases. Static sites and a failing
// override stand in for real dev servers, so no Chromium or node is needed.
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

private slots:
    void initTestCase() { QVERIFY(m_directory.isValid()); }

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
