#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Breakpoints.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "UI/BrowserViews.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

// Browser Views on a canvas (docs/BROWSER-VIEW.md, sections 2 and 3): headless Chromium on a throwaway profile, the
// pages from a local server. The tests that need Chromium skip without it.
namespace {
constexpr int patience = 60'000;

struct Rig {
    EditorSession session;
    EditorCanvas canvas{session};
    QUuid frame;

    explicit Rig(const QUrl &url, QRectF rect = {20, 20, 600, 400}, const QImage &picture = {})
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame(rect, QStringLiteral("Site"));
        view.browser = BrowserView{url, {}, picture};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
        canvas.resize(1000, 800);
        canvas.show();
        views()->attach(&canvas);
    }
    BrowserViews *views() { return BrowserViews::of(session); }
    BrowserViews::State state() { return views()->state(frame); }
    const BrowserView &browser() { return *session.document()->find(frame)->browser; }
    bool red(const QImage &image) const
    {
        if (image.isNull())
            return false;
        const QColor color = image.pixelColor(image.width() / 2, image.height() - 10);
        return color.red() > 180 && color.green() < 100 && color.blue() < 100;
    }
};

struct Answer {
    QJsonObject result;
    QString error;
    QEventLoop *loop = nullptr;
};

Answer ask(const QUuid &frame, const QString &method, const QJsonObject &params = {})
{
    auto answer = std::make_shared<Answer>();
    QEventLoop loop;
    answer->loop = &loop;
    // A late reply (the pool stopping) must not reach a loop that is gone.
    BrowserViews::pool()->call(frame, method, params, [answer](const QJsonObject &result, const QString &error) {
        QMetaObject::invokeMethod(qApp, [answer, result, error] {
            answer->result = result;
            answer->error = error;
            if (answer->loop)
                answer->loop->quit();
        }, Qt::QueuedConnection);
    });
    QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
    loop.exec();
    answer->loop = nullptr;
    return *answer;
}

