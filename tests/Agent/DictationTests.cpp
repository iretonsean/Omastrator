#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Agent/Dictation.h"
#include "Agent/Island.h"
#include "Agent/Vocabulary.h"
#include "Document/PathOperations.h"
#include "FakeAgentHost.h"
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <thread>

// Phase 7 of docs/OS-SUITE.md: the vocabulary, normalisation, the two tiers, Heard and cancel, and voxtype.
namespace {
struct Backend {
    FakeAgentHost host;
    AgentTools tools{host};
    AgentServer server{tools};
};

void script(const QString &path, const QByteArray &body)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString heard(const QString &text)
{
    return Dictation::parse(Dictation::normalize(text)).description;
}
}

class DictationTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QThread m_thread;
    QObject *m_anchor = nullptr;
    Backend *m_backend = nullptr;

    template<typename Work>
    void onBackend(Work work)
    {
        QMetaObject::invokeMethod(m_anchor, work, Qt::BlockingQueuedConnection);
    }

    int dictate(const QStringList &args, QString *out = nullptr)
    {
        QString output, errors;
        QTextStream outStream(&output), errStream(&errors);
        const int code = Dictation::runCli(args, outStream, errStream);
        outStream.flush();
        errStream.flush();
        if (out)
            *out = (output + errors).trimmed();
        return code;
    }

