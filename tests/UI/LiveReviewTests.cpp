#include "Live/Browser.h"
#include "Live/Deploy.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/AgentSheets.h"
#include "Document/PathOperations.h"
#include "UI/ProjectWorkspaceView.h"
#include <QDirIterator>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Live in the app with Chromium (docs/OS-SUITE.md): Deploy writes the page's edits back (directly or
// through the agent's worktree), commits, pushes and deploys; Save and Hand to Agent.
namespace {
// Stands in for `omarchy`: the default agent is sh; "the agent" records its task and edits the stylesheet where it runs.
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo sh; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\"; pwd > \"$FAKE_OUT.cwd\";\n"
    "  case \"$3\" in *'Omastrator deploy'*) ;; *) printf '#title { color: var(--brand); }\\n' >> style.css;; esac; exit 0; fi\n"
    "exit 2\n";

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString git(const QString &folder, const QStringList &arguments)
{
    return WriteBack::git(folder, arguments);
}
}

class LiveReviewTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_repo;
    QString m_bare;

    bool running(AgentBridge &bridge)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 60'000 && bridge.liveSession().state() == LiveSession::State::starting)
            QTest::qWait(50);
        return bridge.liveSession().state() == LiveSession::State::running;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (Browser::executable().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("Chromium and git are needed for Live write-back.");
        QVERIFY(m_directory.isValid());
        qputenv("GIT_CONFIG_GLOBAL", m_directory.filePath(QStringLiteral("gitconfig")).toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        write(m_directory.filePath(QStringLiteral("gitconfig")), "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        // No GitHub here: gh is a stand-in that isn't logged in.
        qputenv("OMASTRATOR_GH", "/bin/false");
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("OMASTRATOR_LIVE_HEADLESS", "1");
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        const QString script = m_directory.filePath(QStringLiteral("omarchy"));
        write(script, fakeOmarchy);
        QFile(script).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_OMARCHY", script.toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("prompt")).toUtf8());

        // The plain fixture as a repository with an upstream, a local bare one.
        m_repo = m_directory.filePath(QStringLiteral("site"));
        QDirIterator files(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/plain"), QDir::Files);
        while (files.hasNext()) {
            const QString path = files.next();
            write(QDir(m_repo).filePath(QFileInfo(path).fileName()), read(path));
        }
        m_bare = m_directory.filePath(QStringLiteral("remote.git"));
        QCOMPARE(QProcess::execute(QStringLiteral("git"), {"init", "-q", "--bare", m_bare}), 0);
        git(m_repo, {"init", "-q", "-b", "main"});
        git(m_repo, {"add", "-A"});
        git(m_repo, {"commit", "-q", "-m", "Plain site"});
        git(m_repo, {"remote", "add", "origin", m_bare});
        git(m_repo, {"push", "-q", "-u", "origin", "main"});
    }

    void deployWritesBackCommitsPushesAndDeploys()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        // The site deploys with a remembered command, already confirmed once.
        QVERIFY(Deploy::remember(m_repo, QStringLiteral("echo 'Deployed: https://plain.example.test/'")).isEmpty());
        QVERIFY(Deploy::saveSettings(m_repo, {true, false}).isEmpty());
        QVERIFY(bridge.startLive({}, m_repo).isEmpty());
        QVERIFY2(running(bridge), qPrintable(bridge.liveSession().message()));
        LiveSession &live = bridge.liveSession();
        QCOMPARE(bridge.deployProject(), QFileInfo(m_repo).canonicalFilePath());

        // A unique text is written directly; a colour with no class to swap goes to the agent.
        QVERIFY(live.evaluate(QStringLiteral("window.__oma.editText('.lead', 'Edited live.')")).toBool());
        QTRY_COMPARE(live.edits().size(), size_t(1));
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e11d48")).isEmpty());
        QVERIFY(bridge.liveDeploy({}).isEmpty());
        QCOMPARE(bridge.deployState().stage, QStringLiteral("writing"));
        QCOMPARE(bridge.liveReviews().size(), size_t(1));
        QVERIFY(read(m_repo + "/index.html").contains("<p class=\"lead\">Edited live.</p>"));
        // Nothing pops up to be reviewed.
        QVERIFY(!bridge.reviewPanel().isVisible());

        // The agent ran in a worktree of its own, told what to change; the deploy waits for it.
        QCOMPARE(bridge.waiting()->task, AgentBridge::Task::live);
        const QString prompt = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt"))));
        QVERIFY(prompt.contains(QLatin1String("#title: color")) && prompt.contains(QLatin1String("--brand")));
        const QString worktree = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt.cwd")))).trimmed();
        QVERIFY(worktree.startsWith(m_directory.filePath(QStringLiteral("data"))));
        const QString id = prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Made the title the brand colour")).isEmpty());
        QVERIFY(!QFileInfo::exists(worktree));

        // Then it commits both, pushes and deploys, with no review in between.
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.deployState().running, 30'000);
        QCOMPARE(bridge.deployState().message, QStringLiteral("Live at https://plain.example.test/"));
        QCOMPARE(bridge.liveReviews().size(), size_t(2));
        const QString head = git(m_repo, {"rev-parse", "HEAD"}).trimmed();
        QCOMPARE(bridge.liveReviews().back().commit, head);
        QCOMPARE(git(m_repo, {"show", "--name-only", "--format=%s", "HEAD"}).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                 (QStringList{"Live edits from Omastrator", "index.html", "style.css"}));
        QCOMPARE(git(m_bare, {"rev-parse", "main"}).trimmed(), head);
        QVERIFY(Deploy::deployed(m_repo, head));
        QVERIFY(!bridge.reviewPanel().isVisible());

        // The user's own uncommitted edit in the same file stays theirs.
        const QByteArray mine = read(m_repo + "/index.html") + "<!-- mine -->\n";
        write(m_repo + "/index.html", mine);
        QVERIFY(live.evaluate(QStringLiteral("window.__oma.editText('#title', 'Hello, edited')")).toBool());
        QTRY_VERIFY(!live.edits().empty());
        QVERIFY(bridge.liveDeploy({}).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.deployState().running, 30'000);
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QVERIFY(git(m_repo, {"show", "HEAD:index.html"}).contains(QLatin1String("Hello, edited")));
        QVERIFY(!git(m_repo, {"show", "HEAD:index.html"}).contains(QLatin1String("mine")));
        QVERIFY(read(m_repo + "/index.html").contains("<!-- mine -->") && read(m_repo + "/index.html").contains("Hello, edited"));
        // Discard of the committed edit is a new commit; the user's line survives it.
        QVERIFY(bridge.discardReview(bridge.liveReviews().back().id).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.deployState().running, 30'000);
        QVERIFY(git(m_repo, {"log", "-1", "--format=%s"}).startsWith(QLatin1String("Discard: ")));
        QVERIFY(!read(m_repo + "/index.html").contains("Hello, edited") && read(m_repo + "/index.html").contains("<!-- mine -->"));
        write(m_repo + "/index.html", git(m_repo, {"show", "HEAD:index.html"}).toUtf8());
        live.stop();
    }

    void mockupsDontWriteBack()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        StaticServer server;
        QVERIFY(server.serve(m_repo).isEmpty());
        QVERIFY(bridge.startLive(server.url(), QString()).isEmpty());
        QVERIFY(running(bridge));
        QVERIFY(bridge.liveSession().edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e11d48")).isEmpty());
        QVERIFY(bridge.liveWriteBack().contains(QLatin1String("mock-up")));
        QVERIFY(bridge.liveAsk(QStringLiteral("rounder"), {}).contains(QLatin1String("mock-up")));
        QVERIFY(bridge.liveDeploy({}).contains(QLatin1String("mock-up")));
        bridge.liveSession().stop();
    }

    void handToAgentChangesTheAppsSourceAndSaves()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.handToAgent(m_repo, QString()).contains(QLatin1String("Open the mockup")));
        workspace.createDocument(QSizeF(120, 80));
        workspace.current().session.addPath(Shapes::rectangle({10, 10, 50, 30}), QStringLiteral("Button"));
        QVERIFY(bridge.handToAgent(m_directory.filePath(QStringLiteral("missing")), QString()).contains(QLatin1String("app's source")));
        QVERIFY(bridge.handToAgent(m_repo, QStringLiteral("The settings screen")).isEmpty());
        const QString prompt = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt"))));
        QVERIFY(prompt.contains(QLatin1String("hand-off")) && prompt.contains(QLatin1String("The settings screen")));
        const QString png = prompt.section(QLatin1String("The mockup: "), 1).section(QLatin1Char(' '), 0, 0);
        QVERIFY2(!QImage(png).isNull(), qPrintable(png));
        QVERIFY(QFileInfo::exists(png.chopped(4) + QStringLiteral(".svg")));
        const QString id = prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Restyled the button")).isEmpty());
        QCOMPARE(bridge.liveReviews().back().folder, QFileInfo(m_repo).canonicalFilePath());
        QVERIFY(bridge.liveReviews().back().diff().contains(QLatin1String("+#title { color: var(--brand); }")));
        // Save commits it in that project and pushes, Live not running.
        QVERIFY(bridge.liveSave().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.deployState().running, 30'000);
        QCOMPARE(bridge.deployState().message, QStringLiteral("Saved and pushed"));
        QCOMPARE(git(m_repo, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Restyled the button"));
        QCOMPARE(git(m_bare, {"log", "-1", "--format=%s", "main"}).trimmed(), QStringLiteral("Restyled the button"));
    }
};

QTEST_MAIN(LiveReviewTests)
#include "LiveReviewTests.moc"