// The page titles of the pool's tabs: the fixture writes its width and scroll into them.
QStringList titles()
{
    QStringList found;
    for (const QJsonValue &each : ask(QUuid(), QStringLiteral("Target.getTargets")).result["targetInfos"].toArray()) {
        if (each["type"].toString() == QLatin1String("page"))
            found << each["title"].toString();
    }
    return found;
}
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class BrowserViewTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;

    QUrl page(const QString &name) const { return QUrl(m_server.url().toString() + name); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        QVERIFY(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/browserview")).isEmpty());
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
    }

    void cleanup()
    {
        BrowserViews::setLiveOpen(false);
        BrowserViews::shutdownPool();
        qunsetenv("OMASTRATOR_CHROMIUM");
    }

    void aFrameStreamsItsPageAtTheFramesWidth()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        QCOMPARE(rig.state(), BrowserViews::State::live);
        QVERIFY(rig.views()->message(rig.frame).isEmpty());
        // One CSS pixel to the point: the page lays out at the frame's 600.
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s0")), 10'000);
        // The canvas draws the live picture.
        const QPointF centre = rig.canvas.documentToView().map(QPointF(320, 340));
        QVERIFY(rig.red(rig.canvas.grab().toImage().copy(QRect(centre.toPoint() - QPoint(5, 5), QSize(10, 20)))));
    }

    void thePagesOwnNavigationIsWrittenBackWithoutAnUndoStep()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("auto.html")));
        const size_t steps = rig.session.undoNames().size();
        QTRY_VERIFY_WITH_TIMEOUT(rig.browser().url.path().endsWith(QLatin1String("second.html")), patience);
        QCOMPARE(rig.session.undoNames().size(), steps);
        QVERIFY(rig.session.isModified());
        // The tab keeps its place: nothing navigates it again.
        QTest::qWait(500);
        QVERIFY(titles().contains(QStringLiteral("Second")));
    }

    void aReopenedFrameScrollsBackToWhereItWas()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        rig.session.setBrowserLocation(rig.frame, rig.browser().url, {0, 120});
        rig.views()->attach(nullptr);
        rig.views()->attach(&rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s120")), patience);
    }

    void theBarKnowsHistoryAndDrivesBackForwardAndReload()
    {
        NEEDS_CHROMIUM;
        using Action = BrowserViewHost::Action;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        QVERIFY(!rig.views()->bar(rig.frame).canGoBack);
        // A site that isn't in the registry is somebody else's.
        QVERIFY(rig.views()->bar(rig.frame).notYours);
        rig.session.setBrowserUrl(rig.frame, page(QStringLiteral("second.html")));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("Second")), patience);
        QTRY_VERIFY_WITH_TIMEOUT(rig.views()->bar(rig.frame).canGoBack, patience);
        QVERIFY(!rig.views()->bar(rig.frame).canGoForward);
        rig.views()->act(rig.frame, Action::back);
        QTRY_VERIFY_WITH_TIMEOUT(rig.browser().url.path().endsWith(QLatin1String("index.html")), patience);
        QTRY_VERIFY_WITH_TIMEOUT(rig.views()->bar(rig.frame).canGoForward, patience);
        rig.views()->act(rig.frame, Action::forward);
        QTRY_VERIFY_WITH_TIMEOUT(rig.browser().url.path().endsWith(QLatin1String("second.html")), patience);
        // Reloading keeps the page and asks for no undo step.
        const size_t steps = rig.session.undoNames().size();
        rig.views()->act(rig.frame, Action::reload);
        rig.views()->act(rig.frame, Action::reloadIgnoringCache);
        QTest::qWait(500);
        QVERIFY(titles().contains(QStringLiteral("Second")));
        QCOMPARE(rig.session.undoNames().size(), steps);
    }

    // Replies to the history and breakpoint questions come on the pool's thread; a document that closes first must not be
    // reached by them. A use-after-free isn't certain to crash, so this is a guard that runs the race at several delays.
    void aDocumentClosedWhileAReplyIsInFlightIsLeftAlone()
    {
        NEEDS_CHROMIUM;
        using Action = BrowserViewHost::Action;
        for (const int delay : {0, 3, 10, 25, 60}) {
            auto rig = std::make_unique<Rig>(page(QStringLiteral("index.html")));
            QTRY_VERIFY_WITH_TIMEOUT(rig->state() == BrowserViews::State::live, patience);
            // Every navigation and load asks Chromium for the history and scans the stylesheets.
            rig->views()->act(rig->frame, Action::reload);
            QTest::qWait(delay);
            rig.reset();
            QTest::qWait(150);
        }
        QVERIFY(BrowserViews::pool());
    }

    void aHiddenCanvasPausesTheFrameAndKeepsItsPicture()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        rig.canvas.hide();
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::paused, 10'000);
        QVERIFY(rig.red(rig.views()->picture(rig.frame)));
        // The last picture went into the document, and no edit was made for it.
        QVERIFY(!rig.browser().picture.isNull());
        QVERIFY(!rig.session.isModified());
        rig.canvas.show();
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, 10'000);
    }

    void aFrameOffScreenNeverOpensATab()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")), {5000, 5000, 600, 400});
        QTest::qWait(500);
        QCOMPARE(rig.state(), BrowserViews::State::closed);
        QVERIFY(!BrowserViews::pool()->isRunning());
        QCOMPARE(BrowserViews::pool()->tabCount(), 0);
    }

    void deletingTheFrameClosesItsTab()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_COMPARE_WITH_TIMEOUT(BrowserViews::pool()->tabCount(), 1, patience);
        rig.session.deleteObjects({rig.frame});
        QTRY_COMPARE_WITH_TIMEOUT(BrowserViews::pool()->tabCount(), 0, 10'000);
        rig.session.undo();
        // The frame comes back on its address, in a new tab.
        QTRY_COMPARE_WITH_TIMEOUT(BrowserViews::pool()->tabCount(), 1, patience);
    }

    void resetPausesEveryFrameAndAClickResumesIt()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        BrowserViews::resetAll();
        QCOMPARE(rig.state(), BrowserViews::State::resetPaused);
        QCOMPARE(rig.views()->message(rig.frame), QStringLiteral("Paused by reset. Click to resume."));
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::pool()->isRunning(), 15'000);
        // Nothing restarts by itself.
        QTest::qWait(500);
        QVERIFY(!BrowserViews::pool()->isRunning());
        QVERIFY(rig.red(rig.views()->picture(rig.frame)));
        rig.session.select({rig.frame});
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, patience);
    }

    void liveInItsWindowMakesTheFramesWait()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, patience);
        BrowserViews::setLiveOpen(true);
        // The profile is free by the time it returns.
        QVERIFY(!BrowserViews::pool()->isRunning());
        QCOMPARE(rig.state(), BrowserViews::State::liveOpen);
        QVERIFY(rig.views()->message(rig.frame).contains(QLatin1String("open for Live")));
        BrowserViews::setLiveOpen(false);
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, patience);
    }

    void aResizePreviewReflowsThePageAndEndsWithoutAStep()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s0")), patience);
        const auto steps = rig.session.undoNames();
        rig.session.select({rig.frame});
        rig.session.beginPreview(QStringLiteral("Preview Width"));
        rig.session.previewFrameBox(rig.frame, {20, 20, 390, 400});
        // The page lays out at 390 wide, as a phone would show it.
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w390s0")), 10'000);
        rig.session.cancelInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s0")), 10'000);
        QVERIFY(rig.session.undoNames() == steps);
        QCOMPARE(rig.session.document()->bounds(rig.frame).width(), 600.0);
    }

    void aNewDesignWidthReflowsThePageAndUndoGivesItBack()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s0")), patience);
        rig.session.select({rig.frame});
        rig.session.beginPreview(QStringLiteral("Preview Width"));
        rig.session.previewFrameBox(rig.frame, {20, 20, 768, 400});
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w768s0")), 10'000);
        rig.session.setPreviewAsDesignWidth();
        QTest::qWait(600);
        QVERIFY(titles().contains(QStringLiteral("w768s0")));
        rig.session.undo();
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600s0")), 10'000);
    }

    void aSiteOfYoursOffersItsOwnBreakpointsAndSomebodyElsesOffersTheDefaults()
    {
        NEEDS_CHROMIUM;
        const QUrl url = page(QStringLiteral("breakpoints.html"));
        {
            Rig rig(url);
            QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, patience);
            QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("w600")), patience);
            QTest::qWait(500);
            QVERIFY(rig.views()->bar(rig.frame).notYours);
            QCOMPARE(rig.views()->breakpoints(rig.frame), Breakpoints::defaults());
        }
        BrowserViews::shutdownPool();
        QVERIFY(ProjectRegistry::remember(url, m_directory.path()).isEmpty());
        Rig rig(url);
        QTRY_COMPARE_WITH_TIMEOUT(rig.views()->breakpoints(rig.frame), (QList<int>{768, 1024, 1280}), patience);
        QVERIFY(!rig.views()->bar(rig.frame).notYours);
        QVERIFY(ProjectRegistry::forget(url).isEmpty());
    }

    void aFrameShownBelow160PxPausesAndKeepsItsPicture()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        // 600 points at a tenth of the zoom are 60 pixels: a thumbnail.
        rig.session.zoomToRect(QRectF(0, 0, 10000, 8000));
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::paused, 10'000);
        QVERIFY(rig.red(rig.views()->picture(rig.frame)));
        rig.session.zoomToRect(QRectF(0, 0, 1000, 800));
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, 10'000);
    }

    void aFrameOnAnotherPagePausesAndComesBack()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.red(rig.views()->picture(rig.frame)), patience);
        const QUuid first = rig.session.document()->currentPageId();
        rig.session.setCurrentPage(rig.session.addPage(QStringLiteral("Other")));
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::paused, 10'000);
        rig.session.setCurrentPage(first);
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::live, 10'000);
    }

    void closingTheDocumentClosesEveryTab()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        QTRY_COMPARE_WITH_TIMEOUT(BrowserViews::pool()->tabCount(), 1, patience);
        rig.session.loadDocument(VectorDocument::blank({1000, 800}));
        QTRY_COMPARE_WITH_TIMEOUT(BrowserViews::pool()->tabCount(), 0, 10'000);
    }

    void withoutChromiumTheFrameShowsItsLastPicture()
    {
        qputenv("OMASTRATOR_CHROMIUM", "/nonexistent/chromium");
        QImage picture(600, 400, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::red);
        Rig rig(page(QStringLiteral("index.html")), {20, 20, 600, 400}, picture);
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(), BrowserViews::State::unavailable, 5000);
        QCOMPARE(rig.views()->message(rig.frame), QStringLiteral("Chromium isn't installed, so this shows the last picture."));
        QVERIFY(rig.views()->picture(rig.frame).isNull());
        const QPointF centre = rig.canvas.documentToView().map(QPointF(320, 340));
        QVERIFY(rig.red(rig.canvas.grab().toImage().copy(QRect(centre.toPoint() - QPoint(5, 5), QSize(10, 20)))));
        QVERIFY(!BrowserViews::pool()->isRunning());
    }

    void aFailedLoadKeepsTheAddressThatWasTyped()
    {
        NEEDS_CHROMIUM;
        quint16 port = 0;
        {
            QTcpServer taken;
            QVERIFY(taken.listen(QHostAddress::LocalHost));
            port = taken.serverPort();
        }
        const QUrl typed(QStringLiteral("http://127.0.0.1:%1/down").arg(port));
        Rig rig(typed);
        const size_t steps = rig.session.undoNames().size();
        // Chromium shows its own error page for it and reports chrome-error://chromewebdata/.
        QTRY_VERIFY_WITH_TIMEOUT(!rig.views()->picture(rig.frame).isNull(), patience);
        QTest::qWait(500);
        QCOMPARE(rig.browser().url, typed);
        QCOMPARE(rig.session.undoNames().size(), steps);
        QVERIFY(!rig.session.isModified());
    }

    void aFrameWithoutAnAddressSaysSo()
    {
        Rig rig{QUrl()};
        QTest::qWait(100);
        QCOMPARE(rig.views()->message(rig.frame), QStringLiteral("No page yet."));
        QCOMPARE(rig.state(), BrowserViews::State::closed);
    }
};

QTEST_MAIN(BrowserViewTests)
#include "BrowserViewTests.moc"
