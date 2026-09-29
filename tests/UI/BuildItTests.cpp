#include "Canvas/EditorCanvas.h"
#include "Live/Registry.h"
#include "Live/WriteBack.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Build It from a Browser View (docs/LIVE-IN-FRAME.md, section 5). No Chromium: the canvas stays hidden, so the package
// is made from the frame's children and its stored picture, and the agent is the same fake as Hand to Agent's.
namespace {
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\";\n"
    "  printf '#title { color: var(--brand); }\\n' >> style.css; exit 0; fi\n"
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

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class BuildItTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;
    int m_ports = 0;

    QString site()
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site%1").arg(++m_sites);
        write(folder + "/index.html", "<h1 id=\"title\">Hello</h1>\n");
        write(folder + "/style.css", "body { color: #111; }\n");
        WriteBack::git(folder, {"init", "-q", "-b", "main"});
        WriteBack::git(folder, {"add", "-A"});
        WriteBack::git(folder, {"commit", "-q", "-m", "First"});
        return folder;
    }

    QUrl unique() { return QUrl(QStringLiteral("http://127.0.0.2:%1/page.html").arg(20'000 + ++m_ports)); }

    // One Browser View on `url`, 300 wide; with `designed`, a shape drawn on it.
    QUuid frameFor(EditorSession &session, EditorCanvas &canvas, const QUrl &url, bool designed)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 300, 200}, QStringLiteral("Pricing"));
        view.browser = BrowserView{url, {0, 40}, {}};
        const QUuid frame = view.id;
        document.insert(view, document.layers().front());
        if (designed)
            document.insert(VectorObject::frame({50, 80, 100, 40}, QStringLiteral("Button")), frame);
        session.loadDocument(document);
        BrowserViews::of(session)->attach(&canvas);
        for (int i = 0; i < 200 && BrowserViews::of(session)->poolKey(frame).isNull(); ++i)
            QTest::qWait(10);
        return frame;
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

    QString prompt()
    {
        QString text;
        for (int i = 0; i < 300 && text.isEmpty(); ++i) {
            text = QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt"))));
            if (text.isEmpty())
                QTest::qWait(10);
        }
        return text;
    }

    static QString requestOf(const QString &prompt) { return prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is needed for Build It.");
        QVERIFY(m_directory.isValid());
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        write(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("prompt")).toUtf8());
    }

    void cleanup()
    {
        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        BrowserViews::setNoteChooser({});
    }

    void theButtonAndItsMenuItemsAppearOnlyForAFrameWithADesign()
    {
        const QString folder = site();
        const QUrl url = unique();
        QVERIFY(ProjectRegistry::remember(url, folder).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid bare = frameFor(session, canvas, url, false);
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(window.agent());
        QVERIFY(views->bar(bare).build.isEmpty());

        const QUuid designed = frameFor(session, canvas, url, true);
        QCOMPARE(views->bar(designed).build, QStringLiteral("Build It"));
        QVERIFY(!views->bar(designed).buildBusy && !views->bar(designed).buildDone);
        const QStringList titles = menuTitles(views, designed);
        QVERIFY(titles.contains(QStringLiteral("Build It")));
        QVERIFY(titles.contains(QStringLiteral("Build It with a Note…")));
        QVERIFY(titles.contains(QStringLiteral("Stop Build")));
    }

    void buildItHandsOverTheDesignAndTheReviewWaits()
    {
        const QString folder = site();
        const QUrl url = unique();
        QVERIFY(ProjectRegistry::remember(url, folder).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas, url, true);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        LiveEdit edit;
        edit.selector = QStringLiteral("#title");
        edit.property = QStringLiteral("padding");
        edit.before = QStringLiteral("0px");
        edit.after = QStringLiteral("24px");
        LiveFrames::hold(folder, {edit});
        const bool undoable = session.canUndo();

        views->act(frame, BrowserViewHost::Action::buildIt);
        const QString text = prompt();
        QVERIFY2(!text.isEmpty(), "the agent was never launched");
        QVERIFY(text.contains(QLatin1String("Designed at 300 px wide")));
        QVERIFY(text.contains(QLatin1String("mockup.png")));
        QVERIFY(text.contains(QLatin1String("selectors.json")));
        QVERIFY(text.contains(QLatin1String("24px")));
        QVERIFY(LiveFrames::pendingEdits(folder).empty());
        QCOMPARE(bridge.buildingFrame(), frame);
        QVERIFY(views->bar(frame).buildBusy);
        QVERIFY(views->bar(frame).build.startsWith(QLatin1String("Building with")));

        // A second build waits for the first.
        QSignalSpy notices(views, &BrowserViews::notice);
        views->act(frame, BrowserViewHost::Action::buildIt);
        QVERIFY(!notices.isEmpty());
        QVERIFY(notices.last().at(0).toString().contains(QLatin1String("still working")));

        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("Padded the title")).isEmpty());
        QVERIFY(bridge.buildingFrame().isNull());
        QVERIFY(views->bar(frame).buildDone);
        QCOMPARE(views->bar(frame).build, QStringLiteral("Built. Review changes"));
        QVERIFY(read(folder + "/style.css").contains("#title { color: var(--brand); }"));
        QCOMPARE(bridge.liveReviews().size(), size_t(1));
        QVERIFY(bridge.liveReviews().front().title.contains(QLatin1String("Build it: Pricing")));
        // The design stays as it was and no undo step was made.
        QCOMPARE(session.document()->children(frame).size(), size_t(1));
        QCOMPARE(session.canUndo(), undoable);

        // A click on the pill opens the review, and it is spent.
        views->act(frame, BrowserViewHost::Action::buildButton);
        QCOMPARE(views->bar(frame).build, QStringLiteral("Build It"));
        QVERIFY(bridge.reviewPanel().isVisible() || window.isHidden());
    }

    void stopBuildEndsTheAgent()
    {
        const QString folder = site();
        const QUrl url = unique();
        QVERIFY(ProjectRegistry::remember(url, folder).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas, url, true);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());

        views->act(frame, BrowserViewHost::Action::buildIt);
        QVERIFY(!prompt().isEmpty());
        QVERIFY(bridge.waiting());
        views->act(frame, BrowserViewHost::Action::stopBuild);
        QVERIFY(!bridge.waiting());
        QVERIFY(bridge.buildingFrame().isNull());
        QCOMPARE(views->bar(frame).build, QStringLiteral("Build It"));
        QVERIFY(bridge.liveReviews().empty());
    }

    void aNoteReachesTheAgentAndCancelingBuildsNothing()
    {
        const QString folder = site();
        const QUrl url = unique();
        QVERIFY(ProjectRegistry::remember(url, folder).isEmpty());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas, url, true);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());

        BrowserViews::setNoteChooser([] { return QString(); });
        views->act(frame, BrowserViewHost::Action::buildItWithNote);
        QVERIFY(!bridge.waiting());

        BrowserViews::setNoteChooser([] { return QStringLiteral("Keep the hero image as it is"); });
        views->act(frame, BrowserViewHost::Action::buildItWithNote);
        const QString text = prompt();
        QVERIFY(text.contains(QLatin1String("Keep the hero image as it is")));
        views->act(frame, BrowserViewHost::Action::stopBuild);
    }

    void aSiteThatIsntYoursAsksForTheFolderInTheHandToAgentSheet()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas, unique(), true);
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(window.agent());
        QVERIFY(menuTitles(views, frame).contains(QStringLiteral("Build It…")));

        views->act(frame, BrowserViewHost::Action::buildIt);
        QDialog *sheet = nullptr;
        for (const auto *each : QApplication::topLevelWidgets())
            if (each->objectName() == QLatin1String("handoffSheet"))
                sheet = const_cast<QDialog *>(qobject_cast<const QDialog *>(each));
        QVERIFY2(sheet, "the Hand to Agent sheet did not open");
        QVERIFY(!window.agent()->waiting());
        sheet->deleteLater();
    }
};

QTEST_MAIN(BuildItTests)
#include "BuildItTests.moc"
