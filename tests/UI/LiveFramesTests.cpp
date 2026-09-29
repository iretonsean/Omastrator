#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTemporaryDir>
#include <QTest>

// Live in Browser Views, the UI side (docs/LIVE-IN-FRAME.md, sections 1 and 8): snapshots follow the session on the
// pool's thread, edits are pending per project across the window, the frames and the held ones, and reset, deleting the
// frame and quitting end every session. Headless Chromium on a throwaway profile; skips without it.
namespace {
constexpr int patience = 60'000;

// Stands in for `omarchy`: the default agent is a fake claude on PATH that exits without answering.
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "exit 2\n";

void write(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

struct Site {
    QString folder;
    StaticServer server;
};

// A page hosted in a frame on `session`, with a canvas so the frame gets its tab.
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

class LiveFramesTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;

    // A copy of the fixture in git, served from where it is written.
    std::unique_ptr<Site> site()
    {
        auto made = std::make_unique<Site>();
        made->folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site%1").arg(++m_sites);
        for (const char *name : {"index.html", "second.html"}) {
            QFile source(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/liveframe/") + QLatin1String(name));
            if (!source.open(QIODevice::ReadOnly))
                return nullptr;
            write(made->folder + QLatin1Char('/') + QLatin1String(name), source.readAll());
        }
        write(made->folder + "/style.css", "body { color: #111; }\n");
        WriteBack::git(made->folder, {"init", "-q", "-b", "main"});
        WriteBack::git(made->folder, {"add", "-A"});
        WriteBack::git(made->folder, {"commit", "-q", "-m", "First"});
        if (!made->server.serve(made->folder).isEmpty())
            return nullptr;
        return made;
    }

    QUrl page(const Site &served) const { return QUrl(served.server.url().toString() + QStringLiteral("index.html")); }

    // Shows the frame's page, then starts Live on it.
    void startLive(EditorSession &session, const QUuid &frame, const QString &folder)
    {
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(frame).isNull(), patience);
        const QString failure = frames->start(frame, folder);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(frame).state, LiveSession::State::running, patience);
        // Running is the tab's; the page may still be loading.
        bool loaded = false;
        for (int i = 0; i < 300 && !loaded; ++i) {
            QString answer;
            bool done = false;
            frames->run(frame, [&answer](LiveSession &live) {
                answer = live.evaluate(QStringLiteral("!!document.querySelector('#title') && !!window.__oma")).toBool() ? QStringLiteral("yes") : QString();
                return QString();
            }, [&done](const QString &) { done = true; });
            QTRY_VERIFY_WITH_TIMEOUT(done, 15'000);
            loaded = answer == QLatin1String("yes");
            if (!loaded)
                QTest::qWait(100);
        }
        QVERIFY(loaded);
    }

    void editOpacity(EditorSession &session, const QUuid &frame, const QString &value)
    {
        LiveFrames *frames = LiveFrames::of(session);
        const size_t before = frames->snapshot(frame).edits.size();
        QString failure = QStringLiteral("pending");
        frames->edit(frame, QStringLiteral("#title"), QStringLiteral("opacity"), value, [&](const QString &error) { failure = error; });
        QTRY_VERIFY_WITH_TIMEOUT(failure != QLatin1String("pending"), patience);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(frame).edits.size() > before, 10'000);
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
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        write(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        QDir().mkpath(m_directory.filePath(QStringLiteral("agents")));
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("agents")).toUtf8());
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "quiet");
        qputenv("OMASTRATOR_GH", "/bin/false");
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
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

    void aFramesSnapshotFollowsItsSession()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        LiveFrames *frames = LiveFrames::of(session);
        QVERIFY(!frames->active(hosted.frame));
        startLive(session, hosted.frame, served->folder);
        QVERIFY(frames->active(hosted.frame));

