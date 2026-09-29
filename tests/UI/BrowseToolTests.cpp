#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/StaticServer.h"
#include "UI/BrowserViews.h"
#include <QAction>
#include <QEventLoop>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <memory>

// The Browse tool (docs/BROWSER-VIEW.md, section 5). Most cases talk to a fake host and need no Chromium; the last few
// drive a real headless page on a throwaway profile from a local server, and skip without Chromium.
namespace {
constexpr int patience = 60'000;

class FakeHost : public BrowserViewHost {
public:
    struct Call {
        QUuid frame;
        QString method;
        QJsonObject params;
    };
    QImage picture(const QUuid &) const override { return {}; }
    QString message(const QUuid &) const override { return {}; }
    bool dispatch(const QUuid &frame, const QString &method, const QJsonObject &params) override
    {
        calls.push_back({frame, method, params});
        return accepts;
    }
    QList<Call> of(const QString &method, const QString &type = {}) const
    {
        QList<Call> found;
        for (const Call &call : calls) {
            if (call.method == method && (type.isEmpty() || call.params[QStringLiteral("type")].toString() == type))
                found << call;
        }
        return found;
    }

    QList<Call> calls;
    bool accepts = true;
};

struct Rig {
    EditorSession session;
    EditorCanvas canvas{session};
    FakeHost host;
    QUuid frame;

    explicit Rig(const QUrl &url = QUrl(QStringLiteral("https://example.com/a")))
    {
        session.loadDocument(VectorDocument::blank({4000, 3000}));
        canvas.resize(1000, 800);
        canvas.show();
        session.zoomToRect(QRectF(50, 100, 900, 700));
        canvas.setBrowserViewHost(&host);
        frame = session.addBrowserView({100, 200, 600, 400}, url);
        session.deselectAll();
        session.selectTool(Tool::browse);
    }
    QPoint view(QPointF document) const { return canvas.documentToView().map(document).toPoint(); }
    QPoint inFrame() const { return view({300, 400}); }
    QPoint offFrame() const { return view({1500, 1500}); }
};

void click(QWidget *widget, QPoint at) { QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, at); }

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

class BrowseToolTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;

    QUrl page(const QString &name) const { return QUrl(m_server.url().toString() + name); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        QVERIFY(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/browse")).isEmpty());
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
    }

    void aClickReachesThePageInItsOwnPointsWithoutSelectingAnything()
    {
        Rig rig;
        click(&rig.canvas, rig.view({100 + 30, 200 + 50}));
        const auto moves = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseMoved"));
        const auto presses = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mousePressed"));
        const auto releases = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseReleased"));
        QCOMPARE(presses.size(), 1);
        QCOMPARE(releases.size(), 1);
        QVERIFY(!moves.isEmpty());
        QCOMPARE(presses[0].frame, rig.frame);
        QCOMPARE(presses[0].params["button"].toString(), QStringLiteral("left"));
        QCOMPARE(presses[0].params["clickCount"].toInt(), 1);
        QVERIFY(qAbs(presses[0].params["x"].toDouble() - 30) < 1.5);
        QVERIFY(qAbs(presses[0].params["y"].toDouble() - 50) < 1.5);
        QVERIFY(rig.session.selection().empty());
    }

    void aDragHoldsTheButtonDownAndReleasesWhereItEnds()
    {
        Rig rig;
        QTest::mousePress(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.view({150, 250}));
        QTest::mouseMove(&rig.canvas, rig.view({200, 300}));
        QTest::mouseRelease(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.view({250, 320}));
        const auto releases = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseReleased"));
        QCOMPARE(releases.size(), 1);
        QVERIFY(qAbs(releases[0].params["x"].toDouble() - 150) < 1.5);
        QVERIFY(qAbs(releases[0].params["y"].toDouble() - 120) < 1.5);
        bool held = false;
        for (const auto &call : rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseMoved")))
            held = held || call.params["buttons"].toInt() == 1;
        QVERIFY(held);
    }

    void aDoubleClickCountsTwo()
    {
        Rig rig;
        const QPoint at = rig.inFrame();
        click(&rig.canvas, at);
        QTest::mouseDClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, at);
        const auto presses = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mousePressed"));
        QVERIFY(presses.size() >= 2);
        QCOMPARE(presses.last().params["clickCount"].toInt(), 2);
    }

    void emptyCanvasPansAndTheMovesDoNotReachAPage()
    {
        Rig rig;
        const QTransform before = rig.canvas.documentToView();
        QTest::mousePress(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.offFrame());
        QTest::mouseMove(&rig.canvas, rig.offFrame() + QPoint(40, 30));
        QTest::mouseRelease(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.offFrame() + QPoint(40, 30));
        QVERIFY(rig.canvas.documentToView() != before);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mousePressed")).isEmpty());
    }

    void theWheelScrollsThePageAndCtrlStillZoomsTheCanvas()
    {
        Rig rig;
        const QPointF at = rig.inFrame();
        QWheelEvent plain(at, rig.canvas.mapToGlobal(at), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&rig.canvas, &plain);
        const auto wheels = rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseWheel"));
        QCOMPARE(wheels.size(), 1);
        QCOMPARE(wheels[0].frame, rig.frame);
        QVERIFY(wheels[0].params["deltaY"].toDouble() > 0);

        const QTransform before = rig.canvas.documentToView();
        QWheelEvent zoom(at, rig.canvas.mapToGlobal(at), {}, QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&rig.canvas, &zoom);
        QCOMPARE(rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseWheel")).size(), 1);
        QVERIFY(rig.canvas.documentToView() != before);
    }

    void keysGoToThePageOnlyAfterAClickPutsTheFocusThere()
    {
        Rig rig;
        // Before any click the keys are the canvas's own: A picks Direct Selection, and Browse is picked again.
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
        rig.session.selectTool(Tool::browse);
        click(&rig.canvas, rig.inFrame());
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        const auto keys = rig.host.of(QStringLiteral("Input.dispatchKeyEvent"));
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys[0].params["type"].toString(), QStringLiteral("keyDown"));
        QCOMPARE(keys[0].params["text"].toString(), QStringLiteral("a"));
        QCOMPARE(keys[1].params["type"].toString(), QStringLiteral("keyUp"));
        // Tool letters don't switch tools while a page has the keys.
        QTest::keyClick(&rig.canvas, Qt::Key_V);
        QCOMPARE(rig.session.tool(), Tool::browse);
    }

    void aClickOnEmptyCanvasTakesTheKeysBack()
    {
        Rig rig;
        click(&rig.canvas, rig.inFrame());
        click(&rig.canvas, rig.offFrame());
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
    }

    void escapeLeavesBrowseAndCtrlKStaysThePalettes()
    {
        Rig rig;
        click(&rig.canvas, rig.inFrame());
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_K, Qt::ControlModifier);
        // The test harness presses Ctrl itself first; the K is what must stay out of the page.
        for (const auto &call : rig.host.of(QStringLiteral("Input.dispatchKeyEvent")))
            QVERIFY(call.params["code"].toString() != QLatin1String("KeyK"));
        QCOMPARE(rig.session.tool(), Tool::browse);
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_Escape);
        QCOMPARE(rig.session.tool(), Tool::select);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
    }

    void thePalettesLiveShortcutFiresAndIsNeverSentToThePage()
    {
        Rig rig;
        int opened = 0;
        QAction palette(&rig.canvas);
        palette.setObjectName(QStringLiteral("commandPalette"));
        palette.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
        rig.canvas.addAction(&palette);
        QObject::connect(&palette, &QAction::triggered, [&] { ++opened; });
        QVERIFY(QTest::qWaitForWindowActive(&rig.canvas));
        click(&rig.canvas, rig.inFrame());
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(opened, 1);
        for (const auto &call : rig.host.of(QStringLiteral("Input.dispatchKeyEvent")))
            QVERIFY(call.params["code"].toString() != QLatin1String("KeyK"));
        // Remapped: the new key opens it, and the old one belongs to the page.
        palette.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_J, Qt::ControlModifier);
        QCOMPARE(opened, 2);
        QTest::keyClick(&rig.canvas, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(opened, 2);
        bool sentK = false;
        for (const auto &call : rig.host.of(QStringLiteral("Input.dispatchKeyEvent")))
            sentK = sentK || call.params["code"].toString() == QLatin1String("KeyK");
        QVERIFY(sentK);
    }

    void leavingBrowseLetsGoOfTheButtonAndTheFocus()
    {
        Rig rig;
        QTest::mousePress(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.inFrame());
        rig.session.selectTool(Tool::select);
        QCOMPARE(rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mouseReleased")).size(), 1);
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
    }

    void aPageThatCantTakeAClickHoldsNothing()
    {
        Rig rig;
        rig.host.accepts = false;
        click(&rig.canvas, rig.inFrame());
        rig.host.calls.clear();
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
    }

    void aHiddenFrameIsNotBrowsed()
    {
        Rig rig;
        rig.session.setVisible(rig.frame, false);
        click(&rig.canvas, rig.inFrame());
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchMouseEvent"), QStringLiteral("mousePressed")).isEmpty());
    }

    void clickingAPageWorksWithNothingSelected()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        BrowserViews::of(rig.session)->attach(&rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(rig.session)->state(rig.frame) == BrowserViews::State::live, patience);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("start")), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(rig.session)->picture(rig.frame).isNull(), patience);
        const QImage before = BrowserViews::of(rig.session)->picture(rig.frame);
        click(&rig.canvas, rig.view({100 + 100, 200 + 50}));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("clicked||0|1")), 10'000);
        // The screencast goes on after a click (focus emulation once starved it), so the new button text arrives as a picture.
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(rig.session)->picture(rig.frame) != before, 15'000);
        QVERIFY(rig.session.selection().empty());
    }

    void typingReachesAnInputAndTheWheelScrollsThePage()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        BrowserViews::of(rig.session)->attach(&rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(rig.session)->state(rig.frame) == BrowserViews::State::live, patience);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("start")), 10'000);
        click(&rig.canvas, rig.view({100 + 100, 200 + 120}));
        // The last field is document.hasFocus(): a clicked page draws its caret and takes :focus.
        QTest::keyClicks(&rig.canvas, QStringLiteral("hi"));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("idle|hi|0|1")), 10'000);
        QTest::keyClick(&rig.canvas, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(&rig.canvas, Qt::Key_Backspace);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("idle||0|1")), 10'000);

        const QPointF at = rig.view({100 + 300, 200 + 300});
        QWheelEvent wheel(at, rig.canvas.mapToGlobal(at), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&rig.canvas, &wheel);
        QTRY_VERIFY_WITH_TIMEOUT(!titles().filter(QRegularExpression(QStringLiteral(R"(\|[1-9][0-9]*\|1$)"))).isEmpty(), 10'000);
    }

    void popupOpensInTheSameFrame(int cssY)
    {
        Rig rig(page(QStringLiteral("index.html")));
        BrowserViews::of(rig.session)->attach(&rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(rig.session)->state(rig.frame) == BrowserViews::State::live, patience);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("start")), 10'000);
        const qsizetype tabs = titles().size();
        click(&rig.canvas, rig.view({100.0 + 100, 200.0 + cssY}));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("popup")), 15'000);
        QTRY_COMPARE_WITH_TIMEOUT(titles().size(), tabs, 15'000);
        QVERIFY(rig.session.document()->find(rig.frame)->browser->url.toString().endsWith(QStringLiteral("popup.html")));
    }

    void aLinkToANewWindowOpensInTheSameFrame()
    {
        NEEDS_CHROMIUM;
        popupOpensInTheSameFrame(180);
    }

    void aScriptedWindowOpensInTheSameFrame()
    {
        NEEDS_CHROMIUM;
        popupOpensInTheSameFrame(240);
    }

    void anAlertIsDismissedAndSaidAsANotice()
    {
        NEEDS_CHROMIUM;
        Rig rig(page(QStringLiteral("index.html")));
        BrowserViews::of(rig.session)->attach(&rig.canvas);
        QSignalSpy notices(BrowserViews::of(rig.session), &BrowserViews::notice);
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(rig.session)->state(rig.frame) == BrowserViews::State::live, patience);
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("start")), 10'000);
        click(&rig.canvas, rig.view({100 + 100, 200 + 300}));
        QTRY_VERIFY_WITH_TIMEOUT(titles().contains(QStringLiteral("after-alert")), 15'000);
        QTRY_VERIFY_WITH_TIMEOUT(!notices.isEmpty(), 5'000);
        QVERIFY(notices.first().first().toString().contains(QStringLiteral("hello there")));
    }
};

QTEST_MAIN(BrowseToolTests)
#include "BrowseToolTests.moc"
