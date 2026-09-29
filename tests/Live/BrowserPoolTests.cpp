#include "Agent/BrowserPoolState.h"
#include "Live/BrowserPool.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <mutex>

// BrowserPool (docs/BROWSER-VIEW.md) in headless Chromium on a throwaway profile; skips without Chromium.
namespace {
struct Answer {
    QJsonObject result;
    QString error;
    QThread *thread = nullptr;
};

// Asks the pool and waits, without blocking the pool's thread.
Answer ask(BrowserPool &pool, const QUuid &frame, const QString &method, const QJsonObject &params = {})
{
    auto answer = std::make_shared<Answer>();
    QEventLoop loop;
    pool.call(frame, method, params, [answer, &loop](const QJsonObject &result, const QString &error) {
        answer->result = result;
        answer->error = error;
        answer->thread = QThread::currentThread();
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
    loop.exec();
    return *answer;
}

int pageTargets(BrowserPool &pool)
{
    int count = 0;
    for (const QJsonValue &each : ask(pool, QUuid(), QStringLiteral("Target.getTargets")).result["targetInfos"].toArray())
        count += each["type"].toString() == QLatin1String("page");
    return count;
}
}

class BrowserPoolTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    BrowserPool::Options options(int idleMs = 60'000, int maxTabs = 8) const
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        options.idleMs = idleMs;
        options.maxTabs = maxTabs;
        return options;
    }

    // Waits for `count` opened() signals.
    static bool opened(QSignalSpy &spy, int count = 1) { return spy.wait(60'000) && (spy.count() >= count || spy.wait(60'000)); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qRegisterMetaType<BrowserPool::CloseReason>();
    }