        LiveFrames::Snapshot snapshot = frames->snapshot(hosted.frame);
        QCOMPARE(snapshot.project, served->folder);
        QVERIFY(!snapshot.mockup);
        QVERIFY(snapshot.edits.empty());
        QVERIFY(!snapshot.canUndo);

        editOpacity(session, hosted.frame, QStringLiteral("0.5"));
        snapshot = frames->snapshot(hosted.frame);
        QCOMPARE(snapshot.edits.size(), size_t(1));
        QCOMPARE(snapshot.edits.front().property, QStringLiteral("opacity"));
        QVERIFY(snapshot.canUndo);

        bool undone = false;
        frames->undo(hosted.frame, [&](const QString &error) {
            QVERIFY(error.isEmpty());
            undone = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(undone, patience);
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).canRedo, 10'000);
        QVERIFY(!frames->snapshot(hosted.frame).canUndo);
    }

    void pendingEditsGatherEveryHostForOneProject()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        EditorSession &session = workspace.current().session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);
        editOpacity(session, hosted.frame, QStringLiteral("0.4"));

        QCOMPARE(bridge.pendingEdits(served->folder).size(), size_t(1));
        QCOMPARE(LiveFrames::pendingEdits(served->folder).size(), size_t(1));
        // Another project has none of them.
        QVERIFY(bridge.pendingEdits(served->folder + QStringLiteral("-other")).empty());
        LiveEdit kept;
        kept.selector = QStringLiteral("h1");
        kept.property = QStringLiteral("color");
        LiveFrames::hold(served->folder, {kept});
        QCOMPARE(bridge.pendingEdits(served->folder).size(), size_t(2));

        LiveFrames::clearPending(served->folder);
        QVERIFY(bridge.pendingEdits(served->folder).empty());
        QVERIFY(LiveFrames::held(served->folder).empty());
        QTRY_VERIFY_WITH_TIMEOUT(!LiveFrames::of(session)->snapshot(hosted.frame).canUndo, 10'000);
    }

    void stoppingAFrameHoldsItsEditsForDeploy()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);
        editOpacity(session, hosted.frame, QStringLiteral("0.3"));

        LiveFrames *frames = LiveFrames::of(session);
        frames->stop(hosted.frame);
        QVERIFY(!frames->active(hosted.frame));
        QCOMPARE(LiveFrames::held(served->folder).size(), size_t(1));
        QCOMPARE(LiveFrames::pendingEdits(served->folder).size(), size_t(1));
        QVERIFY(!LiveFrames::projectInUse(served->folder));
    }

    void resetAndDeletingTheFrameStopEverySession()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);
        editOpacity(session, hosted.frame, QStringLiteral("0.6"));
        QVERIFY(LiveFrames::projectInUse(served->folder));

        BrowserViews::resetAll();
        LiveFrames *frames = LiveFrames::of(session);
        QVERIFY(!frames->active(hosted.frame));
        QCOMPARE(LiveFrames::held(served->folder).size(), size_t(1));
        LiveFrames::clearPending(served->folder);

        // A frame that is deleted takes its Live with it, edits kept.
        EditorSession other;
        Hosted second(other, page(*served));
        startLive(other, second.frame, served->folder);
        editOpacity(other, second.frame, QStringLiteral("0.7"));
        other.select({second.frame});
        other.deleteSelection();
        QTRY_VERIFY_WITH_TIMEOUT(!LiveFrames::of(other)->active(second.frame), 10'000);
        QCOMPARE(LiveFrames::held(served->folder).size(), size_t(1));
    }

    void browsingToAnotherSiteHoldsTheProjectsEditsAndStartsClean()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        const auto other = site();
        QVERIFY(served && other);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);
        editOpacity(session, hosted.frame, QStringLiteral("0.6"));
        LiveFrames *frames = LiveFrames::of(session);
        QVERIFY(!frames->snapshot(hosted.frame).mockup);

        session.setBrowserLocation(hosted.frame, page(*other), {});
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).mockup, patience);
        // The first project's edit is held for its Deploy; the new page starts with none and nothing to undo.
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::held(served->folder).size(), size_t(1), 10'000);
        QVERIFY(frames->snapshot(hosted.frame).edits.empty());
        QVERIFY(!frames->snapshot(hosted.frame).canUndo);
        QVERIFY(!LiveFrames::projectInUse(served->folder));

        // Coming back doesn't bring them back onto the page, and stopping doesn't hold them twice.
        session.setBrowserLocation(hosted.frame, page(*served), {});
        QTRY_VERIFY_WITH_TIMEOUT(!frames->snapshot(hosted.frame).mockup, patience);
        QVERIFY(frames->snapshot(hosted.frame).edits.empty());
        frames->stop(hosted.frame);
        QCOMPARE(LiveFrames::held(served->folder).size(), size_t(1));
    }

    void editsClearedByAWriteBackDontComeBackFromASessionThatHasntCaughtUp()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);
        editOpacity(session, hosted.frame, QStringLiteral("0.6"));
        LiveFrames *frames = LiveFrames::of(session);

        // The session is busy when the write-back clears: its next snapshot still holds an edit made before the clear ran.
        frames->run(hosted.frame, [](LiveSession &live) {
            QThread::msleep(700);
            emit live.changed();
            return QString();
        });
        size_t most = 0;
        const QUuid frame = hosted.frame;
        connect(frames, &LiveFrames::changed, this, [&most, frames, frame] { most = std::max(most, frames->snapshot(frame).edits.size()); });
        LiveFrames::clearPending(served->folder);
        QTest::qWait(2500);
        QCOMPARE(most, size_t(0));
        QVERIFY(frames->snapshot(hosted.frame).edits.empty());
        QVERIFY(!frames->snapshot(hosted.frame).canUndo);
        QVERIFY(LiveFrames::pendingEdits(served->folder).empty());
        QObject::disconnect(frames, nullptr, this, nullptr);
    }

    void closingTheDocumentStopsItsSessions()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        {
            auto session = std::make_unique<EditorSession>();
            Hosted hosted(*session, page(*served));
            startLive(*session, hosted.frame, served->folder);
            editOpacity(*session, hosted.frame, QStringLiteral("0.8"));
            QVERIFY(LiveFrames::projectInUse(served->folder));
        }
        QVERIFY(!LiveFrames::projectInUse(served->folder));
        QCOMPARE(LiveFrames::held(served->folder).size(), size_t(1));
    }

    void deployFollowsTheSelectedFrameAndWriteBackClearsEveryHost()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.deployProject().isEmpty());
        EditorSession &session = workspace.current().session;
        Hosted hosted(session, page(*served));
        startLive(session, hosted.frame, served->folder);

        session.select({});
        QVERIFY(bridge.deployProject().isEmpty());
        session.select({hosted.frame});
        QCOMPARE(bridge.deployProject(), served->folder);

        editOpacity(session, hosted.frame, QStringLiteral("0.5"));
        QString request;
        const QString failure = bridge.liveWriteBack(&request, served->folder);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // Written to the code or handed to the agent: either way nothing is pending in any host.
        QVERIFY(bridge.pendingEdits(served->folder).empty());
        QTRY_VERIFY_WITH_TIMEOUT(!LiveFrames::of(session)->snapshot(hosted.frame).canUndo, 10'000);
        QVERIFY(LiveFrames::held(served->folder).empty());
        QVERIFY(bridge.liveWriteBack(nullptr, served->folder).contains(QLatin1String("no live edits")));
        // The fake agent exits without answering, which is how the bridge learns it is done.
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.liveMessage().isEmpty(), 30'000);
        QVERIFY(!bridge.waiting() && !bridge.run());
    }
};

QTEST_MAIN(LiveFramesTests)
#include "LiveFramesTests.moc"
