#include "Agent/AgentProtocol.h"
#include "Canvas/EditorCanvas.h"
#include "Document/PathOperations.h"
#include "Live/Deploy.h"
#include "Live/Registry.h"
#include "Live/History.h"
#include "Live/WriteBack.h"
#include "UI/AgentSheets.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/LiveHistoryPanel.h"
#include "UI/LivePanel.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>

// Deploy-first Live in the app (docs/OS-SUITE.md): Deploy writes, commits,
// pushes and deploys with no review in between; the first deploy of a project
// asks once; the diff is a record shown only when asked; Discard after a deploy
// is a new commit; History restores. Nothing here needs Chromium: the agent's
// write-backs come through Hand to Agent, and every outside program is a fake.
namespace {
// Stands in for `omarchy`: the default agent is sh (or $FAKE_AGENT). A Live task edits the stylesheet where it runs; a deploy task changes nothing.
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\"; pwd > \"$FAKE_OUT.cwd\";\n"
    "  case \"$3\" in *'Omastrator deploy'*) ;; *) printf '#title { color: var(--brand); }\\n' >> style.css;; esac; exit 0; fi\n"
    "exit 2\n";

constexpr const char *fakeGh =
    "#!/bin/sh\n"
    "echo \"$@\" >> \"$FAKE_GH_LOG\"\n"
    "if [ \"$1 $2\" = 'auth status' ]; then\n"
    "  if [ -f \"$FAKE_GH_LOGGED_IN\" ]; then echo '  ✓ Logged in to github.com account tester (keyring)'; exit 0; fi\n"
    "  echo 'You are not logged into any GitHub hosts.' >&2; exit 1\n"
    "fi\n"
    "if [ \"$1 $2\" = 'repo create' ]; then\n"
    "  git init -q --bare \"$FAKE_GH_REMOTES/$3.git\" && git remote add origin \"https://github.com/tester/$3.git\" && git push -q -u origin HEAD; exit $?\n"
    "fi\n"
    "exit 2\n";

const QByteArray secret = "s3cr3t-deploy-token-9876";
const QByteArray style = "body { color: #111; }\n";

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

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString git(const QString &folder, const QStringList &arguments)
{
    return WriteBack::git(folder, arguments);
}

// Every piece of text a panel shows.
QString shownText(QWidget *root)
{
    QString text;
    for (auto *label : root->findChildren<QLabel *>())
        text += label->text() + QLatin1Char('\n');
    for (auto *button : root->findChildren<QPushButton *>())
        text += button->text() + QLatin1Char(' ') + button->toolTip() + QLatin1Char('\n');
    for (auto *edit : root->findChildren<QPlainTextEdit *>())
        text += edit->toPlainText() + QLatin1Char('\n');
    return text;
}
}

class LiveDeployTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_repos = 0;

    // A site in git; with a remote, a local bare repository as its upstream.
    QString repository(bool withRemote, const QString &deployCommand = QString())
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site%1").arg(++m_repos);
        write(folder + "/index.html", "<h1 id=\"title\">Hello</h1>\n");
        write(folder + "/style.css", style);
        write(folder + "/.gitignore", ".env*\n");
        write(folder + "/.env", "API_TOKEN=placeholder\n");
        write(folder + "/.env.production", "API_TOKEN=" + secret + "\n");
        if (!deployCommand.isEmpty())
            Deploy::remember(folder, deployCommand);
        git(folder, {"init", "-q", "-b", "main"});
        git(folder, {"add", "-A"});
        git(folder, {"commit", "-q", "-m", "First"});
        if (withRemote) {
            QProcess::execute(QStringLiteral("git"), {"init", "-q", "--bare", folder + ".git"});
            git(folder, {"remote", "add", "origin", folder + ".git"});
            git(folder, {"push", "-q", "-u", "origin", "main"});
        }
        return folder;
    }

    // Browser View frames on `urls`, on a canvas that stays hidden so no Chromium starts. Each url's frame is returned.
    QList<QUuid> framesFor(EditorSession &session, EditorCanvas &canvas, const QList<QUrl> &urls)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        QList<QUuid> frames;
        for (const QUrl &url : urls) {
            VectorObject view = VectorObject::frame({20, 20, 300, 200}, QStringLiteral("Site"));
            view.browser = BrowserView{url, {}, {}};
            frames << view.id;
            document.insert(view, document.layers().front());
        }
        session.loadDocument(document);
        BrowserViews::of(session)->attach(&canvas);
        for (const QUuid &frame : frames)
            for (int i = 0; i < 200 && BrowserViews::of(session)->poolKey(frame).isNull(); ++i)
                QTest::qWait(10);
        return frames;
    }

    static QStringList menuTitles(BrowserViewHost *host, const QUuid &frame)
    {
        QMenu menu;
        host->extendBarMenu(frame, &menu);
        QStringList titles;
        for (const QAction *action : menu.actions())
            titles << action->text();
        return titles;
    }

    static LiveEdit headline(const QString &after)
    {
        LiveEdit edit;
        edit.selector = QStringLiteral("#title");
        edit.property = QStringLiteral("text");
        edit.before = QStringLiteral("Hello");
        edit.after = after;
        return edit;
    }

    bool finished(AgentBridge &bridge)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 30'000 && bridge.deployState().running)
            QTest::qWait(20);
        return !bridge.deployState().running;
    }

    QString prompt() { return QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt")))); }

private slots:
    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
    }

    void cleanup() { BrowserViews::shutdownPool(); }

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is needed for Live deploys.");
        QVERIFY(m_directory.isValid());
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        const QString remotes = m_directory.filePath(QStringLiteral("github"));
        QDir().mkpath(remotes);
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        // "GitHub" is a local folder: pushes to it land there, while the remote still names github.com.
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n[url \"" + remotes.toUtf8()
                             + "/\"]\n\tpushInsteadOf = https://github.com/tester/\n");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        write(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("prompt")).toUtf8());
        write(m_directory.filePath(QStringLiteral("gh")), fakeGh, true);
        qputenv("OMASTRATOR_GH", m_directory.filePath(QStringLiteral("gh")).toUtf8());
        qputenv("FAKE_GH_LOG", m_directory.filePath(QStringLiteral("gh.log")).toUtf8());
        qputenv("FAKE_GH_LOGGED_IN", m_directory.filePath(QStringLiteral("gh-logged-in")).toUtf8());
        qputenv("FAKE_GH_REMOTES", remotes.toUtf8());
        write(m_directory.filePath(QStringLiteral("terminal")), "#!/bin/sh\necho \"$@\" > \"$FAKE_TERMINAL_OUT\"\n", true);
        qputenv("OMASTRATOR_TERMINAL", m_directory.filePath(QStringLiteral("terminal")).toUtf8());
        qputenv("FAKE_TERMINAL_OUT", m_directory.filePath(QStringLiteral("terminal.out")).toUtf8());
        qputenv("ENV_DUMP", m_directory.filePath(QStringLiteral("child-env")).toUtf8());
        // Headless agents are fakes on PATH, used where a test names claude.
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    }

    void aFramesDeployButtonAppearsOnlyForYourSiteWithSomethingToSend()
    {
        const QString site = repository(true, QStringLiteral("true"));
        const QUrl mine(QStringLiteral("http://127.0.0.2:11/mine.html"));
        const QUrl theirs(QStringLiteral("http://127.0.0.2:12/theirs.html"));
        QVERIFY(ProjectRegistry::remember(mine, site).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QList<QUuid> frames = framesFor(session, canvas, {mine, theirs});
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(window.agent());

        // Nothing to send yet; the menu has the project's actions on the site that is yours, and not on the other.
        QVERIFY(views->bar(frames[0]).deploy.isEmpty());
        QVERIFY(menuTitles(views, frames[0]).contains(QStringLiteral("Deploy")));
        QVERIFY(menuTitles(views, frames[0]).contains(QStringLiteral("Review Changes")));
        QVERIFY(!menuTitles(views, frames[1]).contains(QStringLiteral("Deploy")));

        LiveFrames::hold(site, {headline(QStringLiteral("Goodbye"))});
        QCOMPARE(views->bar(frames[0]).deploy, QStringLiteral("Deploy"));
        QVERIFY(!views->bar(frames[0]).deployBusy && !views->bar(frames[0]).deployFailed);
        QVERIFY(views->bar(frames[1]).deploy.isEmpty());
        LiveFrames::clearPending(site);
        QVERIFY(views->bar(frames[0]).deploy.isEmpty());
    }

    void deployFromAFrameSendsHeldEditsAsOneCommitAndSaysWhereItIsLive()
    {
        const QString site = repository(true, QStringLiteral("echo 'Production: https://one.example.test/'"));
        Deploy::saveSettings(site, {true, false});
        const QUrl url(QStringLiteral("http://127.0.0.2:13/index.html"));
        QVERIFY(ProjectRegistry::remember(url, site).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QList<QUuid> frames = framesFor(session, canvas, {url});
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(&bridge);

        // The frame's Live had stopped and held its edit; Deploy from the frame writes it, commits and pushes it.
        LiveFrames::hold(site, {headline(QStringLiteral("Goodbye"))});
        const QString before = git(site, {"rev-parse", "HEAD"}).trimmed();
        views->act(frames[0], BrowserViewHost::Action::deployButton);
        QVERIFY(bridge.deployState().running);
        QCOMPARE(bridge.deployState().folder, site);
        QVERIFY(views->bar(frames[0]).deployBusy);
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QVERIFY(read(site + "/index.html").contains("Goodbye"));
        const QString after = git(site, {"rev-parse", "HEAD"}).trimmed();
        QVERIFY(after != before);
        QCOMPARE(git(site, {"rev-list", "--count", before + ".." + after}).trimmed(), QStringLiteral("1"));
        QCOMPARE(git(site + ".git", {"rev-parse", "main"}).trimmed(), after);
        QVERIFY(LiveFrames::pendingEdits(site).empty());

        // The bar says where it is live, for a few seconds, and there is nothing more to send.
        QCOMPARE(views->bar(frames[0]).deploy, QStringLiteral("Live at one.example.test"));
        QVERIFY(!views->bar(frames[0]).deployBusy && !views->bar(frames[0]).deployFailed);
    }

    void theIslandsDeployWritesBackOnlyWhatTheWindowEditedNotAFramesHeldEdits()
    {
        const QString site = repository(true, QStringLiteral("true"));
        Deploy::saveSettings(site, {true, false});
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        bridge.useProject(site);
        LiveFrames::hold(site, {headline(QStringLiteral("Goodbye"))});

        // No folder is the island's call: the Live window has no edits here, so nothing is written, as before frames had Live.
        AgentBridge::DeployRequest request;
        request.deploy = false;
        QVERIFY(bridge.liveDeploy(request).isEmpty());
        QVERIFY(finished(bridge));
        QVERIFY(!read(site + "/index.html").contains("Goodbye"));
        QCOMPARE(LiveFrames::held(site).size(), size_t(1));

        // The frame's own Save names its folder, and writes everything pending for it.
        QVERIFY(bridge.liveSave(site).isEmpty());
        QVERIFY(finished(bridge));
        QVERIFY(read(site + "/index.html").contains("Goodbye"));
        QVERIFY(LiveFrames::held(site).empty());
    }

    void saveFromAFrameCommitsAndPushesWithoutDeploying()
    {
        const QString site = repository(true, QStringLiteral("echo deployed > \"$ENV_DUMP\""));
        QFile::remove(qEnvironmentVariable("ENV_DUMP"));
        Deploy::saveSettings(site, {true, false});
        const QUrl url(QStringLiteral("http://127.0.0.2:14/index.html"));
        QVERIFY(ProjectRegistry::remember(url, site).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QList<QUuid> frames = framesFor(session, canvas, {url});
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(&bridge);

        LiveFrames::hold(site, {headline(QStringLiteral("Saved"))});
        views->act(frames[0], BrowserViewHost::Action::save);
        QVERIFY(finished(bridge));
        QVERIFY(git(site, {"show", "HEAD:index.html"}).contains("Saved"));
        QCOMPARE(git(site + ".git", {"rev-parse", "main"}).trimmed(), git(site, {"rev-parse", "HEAD"}).trimmed());
        QVERIFY(!QFileInfo::exists(qEnvironmentVariable("ENV_DUMP")));
    }

    void aFailedDeployShowsOnTheFrameAndReviewFollowsTheSelectedFrame()
    {
        const QString first = repository(true, QStringLiteral("echo nope; exit 4"));
        const QString second = repository(true, QStringLiteral("true"));
        Deploy::saveSettings(first, {true, false});
        const QUrl one(QStringLiteral("http://127.0.0.2:15/one.html"));
        const QUrl two(QStringLiteral("http://127.0.0.2:16/two.html"));
        QVERIFY(ProjectRegistry::remember(one, first).isEmpty());
        QVERIFY(ProjectRegistry::remember(two, second).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QList<QUuid> frames = framesFor(session, canvas, {one, two});
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(&bridge);

        views->act(frames[0], BrowserViewHost::Action::deploy);
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("failed"));
        QCOMPARE(views->bar(frames[0]).deploy, QStringLiteral("Deploy failed"));
        QVERIFY(views->bar(frames[0]).deployFailed);
        // Another project's frame doesn't wear it.
        QVERIFY(!views->bar(frames[1]).deployFailed);

        // Review Changes and History are about the frame that was asked.
        views->act(frames[1], BrowserViewHost::Action::reviewChanges);
        QCOMPARE(bridge.deployProject(), second);
        QVERIFY(session.isSelected(frames[1]));
        QVERIFY(bridge.reviewPanel().isVisible());
        views->act(frames[0], BrowserViewHost::Action::history);
        QCOMPARE(bridge.deployProject(), first);
        QVERIFY(bridge.historyPanel().isVisible());
    }

    void deployOnASiteThatIsntYoursSaysSoAndRunsNothing()
    {
        const QUrl url(QStringLiteral("http://127.0.0.2:17/theirs.html"));
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QList<QUuid> frames = framesFor(session, canvas, {url});
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(&bridge);
        QSignalSpy notices(views, &BrowserViews::notice);
        views->act(frames[0], BrowserViewHost::Action::deploy);
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains(QLatin1String("isn't one of your sites")));
        QVERIFY(!bridge.deployState().running);
        QCOMPARE(bridge.deployState().stage, QStringLiteral("idle"));
    }

    void nothingToDeployWithoutAProject()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.liveDeploy({}).contains(QLatin1String("Open a project in Live")));
        QVERIFY(bridge.deployProject().isEmpty());
    }

    void theFirstDeployAsksOnceThenDeploysWithTheProjectsEnvironment()
    {
        const QString site = repository(true, QStringLiteral("echo \"keys: $(env | cut -d= -f1 | grep -x API_TOKEN)\"; printf '%s' \"$API_TOKEN\" > \"$ENV_DUMP\"; "
                                                             "echo \"token=$API_TOKEN\"; echo 'Production: https://one.example.test/'"));
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());

        // The first time, it asks and nothing runs.
        bool needsAnswer = false;
        QVERIFY(bridge.liveDeploy({true, false, false, std::nullopt, site}, &needsAnswer).isEmpty());
        QVERIFY(needsAnswer);
        QCOMPARE(bridge.deployState().stage, QStringLiteral("idle"));
        QDialog *sheet = AgentSheets::deploy(bridge, &window, site);
        auto *question = sheet->findChild<QLabel *>(QStringLiteral("deployQuestion"));
        QVERIFY(question && question->text().contains(QLatin1String("Deploy to production with")));
        auto *environment = sheet->findChild<QLabel *>(QStringLiteral("deployEnvironment"));
        QVERIFY(environment && environment->text().contains(QLatin1String("API_TOKEN")) && environment->text().contains(QLatin1String(".env.production")));
        QVERIFY(!shownText(sheet).contains(QString::fromUtf8(secret)));
        QVERIFY(!sheet->findChild<QCheckBox *>(QStringLiteral("deployCreateRepository")));
        sheet->findChild<QCheckBox *>(QStringLiteral("deployDontAsk"))->setChecked(true);
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QVERIFY(bridge.deployState().running);
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QCOMPARE(bridge.deployState().message, QStringLiteral("Live at https://one.example.test/"));
        QVERIFY(Deploy::settings(site).confirmed);
        // Nothing opened on its own.
        QVERIFY(!bridge.reviewPanel().isVisible());

        // The deploy had the value; nothing Omastrator shows or keeps does.
        QCOMPARE(read(qEnvironmentVariable("ENV_DUMP")), secret);
        const QByteArray log = read(bridge.deployState().log);
        QVERIFY(log.contains("keys: API_TOKEN") && log.contains("token=[API_TOKEN]") && !log.contains(secret));
        const QJsonObject status = bridge.tools().status();
        QCOMPARE(status["live"].toObject()["deploy"].toObject()["message"].toString(), QStringLiteral("Live at https://one.example.test/"));
        QVERIFY(!QJsonDocument(status).toJson().contains(secret));

        // After that, Deploy just deploys.
        QVERIFY(bridge.liveDeploy({}, &needsAnswer).isEmpty());
        QVERIFY(!needsAnswer);
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QCOMPARE(Deploy::records(site).size(), size_t(2));

        // Details shows the log; the panel shows no value.
        bridge.showLivePanel();
        QVERIFY(bridge.reviewPanel().isVisible());
        auto *panel = window.findChild<LivePanel *>();
        QVERIFY(panel && !shownText(panel).contains(QString::fromUtf8(secret)));
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("liveDetails")));
        window.findChild<QPushButton *>(QStringLiteral("liveDetails"))->click();
        QPlainTextEdit *details = nullptr;
        QTRY_VERIFY((details = window.findChild<QPlainTextEdit *>(QStringLiteral("deployLogText"))));
        QVERIFY(details->toPlainText().contains(QLatin1String("[API_TOKEN]")) && !details->toPlainText().contains(QString::fromUtf8(secret)));
        details->window()->close();

        // A failure is the plain line first, then at most one dry one.
        Deploy::remember(site, QStringLiteral("echo \"no route for $API_TOKEN\"; exit 4"));
        QVERIFY(bridge.liveDeploy({}).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("failed"));
        QVERIFY2(bridge.deployState().message.startsWith(QLatin1String("Deploy failed: no route for [API_TOKEN]")), qPrintable(bridge.deployState().message));
        QVERIFY(!bridge.deployState().message.contains(QString::fromUtf8(secret)));
        QVERIFY(bridge.tools().status()["live"].toObject()["deploy"].toObject()["failed"].toBool());
        QVERIFY(bridge.showDeployLog().isEmpty());
    }

    void writeBacksAreARecordShownOnlyWhenAsked()
    {
        const QString site = repository(true, QStringLiteral("echo https://two.example.test"));
        Deploy::saveSettings(site, {true, false});
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        workspace.createDocument(QSizeF(120, 80));
        workspace.current().session.addPath(Shapes::rectangle({10, 10, 50, 30}), QStringLiteral("Button"));

        // The agent's change is written straight in, and nothing opens.
        QVERIFY(bridge.handToAgent(site, QStringLiteral("The title")).isEmpty());
        const QString id = prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Made the title the brand colour")).isEmpty());
        QVERIFY(read(site + "/style.css").contains("#title { color: var(--brand); }"));
        QCOMPARE(bridge.liveReviews().size(), size_t(1));
        QVERIFY(!bridge.reviewPanel().isVisible());
        QCOMPARE(bridge.unsavedFiles(), 1);

        // Deploy doesn't stop for a review.
        QVERIFY(bridge.liveDeploy({}).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().message, QStringLiteral("Live at https://two.example.test"));
        const QString deployed = git(site, {"rev-parse", "HEAD"}).trimmed();
        QCOMPARE(git(site, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Made the title the brand colour"));
        QCOMPARE(git(site + ".git", {"rev-parse", "main"}).trimmed(), deployed);
        QCOMPARE(bridge.liveReviews().front().commit, deployed);
        QCOMPARE(bridge.unsavedFiles(), 0);
        QVERIFY(!bridge.reviewPanel().isVisible());

        // The panel opens on Deploy; the diff waits behind Review changes.
        bridge.showLivePanel();
        QVERIFY(bridge.reviewPanel().isVisible());
        QVERIFY(!window.findChild<QPlainTextEdit *>(QStringLiteral("liveReviewDiff")));
        auto *reviewChanges = window.findChild<QPushButton *>(QStringLiteral("liveReviewChanges"));
        QVERIFY(reviewChanges && reviewChanges->text().contains(QLatin1String("Review Changes")));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("liveDeploy"))->isDefault());
        reviewChanges->click();
        QPlainTextEdit *diff = nullptr;
        QTRY_VERIFY((diff = window.findChild<QPlainTextEdit *>(QStringLiteral("liveReviewDiff"))));
        QVERIFY(diff->toPlainText().contains(QLatin1String("+#title { color: var(--brand); }")));

        // Discard after deploy: a new commit that reverts those files, pushed, with Deploy offered.
        QVERIFY(bridge.discardReview(bridge.liveReviews().front().id).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QVERIFY(bridge.deployState().message.contains(QLatin1String("Deploy to take it off")));
        QCOMPARE(read(site + "/style.css"), style);
        QCOMPARE(git(site, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Discard: Made the title the brand colour"));
        QCOMPARE(git(site + ".git", {"log", "-1", "--format=%s", "main"}).trimmed(), QStringLiteral("Discard: Made the title the brand colour"));
        QVERIFY(git(site, {"status", "--porcelain", "--untracked-files=no"}).trimmed().isEmpty());

        // History: each commit, and which one was deployed.
        const std::vector<History::Entry> entries = bridge.history();
        QCOMPARE(entries.size(), size_t(3));
        QVERIFY(!entries[0].deploy);
        QCOMPARE(entries[1].sha, deployed);
        QCOMPARE(entries[1].deploy->url, QStringLiteral("https://two.example.test"));
        QCOMPARE(entries[1].files, QStringList{"style.css"});
        QJsonObject result;
        QVERIFY(bridge.live(QStringLiteral("history"), {{"list", true}}, result).isEmpty());
        QCOMPARE(result["commits"].toArray().size(), 3);
        QVERIFY(result["commits"].toArray()[1].toObject()["deployed"].toBool());
        QVERIFY(!bridge.historyPanel().isVisible());

        // Restore brings that version back as a new commit, then offers Deploy.
        bridge.showHistoryPanel();
        QVERIFY(bridge.historyPanel().isVisible());
        auto *history = window.findChild<LiveHistoryPanel *>();
        QCOMPARE(history->findChildren<QWidget *>(QStringLiteral("historyEntry")).size(), 3);
        QList<QPushButton *> restores = history->findChildren<QPushButton *>(QStringLiteral("historyRestore"));
        QCOMPARE(restores.size(), 3);
        // The newest can't be restored: it's what's there.
        QVERIFY(!restores[0]->isEnabled() && restores[1]->isEnabled());
        restores[1]->click();
        QVERIFY(finished(bridge));
        QCOMPARE(git(site, {"log", "-1", "--format=%s"}).trimmed(), QStringLiteral("Restore %1: Made the title the brand colour").arg(deployed.left(7)));
        QVERIFY(read(site + "/style.css").contains("#title { color: var(--brand); }"));
        QCOMPARE(git(site + ".git", {"rev-parse", "main"}).trimmed(), git(site, {"rev-parse", "HEAD"}).trimmed());
        QPushButton *deployNext = nullptr;
        QTRY_VERIFY((deployNext = history->findChild<QPushButton *>(QStringLiteral("historyDeploy"))));
        deployNext->click();
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QVERIFY(Deploy::deployed(site, git(site, {"rev-parse", "HEAD"}).trimmed()));
    }

    void uncommittedWorkOfTheUsersStaysTheirs()
    {
        const QString site = repository(true, QStringLiteral("true"));
        Deploy::saveSettings(site, {true, false});
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        workspace.createDocument(QSizeF(120, 80));
        workspace.current().session.addPath(Shapes::rectangle({10, 10, 50, 30}), QStringLiteral("Button"));
        // The user's own edit at the top of the stylesheet, not committed.
        const QByteArray mine = "/* mine */\n" + style;
        write(site + "/style.css", mine);
        QVERIFY(bridge.handToAgent(site, QString()).isEmpty());
        const QString id = prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(bridge.liveAgentDone(id, QStringLiteral("Brand title")).isEmpty());
        QVERIFY(read(site + "/style.css").startsWith(mine));
        QVERIFY(bridge.liveDeploy({}).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        // The commit has the agent's line, not theirs; theirs is still on disk, uncommitted.
        const QString committed = git(site, {"show", "HEAD:style.css"});
        QVERIFY(committed.contains("#title") && !committed.contains("mine"));
        QVERIFY(read(site + "/style.css").startsWith(mine) && read(site + "/style.css").contains("#title"));
        QCOMPARE(WriteBack::dirtyFiles(site), QStringList{"style.css"});
    }

    void deployWithAgentReportsBackAndCanBeRemembered()
    {
        write(qEnvironmentVariable("FAKE_GH_LOGGED_IN"), "");
        const QString site = repository(false);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());

        // No command and no GitHub remote: the sheet offers both, once.
        const AgentBridge::DeployQuestion question = bridge.deployQuestion(site);
        QVERIFY(question.command.viaAgent() && question.confirm);
        QCOMPARE(question.github, QFileInfo(site).fileName());
        QDialog *sheet = AgentSheets::deploy(bridge, &window, site);
        QVERIFY(sheet->findChild<QLabel *>(QStringLiteral("deployQuestion"))->text().contains(QLatin1String("Deploy to production with")));
        QVERIFY(sheet->findChild<QCheckBox *>(QStringLiteral("deployCreateRepository"))->isChecked());
        QCOMPARE(sheet->findChild<QLineEdit *>(QStringLiteral("deployRepositoryName"))->text(), QFileInfo(site).fileName());
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();

        // The repository is made on GitHub and pushed, then the agent deploys from the checkout.
        QTRY_COMPARE_WITH_TIMEOUT(bridge.deployState().stage, QStringLiteral("deploying"), 30'000);
        QTRY_VERIFY(bridge.waiting() && bridge.waiting()->task == AgentBridge::Task::deploy);
        QVERIFY(read(qEnvironmentVariable("FAKE_GH_LOG")).contains("repo create " + QFileInfo(site).fileName().toUtf8() + " --private --source . --push"));
        QCOMPARE(History::githubPage(site), QStringLiteral("https://github.com/tester/%1").arg(QFileInfo(site).fileName()));
        QCOMPARE(QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt.cwd")))).trimmed(), site);
        const QString task = prompt();
        QVERIFY(task.contains(QLatin1String("live_deployed")) && task.contains(QLatin1String("API_TOKEN")) && task.contains(QLatin1String(".env.production")));
        QVERIFY(task.contains(git(site, {"rev-parse", "HEAD"}).trimmed().left(12)));
        QVERIFY(!task.contains(QString::fromUtf8(secret)) && !task.contains(QLatin1String("placeholder")));
        const QString id = task.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);

        // A command with a secret in it isn't offered to keep.
        try {
            bridge.tools().call(QStringLiteral("live_deployed"), {{"requestId", "wrong"}, {"url", "https://x.test"}});
            QFAIL("a wrong id was accepted");
        } catch (const AgentProtocol::Error &) {
        }
        bridge.tools().call(QStringLiteral("live_deployed"), {{"requestId", id}, {"url", "https://three.example.test"},
                                                              {"command", QStringLiteral("TOKEN=%1 ship").arg(QString::fromUtf8(secret))}});
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().message, QStringLiteral("Live at https://three.example.test"));
        QVERIFY(bridge.deployState().suggested.isEmpty());
        QVERIFY(!bridge.waiting());
        QVERIFY(bridge.history().front().link.startsWith(QLatin1String("https://github.com/tester/")));
        QVERIFY(bridge.history().front().deploy.has_value());

        // Asked again (Don't ask again wasn't chosen), and GitHub isn't offered again.
        bool needsAnswer = false;
        QVERIFY(bridge.liveDeploy({}, &needsAnswer).isEmpty() && needsAnswer);
        QVERIFY(bridge.deployQuestion().github.isEmpty());
        QVERIFY(bridge.liveDeploy({true, true, false, std::nullopt, QString()}).isEmpty());
        QTRY_VERIFY(bridge.waiting() && bridge.waiting()->task == AgentBridge::Task::deploy);
        const QString second = prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        bridge.tools().call(QStringLiteral("live_deployed"), {{"requestId", second}, {"url", "https://three.example.test"}, {"command", "npx ship --prod"}});
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().suggested, QStringLiteral("npx ship --prod"));
        bridge.showLivePanel();
        QPushButton *remember = nullptr;
        QTRY_VERIFY((remember = window.findChild<QPushButton *>(QStringLiteral("liveRemember"))));
        remember->click();
        QCOMPARE(Deploy::resolve(site).command, QStringLiteral("npx ship --prod"));
        QCOMPARE(Deploy::resolve(site).source, QStringLiteral("omastrator.json"));

        // An agent that can't deploy says why, plainly.
        Deploy::remember(site, QString());
        QVERIFY(bridge.liveDeploy({}).contains(QLatin1String("Confirm")));
        QVERIFY(bridge.liveDeploy({true, true, false, std::nullopt, QString()}).isEmpty());
        QTRY_VERIFY2(bridge.waiting() && bridge.waiting()->task == AgentBridge::Task::deploy, qPrintable(bridge.deployState().message));
        bridge.tools().call(QStringLiteral("live_deployed"), {{"error", "The host needs a login first."}});
        QVERIFY(finished(bridge));
        QVERIFY(bridge.deployState().message.startsWith(QLatin1String("Deploy failed: The host needs a login first.")));
    }

    void gitHubConnectsThroughGhInATerminal()
    {
        QFile::remove(qEnvironmentVariable("FAKE_GH_LOGGED_IN"));
        const QString site = repository(true, QStringLiteral("true"));
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(!bridge.githubAuth(true).loggedIn);
        // Logged out, a project without a remote isn't asked about GitHub; the panel offers Connect GitHub.
        QVERIFY(bridge.deployQuestion(repository(false)).github.isEmpty());
        QVERIFY(bridge.liveDeploy({false, false, false, std::nullopt, site}).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().message, QStringLiteral("Saved and pushed"));
        bridge.showLivePanel();
        auto *connectButton = window.findChild<QPushButton *>(QStringLiteral("liveConnectGitHub"));
        QVERIFY(connectButton);
        connectButton->click();
        QTRY_COMPARE(read(qEnvironmentVariable("FAKE_TERMINAL_OUT")).trimmed(), (qEnvironmentVariable("OMASTRATOR_GH") + " auth login").toUtf8());
    }

    void aDeployAgentThatStopsWithoutReportingFails()
    {
        // Claude runs headless, recording into agents/; the default is sh again afterwards.
        const QString out = m_directory.filePath(QStringLiteral("agents"));
        QDir().mkpath(out);
        const QByteArray fakeOut = qgetenv("FAKE_OUT");
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "quiet");
        qputenv("FAKE_OUT", out.toUtf8());
        const auto restore = qScopeGuard([fakeOut] {
            qunsetenv("FAKE_AGENT");
            qputenv("FAKE_OUT", fakeOut);
        });
        QFile::remove(qEnvironmentVariable("FAKE_GH_LOGGED_IN"));
        const QString site = repository(true);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        QVERIFY(bridge.deployQuestion(site).command.viaAgent());

        // It pushes, then the agent deploys from the checkout and exits without live_deployed: Deploy failed, with its log.
        QVERIFY(bridge.liveDeploy({true, true, false, std::nullopt, site}).isEmpty());
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().stage, QStringLiteral("failed"));
        QVERIFY2(bridge.deployState().message.startsWith(QLatin1String("Deploy failed: Claude stopped without an answer.")),
                 qPrintable(bridge.deployState().message));
        QCOMPARE(FakeAgents::read(out + QStringLiteral("/claude.cwd")).trimmed(), site);
        QVERIFY(!bridge.waiting() && !bridge.run());
        QVERIFY(bridge.deployState().log.startsWith(AgentLauncher::logFolder()));
        QVERIFY(read(bridge.deployState().log).contains("I looked at it and decided not to."));
        bridge.showLivePanel();
        QPushButton *details = nullptr;
        QTRY_VERIFY((details = window.findChild<QPushButton *>(QStringLiteral("liveDetails"))));
        details->click();
        QPlainTextEdit *log = nullptr;
        QTRY_VERIFY((log = window.findChild<QPlainTextEdit *>(QStringLiteral("deployLogText"))));
        QVERIFY(log->toPlainText().contains(QLatin1String("I looked at it")));
        log->window()->close();
        // The record says it failed.
        QVERIFY(!Deploy::records(site).empty() && !Deploy::records(site).back().ok);

        // Cancel while the agent deploys stops it.
        qputenv("FAKE_MODE", "hang");
        QFile::remove(out + QStringLiteral("/claude.child"));
        QVERIFY(bridge.liveDeploy({true, true, false, std::nullopt, site}).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!FakeAgents::read(out + QStringLiteral("/claude.child")).trimmed().isEmpty(), 15'000);
        const pid_t child = pid_t(FakeAgents::read(out + QStringLiteral("/claude.child")).trimmed().toInt());
        QVERIFY(child > 0 && ::kill(child, 0) == 0);
        bridge.cancelDeploy();
        QVERIFY(finished(bridge));
        QCOMPARE(bridge.deployState().message, QStringLiteral("Cancelled."));
        QVERIFY(!bridge.waiting());
        QTRY_VERIFY(::kill(child, 0) != 0);
    }
};

QTEST_MAIN(LiveDeployTests)
#include "LiveDeployTests.moc"
