#include "Live/Browser.h"
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

// Phase 6 of docs/OS-SUITE.md, in the app: write-back, the agent's worktree, review, Save and Publish.
namespace {
// Stands in for `omarchy`: the default agent is sh; "the agent" records its task and edits the stylesheet where it runs.
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo sh; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\"; pwd > \"$FAKE_OUT.cwd\";\n"
    "  printf '#title { color: var(--brand); }\\n' >> style.css; exit 0; fi\n"
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

    void writeBackReviewSaveAndPublish()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        QVERIFY(bridge.startLive({}, m_repo).isEmpty());
        QVERIFY2(running(bridge), qPrintable(bridge.liveSession().message()));
        LiveSession &live = bridge.liveSession();

        // A unique text is written directly; a colour with no class to swap goes to the agent.
        QVERIFY(live.evaluate(QStringLiteral("window.__oma.editText('.lead', 'Edited live.')")).toBool());
        QTRY_COMPARE(live.edits().size(), size_t(1));
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e11d48")).isEmpty());
        QVERIFY(bridge.liveWriteBack(false).isEmpty());
        QCOMPARE(bridge.liveReviews().size(), size_t(1));
        QVERIFY(read(m_repo + "/index.html").contains("<p class=\"lead\">Edited live.</p>"));
        QVERIFY(bridge.reviewPanel().isVisible());
        auto *diff = window.findChild<QPlainTextEdit *>(QStringLiteral("liveReviewDiff"));
        QVERIFY(diff && diff->toPlainText().contains(QLatin1String("+    <p class=\"lead\">Edited live.</p>")));

        // The agent ran in a worktree of its own, told what to change.
        QCOMPARE(bridge.waiting()->task, AgentBridge::Task::live);
        const QString prompt = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt"))));
        QVERIFY(prompt.contains(QLatin1String("#title: color")) && prompt.contains(QLatin1String("--brand")));
        const QString worktree = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt.cwd")))).trimmed();
        QVERIFY(worktree.startsWith(m_directory.filePath(QStringLiteral("data"))));
        QVERIFY(!read(m_repo + "/style.css").contains("#title"));
        const QString id = prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Made the title the brand colour"), false).isEmpty());
        QCOMPARE(bridge.liveReviews().size(), size_t(2));
        QVERIFY(read(m_repo + "/style.css").contains("#title { color: var(--brand); }"));
        QVERIFY(!QFileInfo::exists(worktree));
        QVERIFY(!bridge.waiting());

        // Discard the agent's, keep the text: the stylesheet is exactly as it was.
        const QByteArray style = read(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/plain/style.css"));
        QVERIFY(bridge.discardReview(bridge.liveReviews().back().id).isEmpty());
        QCOMPARE(read(m_repo + "/style.css"), style);
        QVERIFY(bridge.keepReview(QString()).isEmpty());
        QCOMPARE(bridge.unsavedFiles(), 1);
        QVERIFY(bridge.tools().status()["live"].toObject()["unsaved"].toInt() == 1);

        // Publish says what it will do and waits; Save commits; then it pushes.
        QString output;
        QVERIFY(bridge.livePublish(QStringLiteral("git"), false, &output).contains(QLatin1String("git push origin HEAD:main")));
        QVERIFY(bridge.livePublish(QStringLiteral("git"), true, &output).contains(QLatin1String("Save")));
        QVERIFY(bridge.liveSave().isEmpty());
        QCOMPARE(git(m_repo, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Change the text “Edit me live.” to “Edited live.”"));
        QVERIFY(git(m_repo, {"status", "--porcelain"}).trimmed().isEmpty());
        QVERIFY(bridge.livePublish(QStringLiteral("git"), true, &output).isEmpty());
        QCOMPARE(git(m_bare, {"log", "-1", "--format=%s", "main"}).trimmed(), git(m_repo, {"log", "-1", "--format=%s"}).trimmed());
        QVERIFY(bridge.livePublish(QStringLiteral("vercel"), true, &output).contains(QLatin1String("no publish option")));

        // Uncommitted changes in the file are asked about first, and Discard returns them too.
        const QByteArray mine = read(m_repo + "/index.html") + "<!-- mine -->\n";
        write(m_repo + "/index.html", mine);
        QVERIFY(live.evaluate(QStringLiteral("window.__oma.editText('#title', 'Hello, edited')")).toBool());
        QTRY_VERIFY(!live.edits().empty());
        QVERIFY(bridge.liveWriteBack(false).contains(QLatin1String("haven't committed")));
        QVERIFY(bridge.canConfirm());
        QCOMPARE(read(m_repo + "/index.html"), mine);
        QVERIFY(bridge.confirmPending().isEmpty());
        QVERIFY(read(m_repo + "/index.html").contains("Hello, edited"));
        QVERIFY(bridge.discardReview(QString()).isEmpty());
        QCOMPARE(read(m_repo + "/index.html"), mine);

        live.stop();
        // The Publish sheet lists only what the repo has.
        QDialog *sheet = AgentSheets::publish(bridge, &window);
        QVERIFY(sheet->findChild<QWidget *>(QStringLiteral("publish-git")));
        QVERIFY(!sheet->findChild<QWidget *>(QStringLiteral("publish-vercel")));
        sheet->close();
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
        QVERIFY(bridge.liveWriteBack(false).contains(QLatin1String("mock-up")));
        QVERIFY(bridge.liveAsk(QStringLiteral("rounder"), {}, false).contains(QLatin1String("mock-up")));
        bridge.liveSession().stop();
    }

    void handToAgentChangesTheAppsSourceForReview()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.handToAgent(m_repo, QString(), false).contains(QLatin1String("Open the mockup")));
        workspace.createDocument(QSizeF(120, 80));
        workspace.current().session.addPath(Shapes::rectangle({10, 10, 50, 30}), QStringLiteral("Button"));
        QVERIFY(bridge.handToAgent(m_directory.filePath(QStringLiteral("missing")), QString(), false).contains(QLatin1String("app's source")));
        // Clean the checkout from the test before.
        git(m_repo, {"checkout", "--", "."});
        QVERIFY(bridge.handToAgent(m_repo, QStringLiteral("The settings screen"), false).isEmpty());
        const QString prompt = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt"))));
        QVERIFY(prompt.contains(QLatin1String("hand-off")) && prompt.contains(QLatin1String("The settings screen")));
        const QString png = prompt.section(QLatin1String("The mockup: "), 1).section(QLatin1Char(' '), 0, 0);
        QVERIFY2(!QImage(png).isNull(), qPrintable(png));
        QVERIFY(QFileInfo::exists(png.chopped(4) + QStringLiteral(".svg")));
        const QString id = prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Restyled the button"), false).isEmpty());
        QCOMPARE(bridge.liveReviews().back().folder, QFileInfo(m_repo).canonicalFilePath());
        QVERIFY(bridge.liveReviews().back().diff().contains(QLatin1String("+#title { color: var(--brand); }")));
        QVERIFY(bridge.keepReview(QString()).isEmpty());
        QVERIFY(bridge.liveSave().isEmpty());
        QCOMPARE(git(m_repo, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Restyled the button"));
        // Live isn't running, so Publish acts on the project just saved.
        QCOMPARE(bridge.publishOptions().size(), size_t(1));
    }
};

QTEST_MAIN(LiveReviewTests)
#include "LiveReviewTests.moc"
