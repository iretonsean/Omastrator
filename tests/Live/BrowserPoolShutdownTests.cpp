#include "Live/BrowserPool.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <csignal>

// Deleting a BrowserPool while its browser is still starting (docs/BROWSER-VIEW.md): a fake Chromium that never opens
// DevTools stands in, so this needs no real one.
class BrowserPoolShutdownTests : public QObject {
    Q_OBJECT

private slots:
    void aPoolDeletedWhileTheBrowserStartsEndsAtOnceAndKillsIt()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString script = directory.filePath(QStringLiteral("chromium"));
        const QString pidFile = directory.filePath(QStringLiteral("pid"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("#!/bin/sh\necho $$ > '%1'\nexec sleep 60\n").arg(pidFile).toUtf8());
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        qputenv("OMASTRATOR_CHROMIUM", script.toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", directory.filePath(QStringLiteral("runtime")).toUtf8());

        BrowserPool::Options options;
        options.profile = directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        options.writeState = false;
        auto *pool = new BrowserPool(options);
        pool->open(QUuid::createUuid());
        // The start is waiting in its event loops once the fake has said who it is.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFile), 10'000);
        QFile pidRead(pidFile);
        QVERIFY(pidRead.open(QIODevice::ReadOnly));
        QTRY_VERIFY_WITH_TIMEOUT(!pidRead.readAll().trimmed().isEmpty() || (pidRead.seek(0), false), 5'000);
        pidRead.seek(0);
        const qint64 pid = pidRead.readAll().trimmed().toLongLong();
        QVERIFY(pid > 0);

        QElapsedTimer timer;
        timer.start();
        delete pool;
        QVERIFY2(timer.elapsed() < 5'000, qPrintable(QString::number(timer.elapsed())));
        // The process is gone (or a zombie awaiting its parent, which is the pool's own QProcess: reaped by then).
        QTRY_VERIFY_WITH_TIMEOUT(::kill(static_cast<pid_t>(pid), 0) != 0, 3'000);
        qunsetenv("OMASTRATOR_CHROMIUM");
    }
};

QTEST_MAIN(BrowserPoolShutdownTests)
#include "BrowserPoolShutdownTests.moc"
