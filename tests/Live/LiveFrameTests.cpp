#include "Live/BrowserPool.h"
#include "Live/LiveSession.h"
#include "Live/StaticServer.h"
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Live in a Browser View's tab (docs/LIVE-IN-FRAME.md): the session lives on the pool's thread, attaches to the frame's
// tab and follows it. Headless Chromium on a throwaway profile; skips without Chromium.
class LiveFrameTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;
    BrowserPool *m_pool = nullptr;
    LiveSession *m_live = nullptr;
    QUuid m_frame;

    // Runs `work` on the pool's thread, where the session lives, and waits.
    template <typename F>
    void onPool(F work)
    {
        QMetaObject::invokeMethod(m_live, work, Qt::BlockingQueuedConnection);
    }

    QString computed(const QString &selector, const QString &property)
    {
        QString value;
        onPool([&] {
            value = m_live->evaluate(QStringLiteral("getComputedStyle(document.querySelector('%1')).getPropertyValue('%2')").arg(selector, property)).toString();
        });
        return value;
    }

    bool waitRunning()
    {
        for (int i = 0; i < 1200; ++i) {
            QTest::qWait(50);
            LiveSession::State state = LiveSession::State::off;
            onPool([&] { state = m_live->state(); });
            if (state == LiveSession::State::running)
                return true;
            if (state == LiveSession::State::failed)
                return false;
        }
        return false;
    }

    void navigate(const QString &path)
    {
        QEventLoop loop;
        QUrl url = m_server.url();
        url.setPath(path);
        m_pool->call(m_frame, QStringLiteral("Page.navigate"), {{"url", url.toString()}}, [&](const QJsonObject &, const QString &) {
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
        QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    void openFrame()
    {
        QSignalSpy opened(m_pool, &BrowserPool::opened);
        m_pool->open(m_frame);
        QVERIFY(opened.wait(60'000));
    }

private slots:
    void initTestCase()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qRegisterMetaType<BrowserPool::CloseReason>();
        QVERIFY(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/liveframe")).isEmpty());
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        m_pool = new BrowserPool(options);
    }

    void cleanupTestCase()
    {
        delete m_live;
        m_live = nullptr;
        delete m_pool;
        m_pool = nullptr;
    }

    void init()
    {
        if (!m_pool)
            QSKIP("Chromium isn't installed.");
        m_frame = QUuid::createUuid();
        m_live = new LiveSession;
        m_live->moveToThread(m_pool->poolThread());
    }

    void cleanup()
    {
        if (!m_live)
            return;
        onPool([&] { m_live->stop(); });
        m_pool->close(m_frame);
        // Deleted on its own thread.
        QMetaObject::invokeMethod(m_live, &QObject::deleteLater, Qt::QueuedConnection);
        m_live = nullptr;
    }

    void attachesToTheFramesTabOnThePoolThread()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        QString failure;
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { failure = m_live->start(target); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(waitRunning());
        bool inFrame = false;
        QThread *thread = nullptr;
        bool mockup = false;
        onPool([&] {
            inFrame = m_live->inFrame();
            thread = QThread::currentThread();
            mockup = m_live->isMockup();
        });
        QVERIFY(inFrame);
        QCOMPARE(thread, m_pool->poolThread());
        QVERIFY(mockup);
        // The overlay is in the page, quiet, and the page isn't taken over.
        QCOMPARE(m_live->thread(), m_pool->poolThread());
        QString state;
        onPool([&] { state = m_live->evaluate(QStringLiteral("document.getElementById('omastrator-overlay') ? 'drawn' : 'quiet'")).toString(); });
        QCOMPARE(state, QStringLiteral("quiet"));
    }

    void anEditChangesTheComputedStyle()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        QString failure = QStringLiteral("unset");
        onPool([&] { failure = m_live->edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(computed(QStringLiteral("#title"), QStringLiteral("color")), QStringLiteral("rgb(225, 29, 72)"));
        size_t edits = 0;
        QString token;
        onPool([&] {
            edits = m_live->edits().size();
            token = m_live->edits().front().token;
        });
        QCOMPARE(edits, size_t(1));
        QCOMPARE(token, QStringLiteral("--brand"));
    }

    // Live reports running once the tab is attached, a moment before the page's tokens are scanned. Looking without a pause,
    // the first look that finds it running is inside that attach, so this edit comes in before the scan and still snaps.
    void anEditMadeTheMomentLiveRunsSnapsToTheTokens()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        bool edited = false;
        QString failure;
        for (int i = 0; i < 200'000 && !edited && failure != QLatin1String("Live failed to start."); ++i) {
            onPool([&] {
                if (m_live->state() == LiveSession::State::running) {
                    // Before the overlay is in, the page can't take an edit yet: the next look tries again.
                    failure = m_live->edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a"));
                    edited = failure.isEmpty();
                } else if (m_live->state() == LiveSession::State::failed) {
                    failure = QStringLiteral("Live failed to start.");
                }
            });
        }
        QVERIFY2(edited, qPrintable(failure));
        QCOMPARE(computed(QStringLiteral("#title"), QStringLiteral("color")), QStringLiteral("rgb(225, 29, 72)"));
        QString token;
        onPool([&] { token = m_live->edits().front().token; });
        QCOMPARE(token, QStringLiteral("--brand"));
    }

    // A Save clears what it wrote with a call posted to the session. If it runs while an undo or a redo waits on the page,
    // both stacks are empty by the time the answer comes; the step being undone is not on them, and comes back to nothing.
    void aClearThatRunsInsideAnUndoOrRedoIsSurvived()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        struct After {
            QString failure = QStringLiteral("unset");
            bool canUndo = true, canRedo = true;
            size_t edits = 99;
        } undone, redone, gone;
        const auto look = [this](After &after) {
            after.canUndo = m_live->canUndoEdit();
            after.canRedo = m_live->canRedoEdit();
            after.edits = m_live->edits().size();
        };
        onPool([&] {
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("opacity"), QStringLiteral("0.5")).isEmpty());
            const std::vector<LiveEdit> taken = m_live->edits();
            // Posted now, it runs in the event loop the undo waits in for the page's answer.
            QMetaObject::invokeMethod(m_live, [this, taken] { m_live->removeEdits(taken); }, Qt::QueuedConnection);
            undone.failure = m_live->undoEdit();
            look(undone);
        });
        QVERIFY2(undone.failure.isEmpty(), qPrintable(undone.failure));
        QVERIFY(!undone.canUndo && !undone.canRedo);
        QCOMPARE(undone.edits, size_t(0));

        onPool([&] {
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("opacity"), QStringLiteral("0.4")).isEmpty());
            QVERIFY(m_live->undoEdit().isEmpty());
            QVERIFY(m_live->canRedoEdit());
            QMetaObject::invokeMethod(m_live, [this] { m_live->setEdits({}); }, Qt::QueuedConnection);
            redone.failure = m_live->redoEdit();
            look(redone);
        });
        QVERIFY2(redone.failure.isEmpty(), qPrintable(redone.failure));
        QVERIFY(!redone.canUndo && !redone.canRedo);
        QCOMPARE(redone.edits, size_t(0));

        // An undo the page can't take leaves the step where it was.
        onPool([&] {
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("opacity"), QStringLiteral("0.3")).isEmpty());
            m_live->evaluate(QStringLiteral("document.getElementById('title').remove()"));
            gone.failure = m_live->undoEdit();
            look(gone);
        });
        QVERIFY(!gone.failure.isEmpty());
        QVERIFY(gone.canUndo);
        QCOMPARE(gone.edits, size_t(1));
    }

    // A frame's picture is a screencast of the page, so anything the overlay drew would be in the design.
    void noLiveChromeIsDrawnInThePage()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        auto page = [&] {
            QString html;
            onPool([&] { html = m_live->evaluate(QStringLiteral("document.documentElement.outerHTML")).toString(); });
            return html;
        };
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        onPool([&] {
            m_live->setPageEditing(true);
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty());
            m_live->evaluate(QStringLiteral("window.__oma.select('#title', false)"));
        });
        QTest::qWait(300);
        QString html = page();
        // The edit is an inline style on the heading; nothing else was added or wrapped.
        QVERIFY(!html.contains(QLatin1String("omastrator")));
        int elements = 0;
        onPool([&] { elements = m_live->evaluate(QStringLiteral("document.querySelectorAll('*').length")).toInt(); });
        QCOMPARE(elements, 10);
        // The selection is reported as geometry instead.
        QJsonObject geometry;
        QTRY_VERIFY_WITH_TIMEOUT((onPool([&] { geometry = m_live->geometry(); }), geometry["selection"].toArray().size() == 1), 10'000);
        QCOMPARE(geometry["selection"].toArray()[0].toObject()["selector"].toString(), QStringLiteral("#title"));
    }

    void aReopenedTabAttachesAgainWithTheEditsBack()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        onPool([&] { QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty()); });

        // The tab goes (the browser is reset, or evicted) and the frame opens another.
        m_pool->close(m_frame);
        for (int i = 0; i < 100; ++i) {
            QTest::qWait(50);
            LiveSession::State state = LiveSession::State::running;
            onPool([&] { state = m_live->state(); });
            if (state == LiveSession::State::starting)
                break;
        }
        LiveSession::State state = LiveSession::State::running;
        onPool([&] { state = m_live->state(); });
        QCOMPARE(state, LiveSession::State::starting);
        openFrame();
        navigate(QStringLiteral("/index.html"));
        QVERIFY(waitRunning());
        QTRY_COMPARE_WITH_TIMEOUT(computed(QStringLiteral("#title"), QStringLiteral("color")), QStringLiteral("rgb(225, 29, 72)"), 20'000);
        size_t edits = 0;
        onPool([&] { edits = m_live->edits().size(); });
        QCOMPARE(edits, size_t(1));
    }

    void undoAndRedoRoundTripTheStyleClassAndText()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        const QString original = computed(QStringLiteral("#title"), QStringLiteral("color"));
        QString text;
        auto titleText = [&] {
            onPool([&] { text = m_live->evaluate(QStringLiteral("document.getElementById('title').textContent")).toString(); });
            return text;
        };
        auto classes = [&] {
            QString value;
            onPool([&] { value = m_live->evaluate(QStringLiteral("document.querySelector('.card').className")).toString(); });
            return value;
        };
        onPool([&] {
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty());
            QVERIFY(m_live->edit(QStringLiteral("#title"), QStringLiteral("text"), QStringLiteral("Changed")).isEmpty());
            QVERIFY(m_live->edit(QStringLiteral(".card"), QStringLiteral("opacity"), QStringLiteral("0.5")).isEmpty());
            QVERIFY(m_live->canUndoEdit());
            QVERIFY(!m_live->canRedoEdit());
        });
        QCOMPARE(titleText(), QStringLiteral("Changed"));
        QCOMPARE(computed(QStringLiteral(".card"), QStringLiteral("opacity")), QStringLiteral("0.5"));

        // Newest first: padding, then text, then colour.
        onPool([&] { QVERIFY(m_live->undoEdit().isEmpty()); });
        QCOMPARE(computed(QStringLiteral(".card"), QStringLiteral("opacity")), QStringLiteral("1"));
        QCOMPARE(classes(), QStringLiteral("card u-card"));
        onPool([&] { QVERIFY(m_live->undoEdit().isEmpty()); });
        QCOMPARE(titleText(), QStringLiteral("Hello from a frame"));
        onPool([&] { QVERIFY(m_live->undoEdit().isEmpty()); });
        QCOMPARE(computed(QStringLiteral("#title"), QStringLiteral("color")), original);
        size_t edits = 99;
        bool canUndo = true;
        onPool([&] {
            edits = m_live->edits().size();
            canUndo = m_live->canUndoEdit();
        });
        QCOMPARE(edits, size_t(0));
        QVERIFY(!canUndo);

        onPool([&] { QVERIFY(m_live->redoEdit().isEmpty()); });
        QCOMPARE(computed(QStringLiteral("#title"), QStringLiteral("color")), QStringLiteral("rgb(225, 29, 72)"));
        onPool([&] { QVERIFY(m_live->redoEdit().isEmpty()); });
        QCOMPARE(titleText(), QStringLiteral("Changed"));
        onPool([&] { QVERIFY(m_live->redoEdit().isEmpty()); });
        QCOMPARE(computed(QStringLiteral(".card"), QStringLiteral("opacity")), QStringLiteral("0.5"));
        onPool([&] { edits = m_live->edits().size(); });
        QCOMPARE(edits, size_t(3));
        // A new edit forgets what was undone.
        onPool([&] {
            QVERIFY(m_live->undoEdit().isEmpty());
            QVERIFY(m_live->canRedoEdit());
            QVERIFY(m_live->edit(QStringLiteral(".card"), QStringLiteral("opacity"), QStringLiteral("0.25")).isEmpty());
            QVERIFY(!m_live->canRedoEdit());
        });
    }

    void stopTakesTheOverlayOutAndLeavesThePageAlone()
    {
        openFrame();
        navigate(QStringLiteral("/index.html"));
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        onPool([&] { QVERIFY(m_live->start(target).isEmpty()); });
        QVERIFY(waitRunning());
        onPool([&] { m_live->stop(); });
        QString present;
        QEventLoop loop;
        m_pool->call(m_frame, QStringLiteral("Runtime.evaluate"), {{"expression", "String(!!window.__oma)"}, {"returnByValue", true}},
                     [&](const QJsonObject &result, const QString &) {
                         present = result["result"]["value"].toString();
                         QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
                     });
        QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
        loop.exec();
        QCOMPARE(present, QStringLiteral("false"));
        // The tab is still the frame's.
        QCOMPARE(m_pool->tabCount(), 1);
    }
};

QTEST_GUILESS_MAIN(LiveFrameTests)
#include "LiveFrameTests.moc"