    void startsOnlyWhenAFrameNeedsATab()
    {
        BrowserPool pool(options());
        QTest::qWait(200);
        QVERIFY(!pool.isRunning());
        QVERIFY(!QFileInfo::exists(BrowserPoolState::path()));
        QSignalSpy opened(&pool, &BrowserPool::opened);
        pool.open(QUuid::createUuid());
        QVERIFY(opened.wait(60'000));
        QVERIFY(pool.isRunning());
        QCOMPARE(pool.tabCount(), 1);
    }

    void eachOpenIsOneTargetAndCloseRemovesIt()
    {
        BrowserPool pool(options());
        QSignalSpy opened(&pool, &BrowserPool::opened);
        QSignalSpy closed(&pool, &BrowserPool::closed);
        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid();
        pool.open(a);
        pool.open(b);
        // Opening a frame that has a tab does nothing.
        pool.open(a);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 60'000);
        QCOMPARE(pool.tabCount(), 2);
        // The starting tab (about:blank) is closed once ours are up, so the pages are the two.
        QTRY_COMPARE_WITH_TIMEOUT(pageTargets(pool), 2, 10'000);
        const int before = pageTargets(pool);
        pool.close(a);
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(1).value<BrowserPool::CloseReason>(), BrowserPool::CloseReason::closed);
        QCOMPARE(pool.tabCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(pageTargets(pool), before - 1, 10'000);
    }

    void aTabTakesCommandsOnThePoolsThread()
    {
        BrowserPool pool(options());
        QSignalSpy opened(&pool, &BrowserPool::opened);
        const QUuid frame = QUuid::createUuid();
        pool.open(frame);
        QVERIFY(opened.wait(60'000));
        const Answer answer = ask(pool, frame, QStringLiteral("Runtime.evaluate"), {{"expression", "1 + 2"}});
        QVERIFY2(answer.error.isEmpty(), qPrintable(answer.error));
        QCOMPARE(answer.result["result"]["value"].toInt(), 3);
        QCOMPARE(answer.thread, pool.poolThread());
        QVERIFY(pool.poolThread() != QThread::currentThread());
        // A frame with no tab is told so.
        QVERIFY(!ask(pool, QUuid::createUuid(), QStringLiteral("Runtime.evaluate")).error.isEmpty());
    }

    void pastTheCapThePausedTabShownLeastRecentlyGoes()
    {
        BrowserPool pool(options(60'000, 3));
        QSignalSpy opened(&pool, &BrowserPool::opened);
        QSignalSpy closed(&pool, &BrowserPool::closed);
        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid(), c = QUuid::createUuid(), d = QUuid::createUuid();
        for (const QUuid &frame : {a, b, c})
            pool.open(frame);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 3, 60'000);
        // b was paused before c, and a is still on screen: b is the one to go.
        pool.setShown(b, false);
        pool.setShown(c, false);
        pool.open(d);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 4, 30'000);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().at(0).toUuid(), b);
        QCOMPARE(closed.first().at(1).value<BrowserPool::CloseReason>(), BrowserPool::CloseReason::evicted);
        QCOMPARE(pool.tabCount(), 3);
    }

    void opensThatArriveTogetherStayWithinTheCap()
    {
        const int cap = 3;
        BrowserPool pool(options(60'000, cap));
        QSignalSpy closed(&pool, &BrowserPool::closed);
        // More than the cap, all before the browser is up: the case of a file with a page of Browser Views.
        for (int i = 0; i < cap + 2; ++i)
            pool.open(QUuid::createUuid());
        QElapsedTimer timer;
        timer.start();
        int most = 0;
        while (timer.elapsed() < 90'000 && !(closed.count() == 2 && pool.tabCount() == cap)) {
            most = std::max(most, pool.tabCount());
            QTest::qWait(10);
        }
        QTest::qWait(500);
        most = std::max(most, pool.tabCount());
        QCOMPARE(closed.count(), 2);
        QCOMPARE(pool.tabCount(), cap);
        QVERIFY2(most <= cap, qPrintable(QString::number(most)));
        // Chromium's own starting tab is closed, so the pages are ours alone.
        QTRY_COMPARE_WITH_TIMEOUT(pageTargets(pool), cap, 10'000);
    }

    void aFrameClosedWhileTheBrowserStartsGetsNoTab()
    {
        BrowserPool pool(options(60'000));
        QSignalSpy opened(&pool, &BrowserPool::opened);
        QSignalSpy started(&pool, &BrowserPool::started);
        const QUuid frame = QUuid::createUuid();
        pool.open(frame);
        pool.close(frame);
        QVERIFY(started.wait(60'000));
        QTest::qWait(1000);
        QCOMPARE(opened.count(), 0);
        QCOMPARE(pool.tabCount(), 0);
        // Chromium's own starting tab is closed too.
        QTRY_VERIFY_WITH_TIMEOUT(pageTargets(pool) <= 0, 10'000);
    }

    void stopsWhenIdleAndClearsItsFile()
    {
        BrowserPool pool(options(600));
        QSignalSpy opened(&pool, &BrowserPool::opened);
        QSignalSpy stopped(&pool, &BrowserPool::stopped);
        const QUuid frame = QUuid::createUuid();
        pool.open(frame);
        QVERIFY(opened.wait(60'000));
        const BrowserPoolState::State state = BrowserPoolState::read();
        QCOMPARE(state.pid, pool.processId());
        QCOMPARE(state.profile, m_directory.filePath(QStringLiteral("profile")));
        QVERIFY(state.pid > 0);
        // Not while a tab is open.
        QTest::qWait(1500);
        QVERIFY(pool.isRunning());
        pool.close(frame);
        QVERIFY(stopped.wait(15'000));
        QVERIFY(!pool.isRunning());
        QVERIFY(BrowserPoolState::read().isEmpty());
        QVERIFY(!QFileInfo::exists(QStringLiteral("/proc/%1/cmdline").arg(state.pid)) || QFile(QStringLiteral("/proc/%1/cmdline").arg(state.pid)).size() == 0);
        // And it starts again when a frame needs it.
        pool.open(QUuid::createUuid());
        QVERIFY(opened.wait(60'000));
        QVERIFY(pool.isRunning());
    }

    void resetClosesEveryTabAndStopsWithoutRestarting()
    {
        BrowserPool pool(options());
        QSignalSpy opened(&pool, &BrowserPool::opened);
        QSignalSpy closed(&pool, &BrowserPool::closed);
        QSignalSpy stopped(&pool, &BrowserPool::stopped);
        pool.open(QUuid::createUuid());
        pool.open(QUuid::createUuid());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 60'000);
        pool.closeAll();
        QVERIFY(stopped.wait(15'000));
        QCOMPARE(closed.count(), 2);
        for (const QList<QVariant> &each : closed)
            QCOMPARE(each.at(1).value<BrowserPool::CloseReason>(), BrowserPool::CloseReason::reset);
        QTest::qWait(500);
        QVERIFY(!pool.isRunning());
        QCOMPARE(pool.tabCount(), 0);
    }

    void aBrowserThatIsMissingFailsTheOpen()
    {
        qputenv("OMASTRATOR_CHROMIUM", "/nonexistent/chromium");
        BrowserPool pool(options());
        QSignalSpy failed(&pool, &BrowserPool::openFailed);
        pool.open(QUuid::createUuid());
        QVERIFY(failed.wait(15'000));
        qunsetenv("OMASTRATOR_CHROMIUM");
        QVERIFY(!pool.isRunning());
        QVERIFY(!failed.first().at(1).toString().isEmpty());
    }
};

QTEST_MAIN(BrowserPoolTests)
#include "BrowserPoolTests.moc"
