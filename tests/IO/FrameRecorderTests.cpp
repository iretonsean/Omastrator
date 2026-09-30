#include "IO/FrameRecorder.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// The pictures-to-file part of Record (docs/MOTION.md, section 8). ffmpeg is a fake that records its arguments and counts
// the JPEG pictures it is given; one test runs the real ffmpeg, and skips without it and ffprobe.
namespace {
QByteArray jpeg(int index, QSize size = QSize(64, 36))
{
    QImage picture(size, QImage::Format_RGB32);
    picture.fill(QColor::fromHsv((index * 12) % 360, 200, 220));
    QPainter painter(&picture);
    painter.fillRect(QRect(index % size.width(), 0, 4, size.height()), Qt::white);
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "JPEG", 90);
    return bytes;
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class FrameRecorderTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString out() { return m_directory.filePath(QStringLiteral("fake")); }
    QString target(const QString &name) { return m_directory.filePath(name); }
    // The arguments of run `n`, in order.
    QStringList call(int n = 1)
    {
        const QStringList blocks = QString::fromUtf8(read(out() + QStringLiteral("/calls"))).split(QStringLiteral("\n---\n"), Qt::SkipEmptyParts);
        return blocks.value(n - 1).split(QLatin1Char('\n'));
    }
    int counted(int n = 1) { return read(out() + QStringLiteral("/frames-%1").arg(n)).toInt(); }

