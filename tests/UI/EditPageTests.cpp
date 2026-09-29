#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

// Edit Page (docs/LIVE-IN-FRAME.md, section 3). Most cases talk to a fake host and need no Chromium; the last drives a
// real headless page on a throwaway profile from a local server, and skips without Chromium.
namespace {
constexpr int patience = 60'000;
const QString mouse = QStringLiteral("Input.dispatchMouseEvent");

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
        return true;
    }
    QString beginEditPage(const QUuid &frame) override
    {
        begun << frame;
        return failure;
    }
    void endEditPage(const QUuid &frame) override { ended << frame; }
    EditBoxes editBoxes(const QUuid &) const override { return boxes; }
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
    QList<QUuid> begun;
    QList<QUuid> ended;
    QString failure;
    EditBoxes boxes;
};

struct Rig {
    EditorSession session;
    EditorCanvas canvas{session};
    FakeHost host;
    QUuid frame;

    Rig()
    {
        session.loadDocument(VectorDocument::blank({4000, 3000}));
        canvas.resize(1000, 800);
        canvas.show();
        session.zoomToRect(QRectF(50, 100, 900, 700));
        canvas.setBrowserViewHost(&host);
        frame = session.addBrowserView({100, 200, 600, 400}, QUrl(QStringLiteral("https://example.com/a")));
        session.deselectAll();
        session.selectTool(Tool::select);
    }
    QPoint view(QPointF document) const { return canvas.documentToView().map(document).toPoint(); }
    QPoint inFrame() const { return view({300, 400}); }
    QPoint offFrame() const { return view({1500, 1500}); }
    void enter() { QVERIFY(canvas.enterEditPage(frame)); }
};