private slots:
    void cleanup()
    {
        qunsetenv("OMASTRATOR_VOXTYPE");
    }

    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        // Nothing here may pop a notification on the real desktop.
        qputenv("OMASTRATOR_NOTIFY", "/bin/true");
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("OMASTRATOR_APP", "/bin/true");
        // Nothing here may change the session's key state.
        qputenv("OMASTRATOR_HYPRCTL", "/bin/true");
        m_anchor = new QObject;
        m_anchor->moveToThread(&m_thread);
        m_thread.start();
        QString failure;
        onBackend([&] {
            m_backend = new Backend;
            m_backend->host.editor.createDocument({200, 100});
            failure = m_backend->server.listen();
        });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void cleanupTestCase()
    {
        onBackend([&] { delete m_backend; });
        QMetaObject::invokeMethod(m_anchor, &QObject::deleteLater);
        m_thread.quit();
        m_thread.wait();
    }

    void normalizationFixesWhatWhisperMishears()
    {
        QCOMPARE(Dictation::normalize(QStringLiteral("Set the fill to hash F F six six zero zero.")), QStringLiteral("set the fill to #ff6600"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Set the fill to hat F6600.")), QStringLiteral("set the fill to hat f6600"));
        QCOMPARE(Dictation::normalize(QStringLiteral("fill hashtag double f, double zero, zero zero")), QStringLiteral("fill #ff0000"));
        // Five digits is no colour: the words stay for the agent.
        QCOMPARE(Dictation::normalize(QStringLiteral("fill hash f six six zero zero")), QStringLiteral("fill hash f 6 6 0 0"));
        QCOMPARE(Dictation::normalize(QStringLiteral("stroke #0A84FF")), QStringLiteral("stroke #0a84ff"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Apply path finder, minus fronts.")), QStringLiteral("apply pathfinder minus front"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Pick the eye-dropper")), QStringLiteral("pick the eyedropper"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Stroke weight twenty four points")), QStringLiteral("stroke weight 24 points"));
        QCOMPARE(Dictation::normalize(QStringLiteral("opacity fifty percent")), QStringLiteral("opacity 50 percent"));
        QCOMPARE(Dictation::normalize(QStringLiteral("opacity 50%")), QStringLiteral("opacity 50 percent"));
        QCOMPARE(Dictation::normalize(QStringLiteral("one hundred five, six six")), QStringLiteral("105 6 6"));
        QCOMPARE(Dictation::normalize(QStringLiteral("zero point five")), QStringLiteral("0.5"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Centre it, colour it grey")), QStringLiteral("center it color it gray"));
        QCOMPARE(Dictation::normalize(QStringLiteral("Tighten the kerning.")), QStringLiteral("tighten the kerning"));
    }

    void tierOneIsTheLocalGrammar()
    {
        QCOMPARE(heard("Select the pen tool."), QStringLiteral("Pen tool"));
        QCOMPARE(heard("switch to the direct selection tool"), QStringLiteral("Direct Selection tool"));
        QCOMPARE(heard("use circle"), QStringLiteral("Ellipse tool"));
        QCOMPARE(heard("Please undo that."), QStringLiteral("Undo"));
        QCOMPARE(heard("redo"), QStringLiteral("Redo"));
        QCOMPARE(heard("Zoom in."), QStringLiteral("Zoom In"));
        QCOMPARE(heard("fit to the window"), QStringLiteral("Fit Artboard in Window"));
        QCOMPARE(heard("zoom to one hundred percent"), QStringLiteral("Actual Size"));
        QCOMPARE(heard("Align left."), QStringLiteral("Align Left"));
        QCOMPARE(heard("align the selection to the vertical center"), QStringLiteral("Align Vertical Centers"));
        QCOMPARE(heard("distribute them horizontally"), QStringLiteral("Distribute Horizontally"));
        QCOMPARE(heard("bring it to the front"), QStringLiteral("Bring to Front"));
        QCOMPARE(heard("send backward"), QStringLiteral("Send Backward"));
        QCOMPARE(heard("Group them."), QStringLiteral("Group"));
        QCOMPARE(heard("ungroup"), QStringLiteral("Ungroup"));
        QCOMPARE(heard("delete that"), QStringLiteral("Delete"));
        QCOMPARE(heard("duplicate it"), QStringLiteral("Duplicate"));
        QCOMPARE(heard("Set the fill to hash F F six six zero zero."), QStringLiteral("Fill #ff6600"));
        QCOMPARE(heard("make it sky blue"), QStringLiteral("Fill #87ceeb"));
        QCOMPARE(heard("stroke color red"), QStringLiteral("Stroke #ff0000"));
        QCOMPARE(heard("Stroke weight four points"), QStringLiteral("Stroke Weight 4 pt"));
        QCOMPARE(heard("set opacity to fifty percent"), QStringLiteral("Opacity 50%"));
        QCOMPARE(heard("select all"), QStringLiteral("Select All"));

        const Dictation::Command align = Dictation::parse(Dictation::normalize(QStringLiteral("align top to the artboard")));
        QCOMPARE(align.method, QStringLiteral("command"));
        QCOMPARE(align.params["edge"].toString(), QStringLiteral("top"));
        QCOMPARE(align.params["target"].toString(), QStringLiteral("artboard"));
        const Dictation::Command pen = Dictation::parse(Dictation::normalize(QStringLiteral("select the pen tool")));
        QCOMPARE(pen.method, QStringLiteral("select_tool"));
        QCOMPARE(pen.params["tool"].toString(), QStringLiteral("pen"));
        QCOMPARE(pen.tier, 1);
    }

    void everythingElseAsksTheAgent()
    {
        for (const char *request : {"Make the logo rounder.", "tighten the kerning on the headline", "apply pathfinder minus front",
                                    "set the fill to hat F6600", "select the logo"}) {
            const Dictation::Command command = Dictation::parse(Dictation::normalize(QLatin1String(request)));
            QCOMPARE(command.tier, 2);
            QCOMPARE(command.method, QStringLiteral("ai_start"));
            QCOMPARE(command.params["flow"].toString(), QStringLiteral("edit"));
            QVERIFY(command.description.startsWith(QLatin1String("Ask the agent: ")));
        }
        QCOMPARE(Dictation::parse(QString()).tier, 0);
        QVERIFY(Dictation::isCancel(Dictation::normalize(QStringLiteral("Never mind."))));
        QCOMPARE(Dictation::parse(Dictation::normalize(QStringLiteral("cancel"))).kind, QStringLiteral("cancel"));
    }

    void theVocabularyPromptsWhisper()
    {
        QVERIFY(Dictation::initialPrompt().contains(QLatin1String("Pathfinder")));
        QVERIFY(Dictation::initialPrompt().contains(QLatin1String("hash F F six six zero zero")));
        QVERIFY(Dictation::initialPrompt().size() <= 800);
        // The user's own words come in through their vocabulary file.
        QFile file(Vocabulary::path());
        QDir().mkpath(QFileInfo(file).absolutePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# mine\nOmarchy\nGraphite\n");
        file.close();
        QVERIFY(Dictation::initialPrompt().contains(QLatin1String("Omarchy, Graphite")));
        QFile::remove(Vocabulary::path());
    }

    void heardWaitsThenRunsAndCancels()
    {
        // voxtype stands in: it "hears" whatever $FAKE_HEARD says.
        const QString fake = m_directory.filePath(QStringLiteral("voxtype"));
        script(fake, "[ \"$1\" = -q ] || exit 3\necho 'whisper_init_state: noise' >&2\nprintf '\\n%s\\n' \"$FAKE_HEARD\"\n");
        qputenv("OMASTRATOR_VOXTYPE", fake.toUtf8());
        const QString wav = m_directory.filePath(QStringLiteral("x.wav"));
        QFile touched(wav);
        QVERIFY(touched.open(QIODevice::WriteOnly));
        touched.close();

        // The first time, "Heard" waits for Esc before the pen is chosen.
        qputenv("FAKE_HEARD", "Select the pen tool.");
        QElapsedTimer clock;
        clock.start();
        QString out;
        QCOMPARE(dictate({QStringLiteral("file"), wav}, &out), 0);
        QVERIFY(clock.elapsed() >= 2000);
        QCOMPARE(out, QStringLiteral("Pen tool"));
        QVERIFY(Island::read().activity.startsWith(QStringLiteral("Heard: “Select the pen tool.” → Pen tool · Esc cancels")));
        Tool tool = Tool::select;
        onBackend([&] { tool = m_backend->host.editor.tool(); });
        QCOMPARE(tool, Tool::pen);

        // After the first use, a tool switch runs at once.
        qputenv("FAKE_HEARD", "Use the rectangle tool");
        clock.restart();
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        QVERIFY(clock.elapsed() < 2000);
        onBackend([&] { tool = m_backend->host.editor.tool(); });
        QCOMPARE(tool, Tool::rectangle);

        // Esc while it waits: nothing runs.
        QUuid shape;
        onBackend([&] { shape = m_backend->host.editor.addPath(Shapes::rectangle({0, 0, 20, 20}), QStringLiteral("Rectangle")); });
        qputenv("FAKE_HEARD", "Delete that.");
        std::thread cancel([this] {
            QThread::msleep(700);
            dictate({QStringLiteral("cancel")});
        });
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        cancel.join();
        QCOMPARE(Island::read().activity, QStringLiteral("Cancelled."));
        bool exists = false;
        onBackend([&] { exists = m_backend->host.editor.document()->find(shape) != nullptr; });
        QVERIFY(exists);

        // Tier 2 goes to the agent as an instruction; tier 1 edits are the user's own undo steps.
        qputenv("FAKE_HEARD", "Make the logo rounder.");
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        onBackend([&] {
            QCOMPARE(m_backend->host.lastAi.flow, QStringLiteral("edit"));
            QCOMPARE(m_backend->host.lastAi.prompt, QStringLiteral("make the logo rounder"));
        });
        qputenv("FAKE_HEARD", "Stroke weight six points");
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        onBackend([&] {
            QCOMPARE(m_backend->host.editor.document()->find(shape)->stroke.width, 6.0);
            QCOMPARE(m_backend->host.editor.undoName(), QStringLiteral("Stroke"));
            QVERIFY(!m_backend->host.editor.isInteracting());
        });
        qputenv("FAKE_HEARD", "");
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        QCOMPARE(Island::read().activity, QStringLiteral("Didn't catch that."));

        // Without voxtype, Dictate says how to get it.
        qputenv("OMASTRATOR_VOXTYPE", "/nonexistent/voxtype");
        QCOMPARE(dictate({QStringLiteral("start")}, &out), 1);
        QVERIFY(out.contains(QLatin1String("omarchy voxtype install")));
        qunsetenv("OMASTRATOR_VOXTYPE");
    }

    // What the pill showed while dictating (Listening, Heard, Cancelled) is a desktop notification now.
    void whatWasHeardIsAlsoANotification()
    {
        const QString fake = m_directory.filePath(QStringLiteral("voxtype"));
        script(fake, "[ \"$1\" = -q ] || exit 3\nprintf '\\n%s\\n' \"$FAKE_HEARD\"\n");
        qputenv("OMASTRATOR_VOXTYPE", fake.toUtf8());
        const QString log = m_directory.filePath(QStringLiteral("notified.log"));
        const QString notify = m_directory.filePath(QStringLiteral("notify"));
        QFile::remove(log);
        script(notify, "printf '%s\\n' \"$*\" >> '" + log.toUtf8() + "'\n");
        qputenv("OMASTRATOR_NOTIFY", notify.toUtf8());
        const QString wav = m_directory.filePath(QStringLiteral("notified.wav"));
        QFile touched(wav);
        QVERIFY(touched.open(QIODevice::WriteOnly));
        touched.close();

        qputenv("FAKE_HEARD", "");
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        QTRY_VERIFY(QString::fromUtf8(readAll(log)).contains(QLatin1String("Didn't catch that.")));
        qputenv("FAKE_HEARD", "Use the rectangle tool");
        QCOMPARE(dictate({QStringLiteral("file"), wav}), 0);
        QTRY_VERIFY(QString::fromUtf8(readAll(log)).contains(QStringLiteral("Heard: “Use the rectangle tool” → ")));
        // Notifications come from Omastrator, for a few seconds.
        QVERIFY(QString::fromUtf8(readAll(log)).contains(QLatin1String("-a Omastrator -t ")));
        qputenv("OMASTRATOR_NOTIFY", "/bin/true");
        qunsetenv("OMASTRATOR_VOXTYPE");
    }

    void pushToTalkRecordsUntilReleased()
    {
        // pw-record stands in: it writes a recording when interrupted, as the real one finishes its file.
        const QString recorder = m_directory.filePath(QStringLiteral("pw-record"));
        script(recorder, "for last; do :; done\ntrap 'printf RIFFxxxxWAVEdata-and-more-than-44-bytes-of-sound-here-please > \"$last\"; exit 0' INT TERM\n"
                         "while true; do sleep 0.05; done\n");
        const QString fake = m_directory.filePath(QStringLiteral("voxtype"));
        script(fake, "printf '%s\\n' \"$FAKE_HEARD\"\n");
        qputenv("OMASTRATOR_PW_RECORD", recorder.toUtf8());
        qputenv("OMASTRATOR_VOXTYPE", fake.toUtf8());
        qputenv("FAKE_HEARD", "zoom out");
        QString out;
        QCOMPARE(dictate({QStringLiteral("start")}, &out), 0);
        QCOMPARE(Island::read().activity, QStringLiteral("Listening…"));
        QTest::qWait(200);
        QCOMPARE(dictate({QStringLiteral("stop")}, &out), 0);
        QCOMPARE(out, QStringLiteral("Zoom Out"));
        QVERIFY(!QFileInfo::exists(QDir(Island::runtimeDirectory()).filePath(QStringLiteral("dictation.wav"))));
        // Stopping twice, or cancelling nothing, is harmless.
        QCOMPARE(dictate({QStringLiteral("stop")}), 0);
        QCOMPARE(dictate({QStringLiteral("start")}), 0);
        QCOMPARE(dictate({QStringLiteral("cancel")}), 0);
        QCOMPARE(Island::read().activity, QStringLiteral("Cancelled."));
        qunsetenv("OMASTRATOR_PW_RECORD");
    }

    void voxtypeTranscribesTheBundledRecordings()
    {
        if (Dictation::voxtype().isEmpty())
            QSKIP("voxtype isn't installed, so the recordings can't be transcribed. Install it with: omarchy voxtype install");
        struct Recording {
            const char *file;
            const char *normalized;
            int tier;
        };
        for (const Recording &recording : {Recording{"select-the-pen-tool.wav", "select the pen tool", 1},
                                           Recording{"make-the-logo-rounder.wav", "make the logo rounder", 2}}) {
            QString error;
            const QString text = Dictation::transcribe(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Agent/fixtures/dictation/") + QLatin1String(recording.file), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(Dictation::normalize(text), QLatin1String(recording.normalized));
            QCOMPARE(Dictation::parse(Dictation::normalize(text)).tier, recording.tier);
        }
    }
};

QTEST_GUILESS_MAIN(DictationTests)
#include "DictationTests.moc"
