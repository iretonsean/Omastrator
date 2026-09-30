#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "IO/FrameRecorder.h"
#include "Live/Browser.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionRecorder.h"
#include "UI/MotionTimeline.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <memory>

// Record MP4 in the timeline's header (docs/MOTION.md, section 8), against the motion fixture in headless Chromium with a fake
// ffmpeg. The recording itself is MotionRecordTests'; these are the header's controls, the save dialog, Stop, Esc and closing.
// Skips without Chromium.
namespace {
constexpr int patience = 60'000;

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

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
        // Large on screen whatever size the offscreen window settles at: a small frame is paused.
        session.zoomToRect(QRectF(0, 0, 700, 500));
        BrowserViews::of(session)->attach(&canvas);
    }
};
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class MotionRecordTimelineTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;

    QString out() { return m_directory.filePath(QStringLiteral("fake")); }
    QString target(const QString &name) { return m_directory.filePath(name); }
    QUrl page() const
    {
        QUrl url = m_server.url();
        url.setPath(QStringLiteral("/index.html"));
        return url;
    }

    // The tab exists and the timeline is open on it with its rows listed and the page held.
    static void openAndWait(EditorSession &session, MotionTimeline &timeline, const QUuid &frame)
    {
        BrowserViews *views = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(frame).isNull(), patience);
        const QString failure = timeline.open(frame);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.timeline().tracks.isEmpty(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(frame).motionHeld, patience);
        QTRY_VERIFY_WITH_TIMEOUT(views->state(frame) == BrowserViews::State::live, patience);
    }

    // The frame rate the run's arguments name.
    QString rateOf(int run = 1)
    {
        const QStringList blocks = QString::fromUtf8(read(out() + QStringLiteral("/calls"))).split(QStringLiteral("\n---\n"), Qt::SkipEmptyParts);
        const QStringList arguments = blocks.value(run - 1).split(QLatin1Char('\n'));
        return arguments.value(arguments.indexOf(QStringLiteral("-framerate")) + 1);
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
        QDir(out()).removeRecursively();
        QDir().mkpath(out());
        qputenv("OMASTRATOR_FFMPEG", FAKE_FFMPEG);
        qputenv("FAKE_FFMPEG_OUT", out().toUtf8());
        qunsetenv("FAKE_FFMPEG_KEEP");
        qunsetenv("FAKE_FFMPEG_MODE");
    }

    void cleanup()
    {
        MotionTimeline::setRecordChooser({});
        MotionTimeline::setReplaceChooser({});
        BrowserViews::shutdownPool();
    }

    void theHeaderOffersRecordMp4AndTheMoreMenu()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        auto *record = timeline.findChild<QToolButton *>(QStringLiteral("motionRecord"));
        QVERIFY(record);
        // With no rows there is nothing to record.
        QVERIFY(!record->isEnabled());
        openAndWait(session, timeline, hosted.frame);
        QVERIFY(record->isEnabled());
        QCOMPARE(record->text(), QStringLiteral("Record MP4"));
        auto *more = timeline.findChild<QToolButton *>(QStringLiteral("motionRecordMore"));
        QVERIFY(more && more->menu());
        QStringList titles;
        for (const QAction *action : more->menu()->actions())
            if (!action->isSeparator())
                titles << action->text();
        QCOMPARE(titles, (QStringList{"Record GIF…", "Record MP4 at 60 fps…", "Save Frames as PNG…"}));
        for (const QAction *action : more->menu()->actions())
            QVERIFY2(action->isEnabled() || action->isSeparator(), qPrintable(action->text()));
        QCOMPARE(timeline.suggestedName(FrameRecorder::Format::mp4), QStringLiteral("site-motion.mp4"));
        QCOMPARE(timeline.suggestedName(FrameRecorder::Format::gif), QStringLiteral("site-motion.gif"));
        QCOMPARE(timeline.suggestedName(FrameRecorder::Format::pngFrames), QStringLiteral("site-frames"));
    }

    void withoutFfmpegRecordIsDisabledWithTheHintAndThePngFramesRemain()
    {
        NEEDS_CHROMIUM;
        qputenv("OMASTRATOR_FFMPEG", target(QStringLiteral("no-such-ffmpeg")).toUtf8());
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        auto *record = timeline.findChild<QToolButton *>(QStringLiteral("motionRecord"));
        QVERIFY(!record->isEnabled());
        QCOMPARE(record->toolTip(), QStringLiteral("Recording needs ffmpeg. Install it with sudo pacman -S ffmpeg."));
        auto *more = timeline.findChild<QToolButton *>(QStringLiteral("motionRecordMore"));
        // The menu reads ffmpeg as it opens.
        emit more->menu()->aboutToShow();
        QVERIFY(!more->menu()->findChild<QAction *>(QStringLiteral("motionRecordGif"))->isEnabled());
        QVERIFY(!more->menu()->findChild<QAction *>(QStringLiteral("motionRecord60"))->isEnabled());
        QVERIFY(more->menu()->findChild<QAction *>(QStringLiteral("motionSavePng"))->isEnabled());
        QCOMPARE(timeline.record(FrameRecorder::Format::mp4), FrameRecorder::ffmpegHint());

        // The frames still come out as numbered pictures.
        const QString folder = target(QStringLiteral("frames"));
        MotionTimeline::setRecordChooser([folder](const MotionTimeline::RecordAsk &ask) {
            return ask.format == FrameRecorder::Format::pngFrames ? folder : QString();
        });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        more->menu()->findChild<QAction *>(QStringLiteral("motionSavePng"))->trigger();
        QVERIFY(timeline.isRecording());
        QVERIFY2(done.wait(patience), "the frames never finished");
        const int steps = MotionRecorder::steps(timeline.timeline().duration, 30);
        QCOMPARE(QDir(folder).entryList(QDir::Files).size(), steps);
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/frame-0001.png")));
    }

    void recordMp4AsksWhereThenRecordsTheMotionAndPutsThePlayheadBack()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const double before = timeline.playhead();
        const QString file = target(QStringLiteral("hero.mp4"));
        QString asked;
        MotionTimeline::setRecordChooser([&](const MotionTimeline::RecordAsk &ask) {
            asked = QStringLiteral("%1 %2 %3").arg(int(ask.format)).arg(ask.fps).arg(ask.suggested);
            return file;
        });
        // Nothing is written before the dialog is answered.
        QVERIFY(!QFileInfo::exists(file));
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        auto *record = timeline.findChild<QToolButton *>(QStringLiteral("motionRecord"));
        record->click();
        QCOMPARE(asked, QStringLiteral("0 30 site-motion.mp4"));
        QVERIFY(timeline.isRecording());
        // While it runs: Stop, a header line, and no Play or scrubbing.
        QCOMPARE(record->text(), QStringLiteral("Stop"));
        QVERIFY(timeline.recordedText().startsWith(QStringLiteral("Recording… ")));
        QVERIFY(!timeline.findChild<QToolButton *>(QStringLiteral("motionPlay"))->isEnabled());
        timeline.play();
        QVERIFY(!timeline.isPlaying());
        QVERIFY2(done.wait(patience), "the recording never finished");
        QVERIFY(!timeline.isRecording());
        const int steps = MotionRecorder::steps(timeline.timeline().duration, 30);
        QCOMPARE(read(out() + QStringLiteral("/frames-1")).toInt(), steps);
        QVERIFY(read(file).contains(QByteArray("frames=") + QByteArray::number(steps)));
        QCOMPARE(rateOf(), QStringLiteral("30"));
        // "Recorded 1.2 s · hero.mp4", which opens the folder.
        QVERIFY2(timeline.recordedText().startsWith(QStringLiteral("Recorded ")) && timeline.recordedText().endsWith(QStringLiteral(" s · hero.mp4")),
                 qPrintable(timeline.recordedText()));
        QCOMPARE(done.first().at(0).toString(), file);
        QCOMPARE(record->text(), QStringLiteral("Record MP4"));
        QVERIFY(timeline.findChild<QToolButton *>(QStringLiteral("motionPlay"))->isEnabled());
        // The playhead is where it was.
        QTRY_COMPARE_WITH_TIMEOUT(timeline.playhead(), before, patience);
        QCOMPARE(timeline.recordedPath(), file);
        auto *result = timeline.findChild<QToolButton *>(QStringLiteral("motionRecorded"));
        QVERIFY(result->isVisibleTo(&timeline));
        QCOMPARE(result->text(), timeline.recordedText());
    }

    void theMoreMenuRecordsAtSixtyFramesAndAsAGif()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        auto *more = timeline.findChild<QToolButton *>(QStringLiteral("motionRecordMore"));
        QString asked;
        MotionTimeline::setRecordChooser([&](const MotionTimeline::RecordAsk &ask) {
            asked = QStringLiteral("%1 %2 %3").arg(int(ask.format)).arg(ask.fps).arg(ask.suggested);
            return target(ask.format == FrameRecorder::Format::gif ? QStringLiteral("hero.gif") : QStringLiteral("smooth.mp4"));
        });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        more->menu()->findChild<QAction *>(QStringLiteral("motionRecord60"))->trigger();
        QCOMPARE(asked, QStringLiteral("0 60 site-motion.mp4"));
        QVERIFY(done.wait(patience));
        QCOMPARE(rateOf(1), QStringLiteral("60"));
        QVERIFY(read(target(QStringLiteral("smooth.mp4"))).contains(QByteArray("frames=") + QByteArray::number(MotionRecorder::steps(timeline.timeline().duration, 60))));

        more->menu()->findChild<QAction *>(QStringLiteral("motionRecordGif"))->trigger();
        QCOMPARE(asked, QStringLiteral("1 30 site-motion.gif"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, patience);
        QVERIFY(QFileInfo::exists(target(QStringLiteral("hero.gif"))));
    }

    void aCancelledSaveDialogWritesNothing()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        MotionTimeline::setRecordChooser([](const MotionTimeline::RecordAsk &) { return QString(); });
        QSignalSpy notices(&timeline, &MotionTimeline::notice);
        QCOMPARE(timeline.record(FrameRecorder::Format::mp4), QString());
        QVERIFY(!timeline.isRecording());
        QVERIFY(notices.isEmpty());
        QVERIFY(!QFileInfo::exists(out() + QStringLiteral("/calls")));
    }

    void aNameWithoutItsEndingIsAskedAboutBeforeItReplacesAFile()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        // "clip" is typed while clip.mp4 exists: the dialog asked about "clip", and ffmpeg would overwrite clip.mp4 without asking.
        const QString existing = target(QStringLiteral("clip.mp4"));
        QFile old(existing);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("OLD\n");
        old.close();
        MotionTimeline::setRecordChooser([this](const MotionTimeline::RecordAsk &) { return target(QStringLiteral("clip")); });
        QStringList asked;
        bool replace = false;
        MotionTimeline::setReplaceChooser([&](const QString &path) {
            asked << QFileInfo(path).fileName();
            return replace;
        });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);

        // Declined: nothing runs, and the file is as it was.
        QCOMPARE(timeline.record(FrameRecorder::Format::mp4), QString());
        QCOMPARE(asked, QStringList{"clip.mp4"});
        QVERIFY(!timeline.isRecording());
        QVERIFY(!QFileInfo::exists(out() + QStringLiteral("/calls")));
        QCOMPARE(read(existing), QByteArray("OLD\n"));

        // Accepted: it records into clip.mp4, and there is no file called "clip".
        replace = true;
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QCOMPARE(asked.size(), 2);
        QVERIFY2(done.wait(patience), "the recording never finished");
        QVERIFY(read(existing).contains("frames="));
        QVERIFY(!QFileInfo::exists(target(QStringLiteral("clip"))));
        QCOMPARE(done.first().at(0).toString(), existing);

        // A name that gains its ending and is new asks nothing; one that already has it asks nothing either (the dialog did).
        asked.clear();
        MotionTimeline::setRecordChooser([this](const MotionTimeline::RecordAsk &) { return target(QStringLiteral("fresh")); });
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, patience);
        QVERIFY(QFileInfo::exists(target(QStringLiteral("fresh.mp4"))));
        MotionTimeline::setRecordChooser([existing](const MotionTimeline::RecordAsk &) { return existing; });
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, patience);
        QVERIFY(asked.isEmpty());
    }

    void stopLiveWhileRecordingLeavesNoFile()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString file = target(QStringLiteral("stoplive.mp4"));
        MotionTimeline::setRecordChooser([file](const MotionTimeline::RecordAsk &) { return file; });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file), patience);
        // Stop Live ends the frame's session: the timeline closes, and the file was never whole.
        LiveFrames::of(session)->stop(hosted.frame);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.isOpen(), patience);
        QVERIFY(!timeline.isRecording());
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(timeline.recordedText().isEmpty());
        QTest::qWait(300);
        QVERIFY(done.isEmpty());
        QVERIFY(!QFileInfo::exists(file));
    }

    void stopKeepsTheFileWhereItEnded()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString file = target(QStringLiteral("stopped.mp4"));
        MotionTimeline::setRecordChooser([file](const MotionTimeline::RecordAsk &) { return file; });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        // Stop is the header's button.
        auto *record = timeline.findChild<QToolButton *>(QStringLiteral("motionRecord"));
        QTest::qWait(150);
        record->click();
        QVERIFY2(done.wait(patience), "Stop never ended the file");
        const int steps = MotionRecorder::steps(timeline.timeline().duration, 30);
        const int kept = read(out() + QStringLiteral("/frames-1")).toInt();
        QVERIFY2(kept >= 1 && kept <= steps, qPrintable(QString::number(kept)));
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(!timeline.isRecording());
    }

    void escStopsTheRecordingAndTheNextEscLeavesEditPage()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString file = target(QStringLiteral("esc.mp4"));
        MotionTimeline::setRecordChooser([file](const MotionTimeline::RecordAsk &) { return file; });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTest::qWait(100);
        QTest::keyClick(&hosted.canvas, Qt::Key_Escape);
        QVERIFY2(done.wait(patience), "Esc never ended the file");
        // Edit Page and the timeline are still there.
        QVERIFY(timeline.isOpen());
        QCOMPARE(hosted.canvas.editPageFrame(), std::optional<QUuid>(hosted.frame));
        QVERIFY(QFileInfo::exists(file));
        // The second Esc is Edit Page's, and the timeline goes with it.
        QTest::keyClick(&hosted.canvas, Qt::Key_Escape);
        QVERIFY(!hosted.canvas.editPageFrame());
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.isOpen(), patience);
    }

    void closingTheTimelineWhileRecordingLeavesNoFile()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString file = target(QStringLiteral("closed.mp4"));
        MotionTimeline::setRecordChooser([file](const MotionTimeline::RecordAsk &) { return file; });
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file), patience);
        timeline.close();
        QVERIFY(!timeline.isRecording());
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(timeline.recordedText().isEmpty());
        QTest::qWait(300);
        QVERIFY(done.isEmpty());
        QVERIFY(!QFileInfo::exists(file));
    }

    void aFailingFfmpegSaysWhyAndRemovesTheFile()
    {
        NEEDS_CHROMIUM;
        qputenv("FAKE_FFMPEG_MODE", "fail-at-start");
        EditorSession session;
        Hosted hosted(session, page());
        MotionTimeline timeline(session, hosted.canvas);
        openAndWait(session, timeline, hosted.frame);
        const QString file = target(QStringLiteral("broken.mp4"));
        MotionTimeline::setRecordChooser([file](const MotionTimeline::RecordAsk &) { return file; });
        QSignalSpy notices(&timeline, &MotionTimeline::notice);
        QSignalSpy done(&timeline, &MotionTimeline::recorded);
        QVERIFY(timeline.record(FrameRecorder::Format::mp4).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.isRecording(), patience);
        QVERIFY(done.isEmpty());
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Couldn't record: Unknown encoder 'libx264'"));
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(timeline.recordedText().isEmpty());
        // The header is as it was: Record works again.
        QVERIFY(timeline.findChild<QToolButton *>(QStringLiteral("motionRecord"))->isEnabled());
    }
};

QTEST_MAIN(MotionRecordTimelineTests)
#include "MotionRecordTimelineTests.moc"