void click(QWidget *widget, QPoint at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QTest::mouseClick(widget, Qt::LeftButton, modifiers, at);
}
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class EditPageTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
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
        LiveFrames::clearPending(QString());
        BrowserViews::shutdownPool();
    }

    void aDoubleClickInAFrameEntersEditPage()
    {
        Rig rig;
        QSignalSpy changed(&rig.canvas, &EditorCanvas::editPageChanged);
        QVERIFY(!rig.canvas.editPageFrame());
        QTest::mouseDClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.inFrame());
        QCOMPARE(rig.canvas.editPageFrame(), std::optional<QUuid>(rig.frame));
        QCOMPARE(rig.host.begun, QList<QUuid>{rig.frame});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(rig.session.tool(), Tool::select);
        QVERIFY(rig.session.selection().empty());
    }

    void aPageThatCantBeEditedSaysWhyAndStaysOut()
    {
        Rig rig;
        rig.host.failure = QStringLiteral("Type an address first; there's no page to edit.");
        QSignalSpy notices(&rig.canvas, &EditorCanvas::notice);
        QVERIFY(!rig.canvas.enterEditPage(rig.frame));
        QVERIFY(!rig.canvas.editPageFrame());
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().first().toString(), rig.host.failure);
        QVERIFY(rig.host.ended.isEmpty());
    }

    void aClickReachesThePageAsAMoveThenAPressAndRelease()
    {
        Rig rig;
        rig.enter();
        click(&rig.canvas, rig.view({100 + 30, 200 + 50}));
        const auto presses = rig.host.of(mouse, QStringLiteral("mousePressed"));
        QCOMPARE(presses.size(), 1);
        QCOMPARE(rig.host.of(mouse, QStringLiteral("mouseReleased")).size(), 1);
        QVERIFY(qAbs(presses[0].params["x"].toDouble() - 30) < 1.5);
        QVERIFY(qAbs(presses[0].params["y"].toDouble() - 50) < 1.5);
        QCOMPARE(presses[0].params["clickCount"].toInt(), 1);
        int pressAt = -1, moveBefore = -1;
        for (int i = 0; i < rig.host.calls.size(); ++i) {
            if (rig.host.calls[i].params["type"].toString() == QLatin1String("mousePressed"))
                pressAt = i;
            else if (pressAt < 0 && rig.host.calls[i].params["type"].toString() == QLatin1String("mouseMoved"))
                moveBefore = i;
        }
        QVERIFY(moveBefore >= 0 && moveBefore < pressAt);
        // Picking an element never selects the frame's object.
        QVERIFY(rig.session.selection().empty());
        QCOMPARE(rig.canvas.editPageFrame(), std::optional<QUuid>(rig.frame));
    }

    void shiftPassesThroughToAddToTheSelection()
    {
        Rig rig;
        rig.enter();
        click(&rig.canvas, rig.inFrame(), Qt::ShiftModifier);
        const auto presses = rig.host.of(mouse, QStringLiteral("mousePressed"));
        QCOMPARE(presses.size(), 1);
        QCOMPARE(presses[0].params["modifiers"].toInt() & 8, 8);
    }

    void noKeyEverReachesThePage()
    {
        Rig rig;
        rig.enter();
        click(&rig.canvas, rig.inFrame());
        QTest::keyClick(&rig.canvas, Qt::Key_A);
        QTest::keyClick(&rig.canvas, Qt::Key_Return);
        QVERIFY(rig.host.of(QStringLiteral("Input.dispatchKeyEvent")).isEmpty());
    }

    void escapeLeaves()
    {
        Rig rig;
        rig.enter();
        QTest::keyClick(&rig.canvas, Qt::Key_Escape);
        QVERIFY(!rig.canvas.editPageFrame());
        QCOMPARE(rig.host.ended, QList<QUuid>{rig.frame});
    }

    void pickingAnotherToolLeaves()
    {
        Rig rig;
        rig.enter();
        rig.session.selectTool(Tool::rectangle);
        QVERIFY(!rig.canvas.editPageFrame());
        QCOMPARE(rig.host.ended, QList<QUuid>{rig.frame});
    }

    void aClickOutsideTheFrameLeaves()
    {
        Rig rig;
        rig.enter();
        click(&rig.canvas, rig.offFrame());
        QVERIFY(!rig.canvas.editPageFrame());
        QCOMPARE(rig.host.ended, QList<QUuid>{rig.frame});
        QVERIFY(rig.host.of(mouse, QStringLiteral("mousePressed")).isEmpty());
    }

    void deletingTheFrameLeaves()
    {
        Rig rig;
        rig.enter();
        rig.session.select({rig.frame});
        rig.session.deleteSelection();
        QVERIFY(!rig.canvas.editPageFrame());
    }

    void theWheelScrollsThePageAndCtrlStillZooms()
    {
        Rig rig;
        rig.enter();
        const QPointF at = rig.inFrame();
        QWheelEvent plain(at, rig.canvas.mapToGlobal(at), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&rig.canvas, &plain);
        QCOMPARE(rig.host.of(mouse, QStringLiteral("mouseWheel")).size(), 1);
        const QTransform before = rig.canvas.documentToView();
        QWheelEvent zoom(at, rig.canvas.mapToGlobal(at), {}, QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&rig.canvas, &zoom);
        QCOMPARE(rig.host.of(mouse, QStringLiteral("mouseWheel")).size(), 1);
        QVERIFY(rig.canvas.documentToView() != before);
    }

    void aSecondFrameTakesTheModeOverFromTheFirst()
    {
        Rig rig;
        const QUuid other = rig.session.addBrowserView({1200, 200, 600, 400}, QUrl(QStringLiteral("https://example.com/b")));
        rig.session.deselectAll();
        rig.session.selectTool(Tool::select);
        rig.enter();
        QVERIFY(rig.canvas.enterEditPage(other));
        QCOMPARE(rig.canvas.editPageFrame(), std::optional<QUuid>(other));
        QCOMPARE(rig.host.ended, QList<QUuid>{rig.frame});
    }

    void theHoverAndSelectionBoxesFollowTheZoom()
    {
        Rig rig;
        rig.enter();
        BrowserViewHost::EditBoxes boxes;
        boxes.hover = BrowserViewHost::EditBox{QRectF(20, 30, 200, 60), QStringLiteral("h1  200 × 60")};
        rig.host.boxes = boxes;
        rig.canvas.update();
        const QImage plain = rig.canvas.grab().toImage();
        rig.host.boxes = {};
        rig.canvas.update();
        const QImage bare = rig.canvas.grab().toImage();
        // The top edge of the hover box, in view points.
        const QPoint edge = rig.view({100 + 20 + 100, 200 + 30});
        QVERIFY(plain.pixel(edge) != bare.pixel(edge));
        // Zooming moves the box with the page.
        rig.host.boxes = boxes;
        rig.session.zoomToRect(QRectF(0, 0, 2000, 1500));
        rig.canvas.update();
        const QImage zoomed = rig.canvas.grab().toImage();
        const QPoint moved = rig.view({100 + 20 + 100, 200 + 30});
        QVERIFY(moved != edge);
        QVERIFY(zoomed.pixel(moved) != zoomed.pixel(moved + QPoint(0, -6)));
    }

    void theHoverBoxIsRightUnderAHeldPreview()
    {
        Rig rig;
        rig.enter();
        BrowserViewHost::EditBoxes boxes;
        boxes.hover = BrowserViewHost::EditBox{QRectF(20, 30, 200, 60), QStringLiteral("h1  200 \u00d7 60")};
        rig.host.boxes = boxes;
        // The preview narrows the frame and moves its corner: the box is the previewed page's, so it moves with it.
        rig.session.beginPreview(QStringLiteral("Preview Width"));
        rig.session.previewFrameBox(rig.frame, QRectF(160, 260, 300, 400));
        QVERIFY(rig.session.isPreviewOnly());
        QCOMPARE(rig.canvas.editPageFrame(), std::optional<QUuid>(rig.frame));
        rig.canvas.update();
        const QImage shown = rig.canvas.grab().toImage();
        rig.host.boxes = {};
        rig.canvas.update();
        const QImage bare = rig.canvas.grab().toImage();
        const QPoint edge = rig.view({160 + 20 + 100, 260 + 30});
        QVERIFY(shown.pixel(edge) != bare.pixel(edge));
        // Not where the box would be on the frame's own corner.
        const QPoint unpreviewed = rig.view({100 + 20 + 100, 200 + 30});
        QCOMPARE(shown.pixel(unpreviewed), bare.pixel(unpreviewed));
    }

    void theBarsPencilTogglesTheMode()
    {
        Rig rig;
        rig.session.select({rig.frame});
        const QRectF box = rig.canvas.documentToView().mapRect(rig.session.document()->find(rig.frame)->path.painterPath().boundingRect());
        const QPoint pencil(int(box.right() - 2 - 12), int(box.top() - 4 - 14));
        click(&rig.canvas, pencil);
        QCOMPARE(rig.canvas.editPageFrame(), std::optional<QUuid>(rig.frame));
        QCOMPARE(rig.host.begun, QList<QUuid>{rig.frame});
        click(&rig.canvas, pencil);
        QVERIFY(!rig.canvas.editPageFrame());
        QCOMPARE(rig.host.ended, QList<QUuid>{rig.frame});
    }

    void enteringAgainAfterLeavingBeginsAgain()
    {
        Rig rig;
        rig.session.select({rig.frame});
        rig.canvas.update();
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.canvas.leaveEditPage();
        QVERIFY(!rig.canvas.editPageFrame());
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        QCOMPARE(rig.host.begun.size(), 2);
    }

    void aRealPickSelectsALinkWithoutFollowingItAndShiftAddsAnother()
    {
        NEEDS_CHROMIUM;
        RealPage page;
        openReal(page);
        if (QTest::currentTestFailed())
            return;
        EditorCanvas &canvas = page.canvas;
        BrowserViews *views = page.views;
        const QUuid frame = page.frame;
        QVERIFY(canvas.enterEditPage(frame));
        QTRY_COMPARE_WITH_TIMEOUT(page.frames->snapshot(frame).state, LiveSession::State::running, patience);
        QVERIFY(page.frames->snapshot(frame).pageEditing);

        QRectF link;
        for (int i = 0; i < 300 && link.isEmpty(); ++i) {
            link = page.pageRect(QStringLiteral("#link"));
            if (link.isEmpty())
                QTest::qWait(100);
        }
        QVERIFY(!link.isEmpty());
        const QRectF title = page.pageRect(QStringLiteral("#title"));
        QVERIFY(!title.isEmpty());

        QTest::mouseMove(&canvas, page.at(link));
        QTRY_VERIFY_WITH_TIMEOUT(views->editBoxes(frame).hover.has_value(), 15'000);
        const auto hover = views->editBoxes(frame).hover;
        QVERIFY(qAbs(hover->rect.x() - link.x()) < 1.5);
        QVERIFY(qAbs(hover->rect.width() - link.width()) < 1.5);

        click(&canvas, page.at(link));
        QTRY_COMPARE_WITH_TIMEOUT(views->editBoxes(frame).selection.size(), qsizetype(1), 15'000);
        click(&canvas, page.at(title), Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(views->editBoxes(frame).selection.size(), qsizetype(2), 15'000);
        // The click was the overlay's, so the link didn't navigate.
        QVERIFY(page.session.document()->find(frame)->browser->url.toString().endsWith(QStringLiteral("index.html")));

        canvas.leaveEditPage();
        QTRY_VERIFY_WITH_TIMEOUT(!page.frames->snapshot(frame).pageEditing, 15'000);
    }

    void browseFollowsTheLinkAfterEditPage()
    {
        NEEDS_CHROMIUM;
        RealPage page;
        openReal(page);
        if (QTest::currentTestFailed())
            return;
        const QUuid frame = page.frame;
        QVERIFY(page.canvas.enterEditPage(frame));
        QTRY_VERIFY_WITH_TIMEOUT(page.frames->snapshot(frame).pageEditing, patience);
        QRectF link;
        for (int i = 0; i < 300 && link.isEmpty(); ++i) {
            link = page.pageRect(QStringLiteral("#link"));
            if (link.isEmpty())
                QTest::qWait(100);
        }
        QVERIFY(!link.isEmpty());
        // Edit Page's overlay takes the click as a pick; once it is left the page is the user's again.
        page.canvas.leaveEditPage();
        QTRY_VERIFY_WITH_TIMEOUT(!page.frames->snapshot(frame).pageEditing, 15'000);
        page.session.selectTool(Tool::browse);
        click(&page.canvas, page.at(link));
        QTRY_VERIFY_WITH_TIMEOUT(page.session.document()->find(frame)->browser->url.toString().endsWith(QStringLiteral("second.html")), 15'000);
    }

private:
    struct RealPage {
        StaticServer server;
        EditorSession session;
        EditorCanvas canvas{session};
        QUuid frame;
        BrowserViews *views = nullptr;
        LiveFrames *frames = nullptr;

        QRectF pageRect(const QString &selector)
        {
            QRectF found;
            bool done = false;
            frames->run(frame, [&](LiveSession &live) {
                const QJsonObject rect = live.evaluate(QStringLiteral("(() => { const r = document.querySelector('%1').getBoundingClientRect(); return {x: r.x, y: r.y, w: r.width, h: r.height}; })()").arg(selector)).toObject();
                found = QRectF(rect["x"].toDouble(), rect["y"].toDouble(), rect["w"].toDouble(), rect["h"].toDouble());
                return QString();
            }, [&done](const QString &) { done = true; });
            for (int i = 0; i < 150 && !done; ++i)
                QTest::qWait(100);
            return found;
        }
        QPoint at(const QRectF &rect) const { return canvas.documentToView().map(QPointF(100 + rect.center().x(), 100 + rect.center().y())).toPoint(); }
    };

    // The fixture site in a Browser View at (100, 100), loaded and live.
    void openReal(RealPage &page)
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site");
        QDir().mkpath(folder);
        for (const char *name : {"index.html", "second.html"}) {
            const QString target = folder + QLatin1Char('/') + QLatin1String(name);
            QFile::remove(target);
            QVERIFY(QFile::copy(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/liveframe/") + QLatin1String(name), target));
        }
        QVERIFY(page.server.serve(folder).isEmpty());
        VectorDocument document = VectorDocument::blank({4000, 3000});
        VectorObject view = VectorObject::frame({100, 100, 800, 600}, QStringLiteral("Site"));
        view.browser = BrowserView{QUrl(page.server.url().toString() + QStringLiteral("index.html")), {}, {}};
        page.frame = view.id;
        document.insert(view, document.layers().front());
        page.session.loadDocument(document);
        page.canvas.resize(1000, 800);
        page.canvas.show();
        page.session.zoomToRect(QRectF(50, 50, 900, 700));
        page.views = BrowserViews::of(page.session);
        page.views->attach(&page.canvas);
        page.session.selectTool(Tool::select);
        QTRY_VERIFY_WITH_TIMEOUT(!page.views->poolKey(page.frame).isNull(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(page.views->state(page.frame) == BrowserViews::State::live, patience);
        page.frames = LiveFrames::of(page.session);
    }
};

QTEST_MAIN(EditPageTests)
#include "EditPageTests.moc"