    // Feeds `count` pictures and waits for the answer to finish().
    QString record(FrameRecorder &recorder, int count, QSize size = QSize(64, 36))
    {
        for (int i = 0; i < count; ++i) {
            const QString failure = recorder.add(jpeg(i, size));
            if (!failure.isEmpty())
                return failure;
            // A caller that outruns the encoder waits.
            while (recorder.pending() > 4 * 1024 * 1024)
                QTest::qWait(5);
        }
        QSignalSpy done(&recorder, &FrameRecorder::finished);
        recorder.finish();
        if (done.isEmpty() && !done.wait(60'000))
            return QStringLiteral("finish() never answered");
        return done.first().first().toString();
    }

private slots:
    void initTestCase() { QVERIFY(m_directory.isValid()); }

    void init()
    {
        QDir(out()).removeRecursively();
        QDir().mkpath(out());
        qputenv("OMASTRATOR_FFMPEG", FAKE_FFMPEG);
        qputenv("FAKE_FFMPEG_OUT", out().toUtf8());
        qunsetenv("FAKE_FFMPEG_MODE");
    }

    void twoSecondsAtThirtyFramesSendsSixtyPictures()
    {
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("hero.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QVERIFY(recorder.active());
        QVERIFY2(record(recorder, 60).isEmpty(), "the recording failed");
        QCOMPARE(recorder.frames(), 60);
        QCOMPARE(counted(), 60);
        QVERIFY(read(file).contains("frames=60"));
        QVERIFY(!recorder.active());
        const QStringList arguments = call();
        // The pictures come on stdin at the frame rate, as H.264 in yuv420p with the moov atom first.
        const auto has = [&](const QStringList &words) { return arguments.join(QLatin1Char(' ')).contains(words.join(QLatin1Char(' '))); };
        QVERIFY2(has({"-f", "image2pipe", "-framerate", "30", "-c:v", "mjpeg", "-i", "-"}), qPrintable(arguments.join(' ')));
        QVERIFY(has({"-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2:out_range=tv"}));
        QVERIFY(has({"-c:v", "libx264", "-pix_fmt", "yuv420p", "-movflags", "+faststart", file}));
        QCOMPARE(arguments.last(), file);
    }

    void sixtyFramesASecondIsInTheArguments()
    {
        FrameRecorder recorder;
        QVERIFY(recorder.start({target(QStringLiteral("smooth.mp4")), FrameRecorder::Format::mp4, 60}).isEmpty());
        QVERIFY(record(recorder, 6).isEmpty());
        const QStringList arguments = call();
        QCOMPARE(arguments.value(arguments.indexOf(QStringLiteral("-framerate")) + 1), QStringLiteral("60"));
    }

    void stopKeepsTheFileWhereItEnded()
    {
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("stopped.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QVERIFY(record(recorder, 12).isEmpty());
        QVERIFY(read(file).contains("frames=12"));
    }

    void ffmpegFailingAtOnceRemovesTheFileAndSaysWhy()
    {
        qputenv("FAKE_FFMPEG_MODE", "fail-at-start");
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("broken.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QString failure;
        for (int i = 0; i < 400 && failure.isEmpty(); ++i) {
            failure = recorder.add(jpeg(i));
            QTest::qWait(10);
        }
        QCOMPARE(failure, QStringLiteral("Couldn't record: Unknown encoder 'libx264'"));
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!recorder.active());
        // Stop after that has nothing left to do.
        QSignalSpy done(&recorder, &FrameRecorder::finished);
        recorder.finish();
        QVERIFY(done.isEmpty());
    }

    void ffmpegFailingAtTheEndRemovesTheFileToo()
    {
        qputenv("FAKE_FFMPEG_MODE", "fail-at-end");
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("late.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QCOMPARE(record(recorder, 5), QStringLiteral("Couldn't record: Error while encoding: Invalid argument"));
        QVERIFY(!QFileInfo::exists(file));
    }

    void noPictureMeansNothingIsKept()
    {
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("empty.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QCOMPARE(record(recorder, 0), QStringLiteral("Couldn't record: no picture was taken."));
        QVERIFY(!QFileInfo::exists(file));
    }

    void abortRemovesTheHalfFile()
    {
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("aborted.mp4"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
        QVERIFY(recorder.add(jpeg(0)).isEmpty());
        QTRY_VERIFY(QFileInfo::exists(file));
        recorder.abort();
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!recorder.active());
        // Deleting a recorder that is running does the same.
        const QString other = target(QStringLiteral("dropped.mp4"));
        {
            FrameRecorder dropped;
            QVERIFY(dropped.start({other, FrameRecorder::Format::mp4, 30}).isEmpty());
            QVERIFY(dropped.add(jpeg(0)).isEmpty());
            QTRY_VERIFY(QFileInfo::exists(other));
        }
        QVERIFY(!QFileInfo::exists(other));
    }

    void aGifIsTwoPassesOverTheSamePictures()
    {
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("hero.gif"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::gif, 30}).isEmpty());
        QVERIFY2(record(recorder, 20).isEmpty(), "the recording failed");
        QCOMPARE(counted(1), 20);
        QCOMPARE(counted(2), 20);
        const QString first = call(1).join(QLatin1Char(' '));
        const QString second = call(2).join(QLatin1Char(' '));
        QVERIFY(first.contains(QStringLiteral("-vf palettegen")) && first.contains(QStringLiteral("%05d.jpg")));
        QVERIFY(second.contains(QStringLiteral("-lavfi paletteuse")) && second.contains(QStringLiteral("palette.png")) && second.contains(QStringLiteral("%05d.jpg")));
        QVERIFY(second.endsWith(file));
        QVERIFY(read(file).contains("frames=20"));
        // The pictures were beside the file, and they are gone.
        for (const QFileInfo &info : QDir(m_directory.path()).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
            QVERIFY2(!info.fileName().startsWith(QLatin1String(".omastrator-frames")), qPrintable(info.fileName()));
    }

    void aGifWhoseSecondPassFailsIsRemoved()
    {
        qputenv("FAKE_FFMPEG_MODE", "fail-pass2");
        FrameRecorder recorder;
        const QString file = target(QStringLiteral("bad.gif"));
        QVERIFY(recorder.start({file, FrameRecorder::Format::gif, 30}).isEmpty());
        QCOMPARE(record(recorder, 4), QStringLiteral("Couldn't record: Error initializing filter 'paletteuse'"));
        QVERIFY(!QFileInfo::exists(file));
    }

    void framesAsPngAreANumberedSequenceInANewFolder()
    {
        // No ffmpeg at all: PNG frames still work.
        qputenv("OMASTRATOR_FFMPEG", target(QStringLiteral("no-such-ffmpeg")).toUtf8());
        QVERIFY(FrameRecorder::ffmpeg().isEmpty());
        FrameRecorder recorder;
        const QString folder = target(QStringLiteral("hero-frames"));
        QVERIFY(recorder.start({folder, FrameRecorder::Format::pngFrames, 30}).isEmpty());
        QVERIFY2(record(recorder, 5).isEmpty(), "the recording failed");
        const QStringList names = QDir(folder).entryList(QDir::Files, QDir::Name);
        QCOMPARE(names, (QStringList{"frame-0001.png", "frame-0002.png", "frame-0003.png", "frame-0004.png", "frame-0005.png"}));
        const QImage first(folder + QStringLiteral("/frame-0001.png"));
        QCOMPARE(first.size(), QSize(64, 36));

        // The folder is new: one that has files in it is refused.
        FrameRecorder again;
        const QString refused = again.start({folder, FrameRecorder::Format::pngFrames, 30});
        QVERIFY(!refused.isEmpty());
        QVERIFY(refused.contains(QStringLiteral("already exists")));
        // Abort takes the folder away.
        FrameRecorder aborted;
        const QString other = target(QStringLiteral("aborted-frames"));
        QVERIFY(aborted.start({other, FrameRecorder::Format::pngFrames, 30}).isEmpty());
        QVERIFY(aborted.add(jpeg(0)).isEmpty());
        aborted.abort();
        QVERIFY(!QFileInfo::exists(other));
    }

    void withoutFfmpegAnMp4CannotStart()
    {
        qputenv("OMASTRATOR_FFMPEG", target(QStringLiteral("no-such-ffmpeg")).toUtf8());
        FrameRecorder recorder;
        QCOMPARE(recorder.start({target(QStringLiteral("x.mp4")), FrameRecorder::Format::mp4, 30}), FrameRecorder::ffmpegHint());
        QCOMPARE(FrameRecorder::ffmpegHint(), QStringLiteral("Recording needs ffmpeg. Install it with sudo pacman -S ffmpeg."));
        QCOMPARE(recorder.start({target(QStringLiteral("x.gif")), FrameRecorder::Format::gif, 30}), FrameRecorder::ffmpegHint());
        QVERIFY(!QFileInfo::exists(target(QStringLiteral("x.mp4"))));
    }

    void theRealFfmpegMakesAnMp4AndAGifOfTheRightShape()
    {
        qunsetenv("OMASTRATOR_FFMPEG");
        const QString ffmpeg = FrameRecorder::ffmpeg();
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobe.isEmpty())
            QSKIP("ffmpeg and ffprobe aren't installed.");
        // An odd size, as a frame on screen can be.
        const QSize size(321, 181);
        const auto probe = [&](const QString &file, const QString &entries) {
            QProcess process;
            process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-count_frames"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                    QStringLiteral("-show_entries"), entries, QStringLiteral("-of"), QStringLiteral("default=nw=1"), file});
            process.waitForFinished(30'000);
            QMap<QString, QString> values;
            for (const QString &line : QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                values.insert(line.section(QLatin1Char('='), 0, 0), line.section(QLatin1Char('='), 1));
            return values;
        };
        {
            FrameRecorder recorder;
            const QString file = target(QStringLiteral("real.mp4"));
            QVERIFY(recorder.start({file, FrameRecorder::Format::mp4, 30}).isEmpty());
            QVERIFY2(record(recorder, 30, size).isEmpty(), "ffmpeg failed");
            const auto info = probe(file, QStringLiteral("stream=codec_name,pix_fmt,width,height,nb_read_frames"));
            QCOMPARE(info.value(QStringLiteral("codec_name")), QStringLiteral("h264"));
            QCOMPARE(info.value(QStringLiteral("pix_fmt")), QStringLiteral("yuv420p"));
            // The odd side loses one pixel: yuv420p needs even ones.
            QCOMPARE(info.value(QStringLiteral("width")), QStringLiteral("320"));
            QCOMPARE(info.value(QStringLiteral("height")), QStringLiteral("180"));
            QCOMPARE(info.value(QStringLiteral("nb_read_frames")), QStringLiteral("30"));
        }
        {
            FrameRecorder recorder;
            const QString file = target(QStringLiteral("real.gif"));
            QVERIFY(recorder.start({file, FrameRecorder::Format::gif, 30}).isEmpty());
            QVERIFY2(record(recorder, 10, size).isEmpty(), "ffmpeg failed");
            const auto info = probe(file, QStringLiteral("stream=codec_name,width,height,nb_read_frames"));
            QCOMPARE(info.value(QStringLiteral("codec_name")), QStringLiteral("gif"));
            QCOMPARE(info.value(QStringLiteral("width")), QStringLiteral("321"));
            QCOMPARE(info.value(QStringLiteral("nb_read_frames")), QStringLiteral("10"));
        }
    }
};

QTEST_MAIN(FrameRecorderTests)
#include "FrameRecorderTests.moc"
