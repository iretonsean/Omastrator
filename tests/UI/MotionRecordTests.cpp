#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "IO/FrameRecorder.h"
#include "Live/Browser.h"
#include "Live/StaticServer.h"
#include "UI/BrowserViews.h"
#include "UI/MotionRecorder.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <memory>

// Record (docs/MOTION.md, section 8): a page's motion played by seeking, one picture per step, into ffmpeg. The page is a
// real headless Chromium tab on a throwaway profile; ffmpeg is a fake that counts and keeps the pictures. Skips without Chromium.
namespace {
constexpr int patience = 60'000;

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The JPEGs in a run of them.
QList<QImage> pictures(const QByteArray &all)
{
    QList<QImage> list;
    QList<qsizetype> starts;
    for (qsizetype at = all.indexOf("\xff\xd8\xff"); at >= 0; at = all.indexOf("\xff\xd8\xff", at + 3))
        starts << at;
    for (qsizetype i = 0; i < starts.size(); ++i)
        list << QImage::fromData(all.mid(starts[i], (i + 1 < starts.size() ? starts[i + 1] : all.size()) - starts[i]), "JPEG");
    return list;
}

// The left edge of the blue box, in pixels; -1 when there is none. The box is 60 px tall at 60 px down a page 400 px tall.
int boxLeft(const QImage &picture)
{
    const int row = std::min(picture.height() * 9 / 40, picture.height() - 1);
    for (int x = 0; x < picture.width(); ++x) {
        const QColor colour = picture.pixelColor(x, row);
        if (colour.blue() > 200 && colour.red() < 80)
            return x;
    }
    return -1;
}
}

class MotionRecordTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;

    QString out() { return m_directory.filePath(QStringLiteral("fake")); }
    QString target(const QString &name) { return m_directory.filePath(name); }
    int counted() { return read(out() + QStringLiteral("/frames-1")).toInt(); }

    struct Rig {
        EditorSession session;
        EditorCanvas canvas{session};
        QUuid frame;
    };
    // One frame on the fixture, 600 × 400, shown so its tab streams.
    std::unique_ptr<Rig> rig()
    {
        auto made = std::make_unique<Rig>();
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 600, 400}, QStringLiteral("Record"));
        view.browser = BrowserView{m_server.url(), {}, {}};
        made->frame = view.id;
        document.insert(view, document.layers().front());
        made->session.loadDocument(document);
        made->canvas.resize(1000, 800);
        made->canvas.show();
        // Large on screen whatever size the offscreen window settles at: a small frame is paused.
        made->session.zoomToRect(QRectF(0, 0, 700, 500));
        BrowserViews::of(made->session)->attach(&made->canvas);
        BrowserViews *views = BrowserViews::of(made->session);
        for (int i = 0; i < 6000 && views->state(made->frame) != BrowserViews::State::live; ++i)
            QTest::qWait(10);
        return made;
    }

    // Puts the page's slide at `ms`, through the tab, and answers once it has.
    static std::function<void(double, std::function<void(const QString &)>)> seeker(BrowserViews *views, const QUuid &frame, QString failWith = {})
    {
        return [views, frame, failWith](double ms, std::function<void(const QString &)> done) {
            if (!failWith.isEmpty()) {
                QMetaObject::invokeMethod(qApp, [done, failWith] { done(failWith); }, Qt::QueuedConnection);
                return;
            }
            BrowserViews::pool()->call(views->poolKey(frame), QStringLiteral("Runtime.evaluate"), {{"expression", QStringLiteral("seekTo(%1)").arg(ms)}},
                                       [done](const QJsonObject &, const QString &error) {
                                           QMetaObject::invokeMethod(qApp, [done, error] { done(error); }, Qt::QueuedConnection);
                                       });
        };
    }

    MotionRecorder::Job job(Rig &r, const QString &file, double ms, int fps = 30)
    {
        MotionRecorder::Job made;
        made.frame = r.frame;
        made.path = file;
        made.fps = fps;
        made.durationMs = ms;
        made.seek = seeker(BrowserViews::of(r.session), r.frame);
        return made;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        QVERIFY2(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/record")).isEmpty(), "the fixture must be served");
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        QDir(out()).removeRecursively();
        QDir().mkpath(out());
        qputenv("OMASTRATOR_FFMPEG", FAKE_FFMPEG);
        qputenv("FAKE_FFMPEG_OUT", out().toUtf8());
        qputenv("FAKE_FFMPEG_KEEP", "1");
        qunsetenv("FAKE_FFMPEG_MODE");
    }

    void cleanup() { BrowserViews::shutdownPool(); }

    void aMotionIsAsManyPicturesAsItsSecondsAtTheFrameRate()
    {
        QCOMPARE(MotionRecorder::steps(2000, 30), 60);
        QCOMPARE(MotionRecorder::steps(1000, 60), 60);
        QCOMPARE(MotionRecorder::steps(2060, 30), 62);
        QCOMPARE(MotionRecorder::steps(1, 30), 1);
    }

    void twoSecondsAtThirtyFramesSendsSixtyPicturesOfTheSeekedPage()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        auto r = rig();
        QTRY_VERIFY_WITH_TIMEOUT(BrowserViews::of(r->session)->state(r->frame) == BrowserViews::State::live, patience);
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        QSignalSpy progress(&recorder, &MotionRecorder::progress);
        const QString file = target(QStringLiteral("hero.mp4"));
        QVERIFY(recorder.start(job(*r, file, 2000)).isEmpty());
        QVERIFY(recorder.recording());
        QCOMPARE(recorder.total(), 60);
        QVERIFY2(done.wait(patience), "the recording never finished");
        QCOMPARE(done.first().at(0).toString(), QString());
        QCOMPARE(done.first().at(1).toString(), file);
        QCOMPARE(done.first().at(2).toDouble(), 2.0);
        QVERIFY(!recorder.recording());
        QCOMPARE(counted(), 60);
        QVERIFY(read(file).contains("frames=60"));
        QCOMPARE(progress.last().at(0).toInt(), 60);

        // The box slides from the left edge to the right one, a little further each step: the seeks landed before each picture.
        const QList<QImage> shots = pictures(read(out() + QStringLiteral("/stdin-1")));
        QCOMPARE(shots.size(), 60);
        QVERIFY(shots.front().width() <= 2560 && shots.front().width() > 100);
        int last = -1;
        for (const QImage &shot : shots) {
            QCOMPARE(shot.size(), shots.front().size());
            const int at = boxLeft(shot);
            QVERIFY2(at >= 0, "no box in a picture");
            QVERIFY2(at >= last, qPrintable(QStringLiteral("the box moved back: %1 after %2").arg(at).arg(last)));
            last = at;
        }
        QVERIFY(boxLeft(shots.front()) < shots.front().width() / 10);
        // The slide is 240 of the page's 600 px.
        QVERIFY(boxLeft(shots.back()) > shots.back().width() * 3 / 10);
        QVERIFY(boxLeft(shots.back()) < shots.back().width() * 5 / 10);
        QVERIFY(boxLeft(shots.at(30)) > boxLeft(shots.at(5)));
    }

    void stopEndsTheFileWhereItIsAndKeepsIt()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        auto r = rig();
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        const QString file = target(QStringLiteral("stopped.mp4"));
        QVERIFY(recorder.start(job(*r, file, 4000)).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(recorder.step() >= 12, patience);
        recorder.stop();
        QVERIFY2(done.wait(patience), "Stop never ended the file");
        QCOMPARE(done.first().at(0).toString(), QString());
        const int kept = counted();
        QVERIFY2(kept >= 12 && kept < 120, qPrintable(QString::number(kept)));
        QVERIFY(read(file).contains(QByteArray("frames=") + QByteArray::number(kept)));
        QCOMPARE(done.first().at(2).toDouble(), kept / 30.0);
    }

    void aFailingFfmpegRemovesTheFileAndSaysWhy()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        qputenv("FAKE_FFMPEG_MODE", "fail-at-start");
        auto r = rig();
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        const QString file = target(QStringLiteral("broken.mp4"));
        QVERIFY(recorder.start(job(*r, file, 2000)).isEmpty());
        QVERIFY2(done.wait(patience), "the failure never came");
        QCOMPARE(done.first().at(0).toString(), QStringLiteral("Couldn't record: Unknown encoder 'libx264'"));
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!recorder.recording());
    }

    void aSeekThatFailsEndsItWithoutAFile()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        auto r = rig();
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        const QString file = target(QStringLiteral("noseek.mp4"));
        MotionRecorder::Job made = job(*r, file, 2000);
        made.seek = seeker(BrowserViews::of(r->session), r->frame, QStringLiteral("The timeline closed."));
        QVERIFY(recorder.start(made).isEmpty());
        QVERIFY(done.wait(patience));
        QCOMPARE(done.first().at(0).toString(), QStringLiteral("Couldn't record: The timeline closed."));
        QVERIFY(!QFileInfo::exists(file));
    }

    void withoutFfmpegTheFramesAreAnumberedPngSequence()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        qputenv("OMASTRATOR_FFMPEG", target(QStringLiteral("no-such-ffmpeg")).toUtf8());
        auto r = rig();
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        const QString folder = target(QStringLiteral("hero-frames"));
        MotionRecorder::Job made = job(*r, folder, 500);
        made.format = FrameRecorder::Format::pngFrames;
        QVERIFY(recorder.start(made).isEmpty());
        QVERIFY(done.wait(patience));
        QCOMPARE(done.first().at(0).toString(), QString());
        const QStringList names = QDir(folder).entryList(QDir::Files, QDir::Name);
        QCOMPARE(names.size(), 15);
        QCOMPARE(names.front(), QStringLiteral("frame-0001.png"));
        QCOMPARE(names.back(), QStringLiteral("frame-0015.png"));
        QVERIFY(!QImage(folder + QLatin1Char('/') + names.front()).isNull());
    }

    void theRealFfmpegMakesAWholeMp4OfTheMotion()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        qunsetenv("OMASTRATOR_FFMPEG");
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (FrameRecorder::ffmpeg().isEmpty() || ffprobe.isEmpty())
            QSKIP("ffmpeg and ffprobe aren't installed.");
        auto r = rig();
        MotionRecorder recorder(*BrowserViews::of(r->session));
        QSignalSpy done(&recorder, &MotionRecorder::finished);
        const QString file = target(QStringLiteral("real.mp4"));
        QVERIFY(recorder.start(job(*r, file, 1000)).isEmpty());
        QVERIFY(done.wait(patience));
        QCOMPARE(done.first().at(0).toString(), QString());
        QProcess probe;
        probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-count_frames"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                              QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name,pix_fmt,nb_read_frames,duration"), QStringLiteral("-of"),
                              QStringLiteral("default=nw=1"), file});
        probe.waitForFinished(30'000);
        const QString said = QString::fromUtf8(probe.readAllStandardOutput());
        QVERIFY2(said.contains(QStringLiteral("codec_name=h264")), qPrintable(said));
        QVERIFY2(said.contains(QStringLiteral("pix_fmt=yuv420p")), qPrintable(said));
        QVERIFY2(said.contains(QStringLiteral("nb_read_frames=30")), qPrintable(said));
        QVERIFY2(said.contains(QStringLiteral("duration=1.000000")), qPrintable(said));
    }

    void aRecordingAbortedLeavesNothing()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        auto r = rig();
        const QString file = target(QStringLiteral("aborted.mp4"));
        {
            MotionRecorder recorder(*BrowserViews::of(r->session));
            QVERIFY(recorder.start(job(*r, file, 4000)).isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(recorder.step() >= 3, patience);
            QVERIFY(QFileInfo::exists(file));
        }
        QVERIFY(!QFileInfo::exists(file));
    }

    void noMotionOrNoSeekIsRefused()
    {
        EditorSession session;
        session.loadDocument(VectorDocument::blank({100, 100}));
        MotionRecorder recorder(*BrowserViews::of(session));
        MotionRecorder::Job made;
        made.path = target(QStringLiteral("none.mp4"));
        QCOMPARE(recorder.start(made), QStringLiteral("There is no motion to record."));
        QVERIFY(!QFileInfo::exists(made.path));
    }
};

QTEST_MAIN(MotionRecordTests)
#include "MotionRecordTests.moc"
