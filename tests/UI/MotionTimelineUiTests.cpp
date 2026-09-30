#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionInspector.h"
#include "UI/MotionTimeline.h"
#include "UI/MotionTrackView.h"
#include "UI/NumberField.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <atomic>
#include <memory>

// The timeline docked under the canvas (docs/MOTION.md, section 2): its rows, the playhead, Play, Loop and Replay,
// the Code tab and the inspector, driven against the motion fixture in headless Chromium on a throwaway profile. Skips
// without Chromium.
namespace {
constexpr int patience = 60'000;

struct Hosted {
    EditorCanvas canvas;
    QUuid frame;
    Hosted(EditorSession &session, const QUrl &url) : canvas(session)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 600, 400}, QStringLiteral("Site"));
        view.browser = BrowserView{url, {}, {}};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
        canvas.resize(1000, 800);
        canvas.show();
        BrowserViews::of(session)->attach(&canvas);
    }
};
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class MotionTimelineUiTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;
    StaticServer m_plain;

    QUrl page(StaticServer &server, const QString &path) const
    {
        QUrl url = server.url();
        url.setPath(path);
        return url;
    }

    // Runs `expression` in the frame's page through Live, where the timeline's own calls run.
    static QJsonValue inPage(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        auto value = std::make_shared<QJsonValue>();
        bool answered = false;
        LiveFrames::of(session)->run(frame, [value, expression](LiveSession &live) {
            *value = live.evaluate(expression);
            return QString();
        }, [&answered](const QString &) { answered = true; });
        for (int i = 0; i < 600 && !answered; ++i)
            QTest::qWait(25);
        return *value;
    }

    // Straight off the pool, for a page whose Live has ended.
    static QJsonValue inPageNoLive(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        auto answered = std::make_shared<std::atomic<bool>>(false);
        auto value = std::make_shared<QJsonValue>();
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"), {{"expression", expression}, {"returnByValue", true}},
                                   [answered, value](const QJsonObject &result, const QString &error) {
                                       if (error.isEmpty())
                                           *value = result.value("result").toObject().value("value");
                                       answered->store(true);
                                   });
        for (int i = 0; i < 400 && !answered->load(); ++i)
            QTest::qWait(25);
        return *value;
    }

    static double opacity(EditorSession &session, const QUuid &frame, const QString &selector)
    {
        return inPage(session, frame, QStringLiteral("parseFloat(getComputedStyle(document.querySelector('%1')).opacity)").arg(selector)).toDouble(-999);
    }

    // The frame's tab exists and the timeline is open on it with its rows listed.
    static void openAndWait(EditorSession &session, MotionTimeline &timeline, const QUuid &frame)
    {
        BrowserViews *views = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(frame).isNull(), patience);
        const QString failure = timeline.open(frame);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_VERIFY2_WITH_TIMEOUT(!timeline.timeline().tracks.isEmpty(), qPrintable(describe(session, timeline, frame)), patience);
    }

    // Where the timeline is stuck, for a failure that names it.
    static QString describe(EditorSession &session, const MotionTimeline &timeline, const QUuid &frame)
    {
        const LiveFrames::Snapshot snapshot = LiveFrames::of(session)->snapshot(frame);
        return QStringLiteral("state %1, message \"%2\", held %3, motion keys %4, status \"%5\", open %6")
            .arg(int(snapshot.state))
            .arg(snapshot.message)
            .arg(snapshot.motionHeld)
            .arg(snapshot.motion.keys().join(QLatin1Char(',')), timeline.status())
            .arg(timeline.isOpen());
    }

    static QString idOf(const MotionTimeline &timeline, const QString &label)
    {
        for (const Motion::Track &track : timeline.timeline().tracks)
            if (track.label == label)
                return track.id;
        return {};
    }

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
        QVERIFY(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion")).isEmpty());
        QVERIFY(m_plain.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/liveframe")).isEmpty());
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
        QFile::remove(ProjectRegistry::path());
        MotionTimeline::setOpener({});
    }

    void cleanup()
    {
        BrowserViews::shutdownPool();
    }

    void aCommandsAnswerNeverReachesAnOwnerThatHasGone()
    {
        // No frame runs Live here, so every command is answered with an error through the queue: what an owner that went (a tab
        // switch ends the timeline while its seek is in flight) must never receive.
        EditorSession session;
        LiveFrames *frames = LiveFrames::of(session);
        const QUuid frame = QUuid::createUuid();
        int answers = 0;
        const auto command = [](LiveSession &) { return QString(); };
        auto owner = std::make_unique<QObject>();
        frames->run(frame, command, [&answers](const QString &) { ++answers; }, owner.get());
        owner.reset();
        QTest::qWait(50);
        QCOMPARE(answers, 0);
        // An owner that stays is answered, and so is a call with no owner, as before.
        QObject stays;
        frames->run(frame, command, [&answers](const QString &) { ++answers; }, &stays);
        QTRY_COMPARE_WITH_TIMEOUT(answers, 1, 2000);
        frames->run(frame, command, [&answers](const QString &) { ++answers; });
        QTRY_COMPARE_WITH_TIMEOUT(answers, 2, 2000);
    }

    void aTimelineThatGoesWithItsTabGivesBackTheStreamingRate()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        BrowserViews *views = BrowserViews::of(session);
        auto timeline = std::make_unique<MotionTimeline>(session, hosted.canvas);
        openAndWait(session, *timeline, hosted.frame);
        QVERIFY(views->scrubbed(hosted.frame));
        // Playing keeps a seek in flight almost all the time.
        timeline->play();
        QTest::qWait(150);
        // A tab switch deletes the editor without closing its timeline first.
        timeline.reset();
        QVERIFY(!views->scrubbed(hosted.frame));
        // The answers of what was in flight arrive after it: they reach nobody, and the page goes on.
        QTest::qWait(600);
        QVERIFY(LiveFrames::of(session)->active(hosted.frame));
    }

    void siblingsMergeIntoOneRowPerParentAndTheHeaderReadsTheTrigger()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        QVERIFY(!timeline.isOpen());
        openAndWait(session, timeline, hosted.frame);
        QVERIFY(timeline.isOpen());
        QVERIFY(timeline.isVisible() || !timeline.parentWidget());
        const QString words = idOf(timeline, QStringLiteral("h1 .word × 5"));
        QVERIFY2(!words.isEmpty(), "the five words are one row");
        const Motion::Track *track = timeline.timeline().find(words);
        QCOMPARE(track->bars.size(), 5);
        QCOMPARE(track->stagger, 60.0);
        QVERIFY(!idOf(timeline, QStringLiteral("section .card × 3")).isEmpty());
        QVERIFY(timeline.timeline().hasScroll());
        QCOMPARE(timeline.findChild<QLabel *>(QStringLiteral("motionTrigger"))->text(), QStringLiteral("· On load"));
        QVERIFY(timeline.findChild<QLabel *>(QStringLiteral("motionTime"))->text().contains(QStringLiteral(" / ")));
        // Edit Page began on the frame, and the page is held.
        QCOMPARE(hosted.canvas.editPageFrame(), std::optional<QUuid>(hosted.frame));
        QTRY_VERIFY_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).motionHeld, patience);
    }

    void aClickOnARowSelectsItsElementsOnThePage()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        auto *tracks = timeline.findChild<MotionTrackView *>(QStringLiteral("motionTracks"));
        tracks->resize(720, tracks->contentHeight());
        const QString words = idOf(timeline, QStringLiteral("h1 .word × 5"));
        const QRect row = tracks->rowRect(words);
        QVERIFY(row.isValid());
        QSignalSpy changed(&timeline, &MotionTimeline::changed);
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(30, row.center().y()));
        QCOMPARE(timeline.selectedId(), words);
        QVERIFY(changed.size() >= 1);
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).selection.size(), qsizetype(5), patience);
        const QJsonArray picked = LiveFrames::of(session)->snapshot(hosted.frame).selection;
        QCOMPARE(picked[0].toObject()["tag"].toString(), QStringLiteral("span"));
        QCOMPARE(picked[4].toObject()["text"].toString(), QStringLiteral("morning"));
        // And a single element's row picks that one.
        const QString lede = idOf(timeline, QStringLiteral("p#lede"));
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(30, tracks->rowRect(lede).center().y()));
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).selection.size(), qsizetype(1), patience);
    }

    void draggingOnTheRulerSeeksThePage()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        auto *tracks = timeline.findChild<MotionTrackView *>(QStringLiteral("motionTracks"));
        tracks->resize(900, tracks->contentHeight());
        const QRect ruler = tracks->rulerRect(false);
        QVERIFY(ruler.isValid());
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(tracks->xForTime(240), ruler.center().y()));
        QVERIFY2(qAbs(timeline.playhead() - 240) < 3, qPrintable(QString::number(timeline.playhead())));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        // The lede fades in over 400 ms, linearly: 240 ms is 0.6.
        QVERIFY2(qAbs(opacity(session, hosted.frame, QStringLiteral("#lede")) - 0.6) < 0.02, "the page is at 240 ms");
        // A drag: press, then move along the ruler; the page follows the last position.
        QTest::mousePress(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(tracks->xForTime(100), ruler.center().y()));
        QTest::mouseMove(tracks, QPoint(tracks->xForTime(300), ruler.center().y()));
        QTest::mouseRelease(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(tracks->xForTime(300), ruler.center().y()));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QVERIFY2(qAbs(opacity(session, hosted.frame, QStringLiteral("#lede")) - 0.75) < 0.02, "the page is at 300 ms");
    }

    void aFastScrubSendsFewSeeksAndEndsWhereTheHandEnded()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("window.__oma.motion.isHeld()")).toBool(), patience);
        inPage(session, hosted.frame, QStringLiteral("window.__seeks = 0; const seek = window.__oma.motion.seek; window.__oma.motion.seek = (ms) => { window.__seeks++; return seek(ms); }; true"));
        // Thirty positions in a row, with no event loop between them.
        for (int i = 1; i <= 30; ++i)
            timeline.scrubTo(i * 10.0);
        QCOMPARE(timeline.playhead(), 300.0);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        const int seeks = inPage(session, hosted.frame, QStringLiteral("window.__seeks")).toInt();
        QVERIFY2(seeks >= 1 && seeks <= 6, qPrintable(QStringLiteral("%1 seeks for 30 positions").arg(seeks)));
        QVERIFY2(qAbs(opacity(session, hosted.frame, QStringLiteral("#lede")) - 0.75) < 0.02, "the page ended at 300 ms");
    }

    void thePicturesChangeAfterASeek()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        BrowserViews *views = BrowserViews::of(session);
        const QJsonObject box = inPage(session, hosted.frame, QStringLiteral("(() => { const r = document.querySelector('#lede').getBoundingClientRect(); return {x: r.x, y: r.y, w: r.width, h: r.height}; })()")).toObject();
        QVERIFY(box["w"].toDouble() > 100);
        // Dark pixels of the lede's text in the frame's newest picture.
        const auto ink = [&] {
            const QImage image = views->picture(hosted.frame);
            if (image.isNull())
                return -1;
            const double scale = image.devicePixelRatio();
            const QRect area(int(box["x"].toDouble() * scale), int(box["y"].toDouble() * scale), int(box["w"].toDouble() * scale), int(box["h"].toDouble() * scale));
            int dark = 0;
            for (int y = area.top(); y < std::min(area.bottom(), image.height()); ++y)
                for (int x = area.left(); x < std::min(area.right(), image.width()); ++x)
                    dark += qGray(image.pixel(x, y)) < 110;
            return dark;
        };
        timeline.scrubTo(0);
        QTRY_COMPARE_WITH_TIMEOUT(ink(), 0, patience);
        QSignalSpy arrived(views, &BrowserViews::pictureArrived);
        timeline.scrubTo(400);
        QTRY_VERIFY2_WITH_TIMEOUT(ink() > 60, "the picture shows the text once the playhead is past its fade", patience);
        QVERIFY(arrived.size() >= 1);
        timeline.scrubTo(0);
        QTRY_COMPARE_WITH_TIMEOUT(ink(), 0, patience);
    }

    void playAdvancesThePlayheadAndStopsAtTheEndOrLoops()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const double duration = timeline.timeline().duration;
        QVERIFY(duration > 500);
        timeline.scrubTo(0);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        timeline.play();
        QVERIFY(timeline.isPlaying());
        QTRY_VERIFY_WITH_TIMEOUT(timeline.playhead() > 0, 5000);
        // Without Loop it stops at the end, and the page is at its end.
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.isPlaying(), 10'000);
        QCOMPARE(timeline.playhead(), duration);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QCOMPARE(opacity(session, hosted.frame, QStringLiteral("#lede")), 1.0);

        // Loop: it starts again at the end instead of stopping.
        timeline.findChild<QToolButton *>(QStringLiteral("motionLoop"))->click();
        QVERIFY(timeline.isLooping());
        QList<double> seen;
        connect(&timeline, &MotionTimeline::playheadChanged, this, [&] { seen << timeline.playhead(); });
        timeline.replay();
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            for (int i = 1; i < seen.size(); ++i)
                if (seen[i] + 100 < seen[i - 1])
                    return true;
            return false;
        })(), 15'000);
        QVERIFY(timeline.isPlaying());
        timeline.pause();
        QVERIFY(!timeline.isPlaying());
        timeline.disconnect(this);
    }

    void replayGoesBackToTheStartAndPlays()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        timeline.scrubTo(timeline.timeline().duration);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QCOMPARE(opacity(session, hosted.frame, QStringLiteral("#lede")), 1.0);
        timeline.findChild<QToolButton *>(QStringLiteral("motionReplay"))->click();
        QVERIFY(timeline.isPlaying());
        // A load animation is seen from its start without reloading the page: the lede is not there at the first frame.
        QVERIFY(timeline.playhead() < 100);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.isPlaying(), 10'000);
        QCOMPARE(timeline.playhead(), timeline.timeline().duration);
    }

    void theFramesScrollDrivesTheScrollPlayheadAndTheRulerScrollsTheFrame()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        auto *tracks = timeline.findChild<MotionTrackView *>(QStringLiteral("motionTracks"));
        tracks->resize(900, tracks->contentHeight());
        // The page scrolls (the wheel over the frame in Edit Page): the playhead on the scroll ruler follows.
        inPage(session, hosted.frame, QStringLiteral("window.scrollTo(0, 500); true"));
        QTRY_COMPARE_WITH_TIMEOUT(timeline.scrollPlayhead(), 500.0, patience);
        // And a click on that ruler scrolls the page.
        const QRect ruler = tracks->rulerRect(true);
        QVERIFY(ruler.isValid());
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(tracks->xForScroll(800), ruler.center().y()));
        QVERIFY2(qAbs(timeline.scrollPlayhead() - 800) < 4, qPrintable(QString::number(timeline.scrollPlayhead())));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QVERIFY(qAbs(inPage(session, hosted.frame, QStringLiteral("scrollY")).toDouble() - timeline.scrollPlayhead()) < 1);
        // At the start of a card's range it isn't there yet; at the end it is.
        const Motion::Track *cards = nullptr;
        for (const Motion::Track &track : timeline.timeline().tracks)
            if (track.isScroll())
                cards = &track;
        QVERIFY(cards);
        timeline.scrubScrollTo(cards->bars.first().start + cards->bars.first().length);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QVERIFY(opacity(session, hosted.frame, QStringLiteral("#guji")) > 0.97);
    }

    void aHoverRowHoldsItsStateWhenPickedAndLetsGoWhenAnotherIsPicked()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString hover = idOf(timeline, QStringLiteral("a#primary"));
        QVERIFY(!hover.isEmpty());
        QVERIFY(timeline.timeline().find(hover)->potential);
        timeline.selectRow(hover);
        // Held in :hover, the transition is a real row now, with the same id, and scrubbing moves the link.
        QTRY_VERIFY_WITH_TIMEOUT(timeline.timeline().find(hover) && !timeline.timeline().find(hover)->potential, patience);
        QCOMPARE(timeline.timeline().find(hover)->state(), QStringLiteral("hover"));
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("document.getAnimations().some(a => a.constructor.name === 'CSSTransition' && a.playState === 'paused')")).toBool(), patience);
        timeline.scrubTo(200);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QCOMPARE(inPage(session, hosted.frame, QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("matrix(1, 0, 0, 1, 0, -2)"));
        // Picking a row that isn't a state lets it go.
        timeline.selectRow(idOf(timeline, QStringLiteral("p#lede")));
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("none"), patience);
    }

    void escapeLeavesEditPageAndReleasesThePage()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        QSignalSpy closed(&timeline, &MotionTimeline::closed);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("window.__oma.motion.isHeld()")).toBool(), patience);
        hosted.canvas.setFocus();
        QTest::keyClick(&hosted.canvas, Qt::Key_Escape);
        QVERIFY(!hosted.canvas.editPageFrame());
        QVERIFY(!timeline.isOpen());
        QCOMPARE(closed.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!inPage(session, hosted.frame, QStringLiteral("window.__oma.motion.isHeld()")).toBool(), patience);
        QCOMPARE(inPage(session, hosted.frame, QStringLiteral("document.getAnimations().some(a => a.playState === 'paused')")).toBool(), false);
        QVERIFY(!LiveFrames::of(session)->snapshot(hosted.frame).motionHeld);
    }

    void theCloseButtonReleasesAndHidesTheTimeline()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        QWidget host;
        MotionTimeline timeline(session, hosted.canvas, &host);
        host.show();
        openAndWait(session, timeline, hosted.frame);
        QVERIFY(timeline.isVisible());
        timeline.findChild<QToolButton *>(QStringLiteral("motionClose"))->click();
        QVERIFY(!timeline.isOpen());
        QVERIFY(!timeline.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!inPage(session, hosted.frame, QStringLiteral("window.__oma.motion.isHeld()")).toBool(), patience);
        // Edit Page stays on: the timeline was closed, not the mode.
        QCOMPARE(hosted.canvas.editPageFrame(), std::optional<QUuid>(hosted.frame));
        // Opened again, it holds the page again.
        QVERIFY(timeline.open(hosted.frame).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("window.__oma.motion.isHeld()")).toBool(), patience);
    }

    void stoppingLiveClosesTheTimelineAndLetsTheKeysGo()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        QSignalSpy closed(&timeline, &MotionTimeline::closed);
        LiveFrames::of(session)->stop(hosted.frame);
        QVERIFY(!timeline.isOpen());
        QCOMPARE(closed.size(), 1);
        // The page plays on: the session let go of it as it ended.
        QTRY_VERIFY_WITH_TIMEOUT(!inPageNoLive(session, hosted.frame, QStringLiteral("window.__oma ? window.__oma.motion.isHeld() : false")).toBool(), patience);
    }

    void aPageWithNoMotionSaysSo()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_plain, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        BrowserViews *views = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(hosted.frame).isNull(), patience);
        QVERIFY(timeline.open(hosted.frame).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).motionHeld, patience);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.status(), QStringLiteral("No motion on this page yet."), patience);
        QVERIFY(timeline.timeline().tracks.isEmpty());
        QVERIFY(!timeline.findChild<QToolButton *>(QStringLiteral("motionPlay"))->isEnabled());
    }

    void theCodeTabShowsTheMarkedBlockAndAClickOpensItsFile()
    {
        NEEDS_CHROMIUM;
        const QString folder = QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion");
        QVERIFY(ProjectRegistry::remember(page(m_server, QStringLiteral("/index.html")), folder).isEmpty());
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        QTRY_VERIFY_WITH_TIMEOUT(!LiveFrames::of(session)->snapshot(hosted.frame).project.isEmpty(), patience);
        // Live has the project; the list comes again with the next change, which a pick gives.
        timeline.selectRow(idOf(timeline, QStringLiteral("h1 .word × 5")));
        QStringList opened;
        MotionTimeline::setOpener([&opened](const QString &path) { opened << path; });
        timeline.showTab(true);
        auto *code = timeline.findChild<QPlainTextEdit *>(QStringLiteral("motionCode"));
        QVERIFY(timeline.codeShown());
        QVERIFY2(code->toPlainText().contains(QStringLiteral("/* omastrator:motion headline-reveal */")), qPrintable(code->toPlainText()));
        QVERIFY(code->toPlainText().contains(QStringLiteral("@keyframes nl-rise")));
        QVERIFY(code->toPlainText().contains(QStringLiteral("style.css")));
        QCOMPARE(timeline.codeBlocks().size(), 1);
        // A click on a line opens the file it is in.
        code->resize(600, 300);
        code->show();
        QTest::mouseClick(code->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 60));
        QCOMPARE(opened.size(), 1);
        QVERIFY(opened.first().endsWith(QStringLiteral("/style.css")));
        // A page that isn't the user's has no code to show.
        timeline.showTab(false);
        QVERIFY(!timeline.codeShown());
    }

    void theInspectorShowsWhatTheSelectedRowDoes()
    {
        NEEDS_CHROMIUM;
        const QString folder = QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion");
        QVERIFY(ProjectRegistry::remember(page(m_server, QStringLiteral("/index.html")), folder).isEmpty());
        EditorSession session;
        Hosted hosted(session, page(m_server, QStringLiteral("/index.html")));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QVERIFY(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorEmpty")));
        openAndWait(session, timeline, hosted.frame);
        QTRY_VERIFY_WITH_TIMEOUT(!LiveFrames::of(session)->snapshot(hosted.frame).project.isEmpty(), patience);
        QCOMPARE(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorEmpty"))->text(), QStringLiteral("No motion on this element yet."));
        timeline.selectRow(idOf(timeline, QStringLiteral("h1 .word × 5")));
        const auto text = [&](const char *name) {
            auto *label = inspector.findChild<QLabel *>(QString::fromLatin1(name));
            return label ? label->text() : QString();
        };
        QCOMPARE(text("motionInspectorName"), QStringLiteral("Group · 5 words"));
        // What starts it is a segmented control; this one starts on load.
        QVERIFY(inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:load"))->isChecked());
        QVERIFY(!inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:scroll"))->isChecked());
        // The values are fields (phase B): the easing as text and a preset, the duration and stagger in ms.
        auto *easing = inspector.findChild<QLineEdit *>(QStringLiteral("motionInspectorEasingText"));
        QVERIFY(easing);
        QCOMPARE(easing->text(), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        QCOMPARE(inspector.findChild<QComboBox *>(QStringLiteral("motionInspectorEasingPreset"))->currentText(), QStringLiteral("Soft out"));
        QCOMPARE(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorDuration"))->value(), 480.0);
        QCOMPARE(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorStagger"))->value(), 60.0);
        QCOMPARE(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorToken:--duration-reveal"))->value(), 480.0);
        QCOMPARE(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorToken:--stagger-words"))->value(), 60.0);
        QCOMPARE(inspector.findChild<QLineEdit *>(QStringLiteral("motionInspectorToken:--ease-reveal"))->text(), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        auto *reduced = inspector.findChild<QCheckBox *>(QStringLiteral("motionInspectorReduced"));
        QVERIFY(reduced);
        QVERIFY(reduced->isChecked());
        QVERIFY(reduced->isEnabled());
        auto *from = inspector.findChild<QLineEdit *>(QStringLiteral("motionInspectorKeyframe:from:opacity"));
        QVERIFY(from);
        QCOMPARE(from->text(), QStringLiteral("0"));
        // A script's motion says it isn't tuned here.
        timeline.selectRow(idOf(timeline, QStringLiteral("span#badge")));
        QVERIFY(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorScript")));
    }
};

QTEST_MAIN(MotionTimelineUiTests)
#include "MotionTimelineUiTests.moc"
