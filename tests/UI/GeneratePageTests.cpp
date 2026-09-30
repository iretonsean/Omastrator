#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Live/Browser.h"
#include "Live/DevServers.h"
#include "Live/PageTemplates.h"
#include "Live/Registry.h"
#include "Live/WriteBack.h"
#include "System/PagePlan.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/GeneratePageSheet.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SyncConfirmDialog.h"
#include "../Agent/FakeAgents.h"
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>
#include <memory>

// Generate a page in an empty Browser View (docs/MOTION.md, section 4). No Chromium: the canvas stays hidden, the agent is
// the same fake as Build It's, the confirmation is answered by the test, and the dev server is Omastrator's own static
// server (or a fake npm that serves the folder, for the stacks that run npm).
namespace {
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_PROMPT\";\n"
    "  if [ \"$FAKE_WRITE\" = 1 ]; then printf '<!doctype html>\\n<title>Roaster</title>\\n<h1>Coffee that tastes like morning.</h1>\\n' > index.html; fi\n"
    "  exit 0; fi\n"
    "exit 2\n";

// `npm install` makes node_modules; `npm run dev` serves the folder the way a dev server does, and both are logged.
constexpr const char *fakeNpm =
    "#!/bin/sh\n"
    "echo \"$@\" >> \"$FAKE_NPM_LOG\"\n"
    "if [ \"$1\" = install ]; then mkdir -p node_modules; exit 0; fi\n"
    "if [ \"$1\" = run ] && [ \"$2\" = dev ]; then exec python3 -u -m http.server 0 --bind 127.0.0.1; fi\n"
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

QString git(const QString &folder, const QStringList &arguments)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.start(QStringLiteral("git"), arguments);
    process.waitForFinished(20'000);
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

int lines(const QByteArray &bytes)
{
    return int(QString::fromUtf8(bytes).split(QLatin1Char('\n')).size()) - (bytes.endsWith('\n') ? 1 : 0);
}

// What the canvas asks of the host, with an empty frame to offer.
class FakeHost : public BrowserViewHost {
public:
    QImage picture(const QUuid &) const override { return {}; }
    QString message(const QUuid &) const override { return QStringLiteral("No page yet."); }
    void act(const QUuid &frame, Action action) override { acts.emplace_back(frame, action); }
    Empty empty(const QUuid &) const override { return offer; }

    Empty offer;
    std::vector<std::pair<QUuid, Action>> acts;
};
}

class GeneratePageTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_projects = 0;

    QString folder() { return m_directory.filePath(QStringLiteral("projects/page%1").arg(++m_projects)); }
    QString prompt() { return m_directory.filePath(QStringLiteral("prompt")); }
    QString stagingRoot() { return PageTemplates::stagingRoot(); }
    bool stagingEmpty() { return !QDir(stagingRoot()).exists() || QDir(stagingRoot()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot); }

    // One empty Browser View, 300 wide; with `designed`, a shape drawn on it.
    QUuid frameFor(EditorSession &session, EditorCanvas &canvas, bool designed = false)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 300, 200}, QStringLiteral("Browser View 1"));
        view.browser = BrowserView{QUrl(), {0, 0}, {}};
        const QUuid frame = view.id;
        document.insert(view, document.layers().front());
        if (designed)
            document.insert(VectorObject::frame({50, 80, 100, 40}, QStringLiteral("Button")), frame);
        session.loadDocument(document);
        BrowserViews::of(session)->attach(&canvas);
        // The frame's entry is made by the next reconcile.
        for (int i = 0; i < 200 && BrowserViews::of(session)->poolKey(frame).isNull(); ++i)
            QTest::qWait(10);
        return frame;
    }

    static QString requestOf(const QString &text) { return text.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0); }

    QString promptText()
    {
        QString text;
        for (int i = 0; i < 400 && text.isEmpty(); ++i) {
            text = QString::fromUtf8(read(prompt()));
            if (text.isEmpty())
                QTest::qWait(10);
        }
        return text;
    }

    // What the sheet answers with.
    void answer(const QString &description, PageTemplates::Stack stack, const QString &where)
    {
        GeneratePageSheet::setResponder([=](bool describe) -> std::optional<GeneratePageSheet::Answer> {
            return GeneratePageSheet::Answer{describe ? description : QString(), stack, where};
        });
    }

    // Answers the confirmation, keeping what it said.
    struct Confirm {
        bool answer = true;
        QString said;
        int asked = 0;
    };
    void confirm(Confirm &state)
    {
        SyncConfirmDialog::setResponder([&state](SyncConfirmDialog &dialog) {
            ++state.asked;
            state.said = dialog.text();
            return state.answer;
        });
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is needed for Generate a page.");
        QVERIFY(m_directory.isValid());
        write(m_directory.filePath(QStringLiteral("gitconfig")), "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", m_directory.filePath(QStringLiteral("gitconfig")).toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_CACHE_HOME", m_directory.filePath(QStringLiteral("cache")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        write(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("FAKE_PROMPT", prompt().toUtf8());
        // The fake npm comes first on PATH; the real one, if there is one, is never run.
        write(m_directory.filePath(QStringLiteral("bin/npm")), fakeNpm, true);
        qputenv("FAKE_NPM_LOG", m_directory.filePath(QStringLiteral("npm.log")).toUtf8());
        qputenv("PATH", (m_directory.filePath(QStringLiteral("bin")) + QLatin1Char(':') + QString::fromLocal8Bit(qgetenv("PATH"))).toUtf8());
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        QFile::remove(ProjectRegistry::path());
        QFile::remove(prompt());
        QFile::remove(m_directory.filePath(QStringLiteral("npm.log")));
        qunsetenv("FAKE_WRITE");
        qunsetenv("FAKE_AGENT");
        qunsetenv("FAKE_MODE");
    }

    void cleanup()
    {
        GeneratePageSheet::setResponder({});
        SyncConfirmDialog::setResponder({});
        BrowserViews::shutdownPool();
        QDir(stagingRoot()).removeRecursively();
    }

    // The plan for each stack, with no agent ----------------------------------------------------------------------

    void eachTemplateMakesAPlanWithItsFiles_data()
    {
        QTest::addColumn<int>("stack");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<QStringList>("runs");
        QTest::newRow("vite") << int(PageTemplates::Stack::viteTailwind)
                              << QStringList{"index.html", "src/style.css", "package.json", "vite.config.js", ".gitignore"}
                              << QStringList{"npm install", "npm run dev"};
        QTest::newRow("html") << int(PageTemplates::Stack::plainHtml) << QStringList{"index.html", "style.css"}
                              << QStringList{"Omastrator's static server, on the folder"};
        QTest::newRow("astro") << int(PageTemplates::Stack::astro)
                               << QStringList{"src/pages/index.astro", "src/styles/global.css", "package.json", "astro.config.mjs", ".gitignore"}
                               << QStringList{"npm install", "npm run dev"};
    }

    void eachTemplateMakesAPlanWithItsFiles()
    {
        QFETCH(int, stack);
        QFETCH(QStringList, expected);
        QFETCH(QStringList, runs);
        const PageTemplates::Stack chosen = PageTemplates::Stack(stack);
        const QString where = folder();
        const std::vector<PageTemplates::File> files = PageTemplates::files(chosen, QStringLiteral("Demo"));
        QStringList paths;
        for (const PageTemplates::File &file : files)
            paths << file.path;
        QCOMPARE(paths, expected);

        const SyncPlan plan = PagePlan::make(where, chosen, files, QStringLiteral("Browser View 1"));
        QVERIFY2(plan.problem.isEmpty(), qPrintable(plan.problem));
        QCOMPARE(plan.writes.size(), files.size());
        QVERIFY(plan.git && plan.git->create && plan.git->commit);
        QCOMPARE(plan.git->branch, QStringLiteral("main"));
        QCOMPARE(plan.git->message, QStringLiteral("First page from Omastrator"));
        QCOMPARE(plan.runsAfter, runs);
        QVERIFY(plan.command.isEmpty() && plan.commands.empty());
        for (size_t i = 0; i < files.size(); ++i) {
            QCOMPARE(plan.writes[i].path, where + QLatin1Char('/') + files[i].path);
            QCOMPARE(plan.writes[i].summary(), QStringLiteral("new, %1 lines").arg(lines(files[i].bytes)));
        }

        // The dialog says each file and its line count, the repository, what runs and what happens in the app.
        SyncConfirmDialog dialog(plan);
        const QString said = dialog.text();
        for (size_t i = 0; i < files.size(); ++i)
            QVERIFY2(said.contains(plan.writes[i].path + QStringLiteral(" (new, %1 lines)").arg(lines(files[i].bytes))), qPrintable(said));
        QVERIFY(said.contains(QStringLiteral("Creates: %1").arg(where)));
        QVERIFY(said.contains(QStringLiteral("A new git repository, branch main: “First page from Omastrator”. Nothing is pushed.")));
        QVERIFY(said.contains(runs.join(QLatin1Char('\n'))));
        QVERIFY(said.contains(QStringLiteral("Browser View 1 shows the page from its dev server. One undo step.")));
        QCOMPARE(dialog.confirmButton()->text(), QStringLiteral("Create Project"));
        QVERIFY(!dialog.confirmButton()->isDefault() && !dialog.confirmButton()->autoDefault());
    }

    void aFolderThatHasFilesInItIsRefused()
    {
        const QString where = folder();
        write(where + "/notes.txt", "mine\n");
        const SyncPlan plan = PagePlan::make(where, PageTemplates::Stack::plainHtml, PageTemplates::files(PageTemplates::Stack::plainHtml, QStringLiteral("Demo")),
                                             QStringLiteral("Browser View 1"));
        QCOMPARE(plan.problem, QStringLiteral("This folder isn't empty. Generate a page writes a new project."));
        SyncConfirmDialog dialog(plan);
        QVERIFY(dialog.confirmButton() == nullptr);
        QVERIFY(dialog.text().contains(QStringLiteral("This folder isn't empty")));
        // An empty folder that exists, and one that doesn't, are both fine.
        const QString empty = folder();
        QDir().mkpath(empty);
        QVERIFY(PageTemplates::folderProblem(empty).isEmpty());
        QVERIFY(PageTemplates::folderProblem(folder()).isEmpty());
        QVERIFY(!PageTemplates::folderProblem(where + "/notes.txt").isEmpty());
    }

    void theFolderNameComesFromTheWordsThatNameThePage()
    {
        QCOMPARE(PageTemplates::slug(QStringLiteral("A landing page for a small coffee roaster")), QStringLiteral("coffee-roaster"));
        QCOMPARE(PageTemplates::slug(QStringLiteral("Portfolio for Ana Ruiz, photographer")), QStringLiteral("portfolio-ana-ruiz"));
        QCOMPARE(PageTemplates::slug(QStringLiteral("A page")), QString());
        for (const PageTemplates::Stack stack : PageTemplates::stacks())
            QCOMPARE(PageTemplates::fromId(PageTemplates::id(stack)), std::optional(stack));
    }

    // The empty frame's offers -------------------------------------------------------------------------------------

    void anEmptyFrameOffersThreeWaysToFillIt()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid bare = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        views->setAgent(window.agent());

        BrowserViewHost::Empty offer = views->empty(bare);
        QVERIFY(offer.offered && !offer.build && !offer.generating);
        QCOMPARE(offer.buildLine, QStringLiteral("Design it with vectors and text first; Claude turns the frame into code."));
        QCOMPARE(views->message(bare), QStringLiteral("No page yet."));

        const QUuid designed = frameFor(session, canvas, true);
        offer = views->empty(designed);
        QVERIFY(offer.offered && offer.build && offer.buildLine.isEmpty());

        // A frame with an address has nothing to offer.
        session.setBrowserUrl(designed, QUrl(QStringLiteral("http://127.0.0.2:20111/")));
        QVERIFY(!views->empty(designed).offered);
    }

    void theCanvasDrawsTheOffersAndRoutesTheirClicks()
    {
        EditorSession session;
        session.loadDocument(VectorDocument::blank({4000, 3000}));
        EditorCanvas canvas(session);
        FakeHost host;
        canvas.resize(1000, 800);
        canvas.show();
        session.zoomToRect(QRectF(50, 100, 900, 700));
        canvas.setBrowserViewHost(&host);
        const QUuid frame = session.addBrowserView({100, 200, 600, 400}, QUrl());
        session.deselectAll();
        host.offer.offered = true;
        host.offer.buildLine = QStringLiteral("Design it with vectors and text first.");

        // For a look at it: OMASTRATOR_TEST_GRAB=out.png.
        if (const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB"); !grab.isEmpty())
            canvas.grab().save(QString::fromLocal8Bit(grab));

        // The buttons sit in a row about the frame's middle, as the canvas lays them out.
        const QRectF box = canvas.documentToView().mapRect(QRectF(100, 200, 600, 400));
        QFont font = canvas.font();
        font.setPixelSize(12);
        const QFontMetricsF metrics(font);
        const double first = metrics.horizontalAdvance(QStringLiteral("Type an address")) + 28;
        const double second = metrics.horizontalAdvance(QStringLiteral("Generate a page…")) + 28;
        const double row = first + 8 + second;
        const double block = 22 + 12 + 30 + 10 + 16;
        const double top = box.center().y() - block / 2;
        const double x = box.center().x() - row / 2;
        const double y = top + 34 + 15;

        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(int(x + first + 8 + second / 2), int(y)));
        QCOMPARE(host.acts.size(), size_t(1));
        QCOMPARE(host.acts.back().first, frame);
        QCOMPARE(int(host.acts.back().second), int(BrowserViewHost::Action::generatePage));

        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(int(x + first / 2), int(y)));
        QVERIFY(canvas.isEditingAddress());
        QCOMPARE(host.acts.size(), size_t(1));

        // With a design on the frame the third button is Build It's.
        QTest::keyClick(canvas.findChild<QLineEdit *>(QStringLiteral("browserAddressEdit")), Qt::Key_Escape);
        QVERIFY(!canvas.isEditingAddress());
        host.offer.build = true;
        host.offer.buildLine.clear();
        const double third = metrics.horizontalAdvance(QStringLiteral("Build It from a canvas frame")) + 28;
        const double wide = first + 8 + second + 8 + third;
        const double topBuild = box.center().y() - (22 + 12 + 30) / 2.0;
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                          QPoint(int(box.center().x() - wide / 2 + first + 8 + second + 8 + third / 2), int(topBuild + 34 + 15)));
        QCOMPARE(host.acts.size(), size_t(2));
        QCOMPARE(int(host.acts.back().second), int(BrowserViewHost::Action::buildIt));

        // While a page is being written, Esc stops it.
        host.offer = {};
        host.offer.generating = true;
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QCOMPARE(host.acts.size(), size_t(3));
        QCOMPARE(int(host.acts.back().second), int(BrowserViewHost::Action::stopBuild));
    }

    // The flow ------------------------------------------------------------------------------------------------------

    void theAgentWritesIntoAStagingFolderAndNothingIsWrittenBeforeConfirm()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A landing page for a small coffee roaster"), PageTemplates::Stack::plainHtml, where);
        qputenv("FAKE_WRITE", "1");

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY2(!text.isEmpty(), "the agent was never launched");
        QVERIFY(text.contains(QLatin1String("A landing page for a small coffee roaster")));
        QVERIFY(text.contains(QLatin1String("Plain HTML and CSS")));
        QVERIFY(text.contains(QLatin1String("- index.html")));
        QVERIFY(text.contains(QLatin1String("- style.css")));
        QVERIFY(text.contains(QLatin1String("Don't run npm")));
        QVERIFY(text.contains(QLatin1String("agentDone")));
        // The agent works in the staging folder, which holds the template; the project doesn't exist.
        QVERIFY(text.contains(stagingRoot()));
        QVERIFY(!QFileInfo::exists(where));
        QVERIFY(bridge.writingPage());
        QVERIFY(views->empty(frame).generating && !views->empty(frame).offered);
        QVERIFY(views->bar(frame).buildBusy);
        QVERIFY(views->bar(frame).build.startsWith(QLatin1String("Writing with")));
        QVERIFY(views->message(frame).contains(QLatin1String("Writing the page from your description")));
        QCOMPARE(bridge.activity().size(), size_t(3));
        QCOMPARE(bridge.activity().front().text, QStringLiteral("Writing the page from your description"));
        QVERIFY(bridge.activity().front().state == AgentBridge::ActivityLine::State::running);

        // The Live panel opened with the steps, one line each, and a Stop.
        QVERIFY(bridge.reviewPanel().isVisible() || window.isHidden());
        const QList<QLabel *> shown = window.findChildren<QLabel *>(QStringLiteral("liveActivityLine"));
        QCOMPARE(shown.size(), 3);
        QCOMPARE(shown.front()->text(), QStringLiteral("…  Writing the page from your description"));
        QCOMPARE(shown.back()->text(), QStringLiteral("·  Start the dev server"));
        QPushButton *stop = window.findChild<QPushButton *>(QStringLiteral("liveActivityStop"));
        QVERIFY(stop);

        // A second run waits for the first.
        QSignalSpy notices(views, &BrowserViews::notice);
        views->act(frame, BrowserViewHost::Action::generatePage);
        QVERIFY(!notices.isEmpty());
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("A page is already being written for this frame."));
        // The panel's Stop is the same as the pill's.
        stop->click();
        QVERIFY(!bridge.writingPage());
        QTRY_VERIFY(stagingEmpty());
        QTRY_VERIFY(window.findChildren<QLabel *>(QStringLiteral("liveActivityLine")).isEmpty());
    }

    void cancelWritesNothingAndRemovesTheStagingFolder()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A landing page for a small coffee roaster"), PageTemplates::Stack::plainHtml, where);
        qputenv("FAKE_WRITE", "1");
        Confirm state;
        state.answer = false;
        confirm(state);
        const bool undoable = session.canUndo();
        QSignalSpy notices(views, &BrowserViews::notice);

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(!text.isEmpty());
        QVERIFY(!stagingEmpty());
        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("A landing page")).isEmpty());
        QTRY_COMPARE(state.asked, 1);
        // The plan lists the agent's own page, and the frame is still empty.
        QVERIFY(state.said.contains(where + QStringLiteral("/index.html (new, 3 lines)")));
        QVERIFY(state.said.contains(where + QStringLiteral("/style.css (new,")));
        QTRY_VERIFY(stagingEmpty());
        QTRY_VERIFY(!notices.isEmpty());
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Nothing was written."));
        QVERIFY(!QFileInfo::exists(where));
        QVERIFY(session.document()->find(frame)->browser->url.isEmpty());
        QCOMPARE(session.canUndo(), undoable);
        QVERIFY(bridge.activity().empty());
        QVERIFY(views->empty(frame).offered);
        QVERIFY(!DevServers::shared().running(where));
    }

    void confirmCreatesTheProjectItsRepositoryAndTheFrameShowsItsPage()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A landing page for a small coffee roaster"), PageTemplates::Stack::plainHtml, where);
        qputenv("FAKE_WRITE", "1");
        Confirm state;
        confirm(state);
        QSignalSpy notices(views, &BrowserViews::notice);

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(!text.isEmpty());
        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("A landing page for Northlight")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state.asked, 1, 10'000);
        QVERIFY(state.said.contains(QStringLiteral("Creates: %1").arg(where)));

        // The folder, one commit on main, and no remote.
        QTRY_VERIFY_WITH_TIMEOUT(!session.document()->find(frame)->browser->url.isEmpty(), 20'000);
        QVERIFY(QFileInfo(where + "/index.html").isFile() && QFileInfo(where + "/style.css").isFile());
        QVERIFY(read(where + "/index.html").contains("Coffee that tastes like morning."));
        QCOMPARE(git(where, {"rev-list", "--count", "HEAD"}), QStringLiteral("1"));
        QCOMPARE(git(where, {"symbolic-ref", "--short", "HEAD"}), QStringLiteral("main"));
        QCOMPARE(git(where, {"log", "-1", "--format=%s"}), QStringLiteral("First page from Omastrator"));
        QVERIFY(git(where, {"remote"}).isEmpty());
        QVERIFY(git(where, {"status", "--porcelain"}).isEmpty());
        QVERIFY(stagingEmpty());

        // The frame shows the dev server's page, as one "Change URL" step, and the page is its folder's.
        const QUrl url = session.document()->find(frame)->browser->url;
        QVERIFY2(DevServer::answers(url, 3000), qPrintable(url.toString()));
        QCOMPARE(session.undoName(), QStringLiteral("Change URL"));
        QVERIFY(views->bar(frame).dev && !views->bar(frame).notYours);
        QCOMPARE(views->bar(frame).devTip, url.toString());
        QCOMPARE(QFileInfo(views->generatedProject(frame)).canonicalFilePath(), QFileInfo(where).canonicalFilePath());
        QVERIFY(!ProjectRegistry::owns(url));
        QCOMPARE(DevServers::shared().holders(where), 1);
        QVERIFY(!notices.isEmpty() && notices.last().at(0).toString().startsWith(QLatin1String("Page ready.")));
        // The steps are all done.
        for (const AgentBridge::ActivityLine &line : bridge.activity())
            QVERIFY2(line.state == AgentBridge::ActivityLine::State::done, qPrintable(line.text));
        QCOMPARE(bridge.activity().back().text, QStringLiteral("Starting the dev server on %1:%2").arg(url.host(), QString::number(url.port())));

        // Undo puts the frame back to empty and the project stays where it is.
        session.undo();
        QVERIFY(session.document()->find(frame)->browser->url.isEmpty());
        QVERIFY(QFileInfo(where + "/index.html").isFile());
    }

    void theStacksThatRunNpmInstallFirstAndThenTheDevScript()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty())
            QSKIP("python3 isn't installed, and the fake npm's dev server is Python's.");
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A portfolio for a photographer"), PageTemplates::Stack::viteTailwind, where);
        qputenv("FAKE_WRITE", "1");
        Confirm state;
        confirm(state);

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(text.contains(QLatin1String("Vite with Tailwind CSS v4")));
        QVERIFY(text.contains(QLatin1String("- vite.config.js")));
        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("A portfolio")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state.asked, 1, 10'000);
        QVERIFY(state.said.contains(QStringLiteral("npm install\nnpm run dev")));
        QTRY_VERIFY_WITH_TIMEOUT(!session.document()->find(frame)->browser->url.isEmpty(), 30'000);
        QCOMPARE(QString::fromUtf8(read(m_directory.filePath(QStringLiteral("npm.log")))).trimmed(), QStringLiteral("install\nrun dev"));
        QVERIFY(QFileInfo(where + "/package.json").isFile() && QFileInfo(where + "/vite.config.js").isFile());
        // node_modules is npm's and never committed.
        QVERIFY(git(where, {"ls-files"}).contains(QStringLiteral("package.json")));
        QVERIFY(!git(where, {"ls-files"}).contains(QStringLiteral("node_modules")));
        QCOMPARE(git(where, {"rev-list", "--count", "HEAD"}), QStringLiteral("1"));
    }

    void theDesignSystemsTokensGoIntoTheTemplatesTokenFile()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        VectorDocument document = *session.document();
        document.tokens.push_back(DesignToken::color(QStringLiteral("color/brand"), QColor(0xc8, 0x55, 0x3d)));
        session.loadDocument(document);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        answer(QStringLiteral("A landing page"), PageTemplates::Stack::viteTailwind, folder());

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(text.contains(QLatin1String("design system tokens are already in src/style.css")));
        // The staged file has the token, in `@theme`, before the agent starts.
        QDir root(stagingRoot());
        const QStringList staged = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(staged.size(), 1);
        const QString css = QString::fromUtf8(read(root.filePath(staged.front() + "/src/style.css")));
        QVERIFY2(css.contains(QLatin1String("--color-brand")) && css.contains(QLatin1String("@theme")), qPrintable(css));
        QCOMPARE(bridge.activity().front().text, QStringLiteral("Using your design system tokens for colour and type"));
        QVERIFY(bridge.activity().front().state == AgentBridge::ActivityLine::State::done);
        bridge.stopWaiting();
        QTRY_VERIFY(stagingEmpty());
    }

    void aNonEmptyFolderIsRefusedBeforeTheAgentStarts()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        write(where + "/index.html", "<h1>Mine</h1>\n");
        answer(QStringLiteral("A landing page"), PageTemplates::Stack::plainHtml, where);
        QSignalSpy notices(views, &BrowserViews::notice);

        views->act(frame, BrowserViewHost::Action::generatePage);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.front().at(0).toString(), QStringLiteral("This folder isn't empty. Generate a page writes a new project."));
        QVERIFY(!QFileInfo::exists(prompt()));
        QVERIFY(!bridge.writingPage() && bridge.activity().empty());
        QCOMPARE(read(where + "/index.html"), QByteArray("<h1>Mine</h1>\n"));
        QVERIFY(stagingEmpty());
        QVERIFY(views->empty(frame).offered);

        // No description is refused too, and a frame with an address takes no page.
        answer(QString(), PageTemplates::Stack::plainHtml, folder());
        views->act(frame, BrowserViewHost::Action::generatePage);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Describe the page first."));
        QVERIFY(!QFileInfo::exists(prompt()));
    }

    void anAgentThatChangedNothingWritesNoPlan()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        answer(QStringLiteral("A landing page"), PageTemplates::Stack::plainHtml, folder());
        Confirm state;
        confirm(state);
        QSignalSpy notices(views, &BrowserViews::notice);

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("I can't write pages")).isEmpty());
        QTRY_VERIFY(!notices.isEmpty());
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("The agent didn't write a page: I can't write pages"));
        QCOMPARE(state.asked, 0);
        QVERIFY(stagingEmpty());
        QVERIFY(views->empty(frame).offered);
    }

    void stopDuringWritingLeavesNothing()
    {
        const QString fakes = FakeAgents::install(m_directory.filePath(QStringLiteral("agents")));
        QVERIFY(!fakes.isEmpty());
        const QString out = m_directory.filePath(QStringLiteral("agents/out"));
        QDir().mkpath(out);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", (fakes + QLatin1Char(':') + QString::fromLocal8Bit(path)).toUtf8());
        qputenv("FAKE_OUT", out.toUtf8());
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "hang");
        const auto restore = qScopeGuard([&] {
            qputenv("PATH", path);
            qunsetenv("FAKE_OUT");
        });
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A landing page for a small coffee roaster"), PageTemplates::Stack::plainHtml, where);
        QSignalSpy notices(views, &BrowserViews::notice);

        views->act(frame, BrowserViewHost::Action::generatePage);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out + "/claude.argv"), 10'000);
        QVERIFY(bridge.writingPage());
        QVERIFY(!stagingEmpty());
        QCOMPARE(FakeAgents::read(out + "/claude.cwd").trimmed(), QFileInfo(QDir(stagingRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot).front()).canonicalFilePath());

        // The frame's pill stops it, as Build It's does.
        QCOMPARE(views->bar(frame).build, QStringLiteral("Writing with Claude…"));
        views->act(frame, BrowserViewHost::Action::buildButton);
        QVERIFY(!bridge.writingPage());
        QTRY_VERIFY_WITH_TIMEOUT(stagingEmpty(), 10'000);
        QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Stopped. Nothing was written."));
        QVERIFY(!QFileInfo::exists(where));
        QVERIFY(session.document()->find(frame)->browser->url.isEmpty());
        QVERIFY(bridge.activity().empty());
        QVERIFY(!bridge.waiting());
        QVERIFY(views->empty(frame).offered);
        const int child = FakeAgents::read(out + "/claude.child").trimmed().toInt();
        if (child > 0)
            QTRY_VERIFY_WITH_TIMEOUT(::kill(child, 0) != 0, 10'000);
    }

    void escAndResetStopTheWritingToo()
    {
        const QString fakes = FakeAgents::install(m_directory.filePath(QStringLiteral("agents")));
        const QString out = m_directory.filePath(QStringLiteral("agents/out"));
        QDir().mkpath(out);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", (fakes + QLatin1Char(':') + QString::fromLocal8Bit(path)).toUtf8());
        qputenv("FAKE_OUT", out.toUtf8());
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "hang");
        const auto restore = qScopeGuard([&] {
            qputenv("PATH", path);
            qunsetenv("FAKE_OUT");
        });
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        answer(QStringLiteral("A landing page"), PageTemplates::Stack::plainHtml, folder());

        views->act(frame, BrowserViewHost::Action::generatePage);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.writingPage(), 10'000);
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!bridge.writingPage());
        QTRY_VERIFY_WITH_TIMEOUT(stagingEmpty(), 10'000);

        // Reset stops a run the same way.
        views->act(frame, BrowserViewHost::Action::generatePage);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.writingPage(), 10'000);
        BrowserViews::resetAll();
        QVERIFY(!bridge.writingPage());
        QTRY_VERIFY_WITH_TIMEOUT(stagingEmpty(), 10'000);
        QVERIFY(session.document()->find(frame)->browser->url.isEmpty());
    }

    void closingTheDocumentWhileTheAgentWritesLeavesNothing()
    {
        const QString fakes = FakeAgents::install(m_directory.filePath(QStringLiteral("agents")));
        const QString out = m_directory.filePath(QStringLiteral("agents/out"));
        QDir().mkpath(out);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", (fakes + QLatin1Char(':') + QString::fromLocal8Bit(path)).toUtf8());
        qputenv("FAKE_OUT", out.toUtf8());
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "hang");
        const auto restore = qScopeGuard([&] {
            qputenv("PATH", path);
            qunsetenv("FAKE_OUT");
        });
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startServer().isEmpty());
        // A session of its own, so it can end while the window and its agent carry on.
        auto session = std::make_unique<EditorSession>();
        auto canvas = std::make_unique<EditorCanvas>(*session);
        const QUuid frame = frameFor(*session, *canvas);
        BrowserViews *views = BrowserViews::of(*session);
        views->setAgent(&bridge);
        const QString where = folder();
        answer(QStringLiteral("A landing page"), PageTemplates::Stack::plainHtml, where);

        views->act(frame, BrowserViewHost::Action::generatePage);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.writingPage(), 10'000);
        QVERIFY(!stagingEmpty());
        canvas.reset();
        session.reset();
        QVERIFY(!bridge.writingPage());
        QTRY_VERIFY_WITH_TIMEOUT(stagingEmpty(), 10'000);
        QVERIFY(!QFileInfo::exists(where));
        QVERIFY(!bridge.waiting());
    }

    // Into Edit Page ------------------------------------------------------------------------------------------------

    void aGeneratedPageGoesIntoEditPageAsItsFoldersOwn()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        canvas.resize(1000, 800);
        canvas.show();
        const QUuid frame = frameFor(session, canvas);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QStringLiteral("A landing page for a small coffee roaster"), PageTemplates::Stack::plainHtml, where);
        qputenv("FAKE_WRITE", "1");
        Confirm state;
        confirm(state);

        views->act(frame, BrowserViewHost::Action::generatePage);
        const QString text = promptText();
        QVERIFY(bridge.liveAgentDone(requestOf(text), QStringLiteral("A landing page")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!session.document()->find(frame)->browser->url.isEmpty(), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(views->state(frame) == BrowserViews::State::live, 60'000);
        QVERIFY(!views->bar(frame).notYours);

        // Edit Page starts Live with the project's folder: not a site that isn't the user's.
        QVERIFY2(views->beginEditPage(frame).isEmpty(), "Edit Page must start");
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(frame).state, LiveSession::State::running, 60'000);
        QCOMPARE(QFileInfo(frames->snapshot(frame).project).canonicalFilePath(), QFileInfo(where).canonicalFilePath());
        QVERIFY(!frames->snapshot(frame).mockup);
        QCOMPARE(views->generatedProject(frame), QFileInfo(where).canonicalFilePath());
        frames->stop(frame);
    }

    // Build It from an empty frame -----------------------------------------------------------------------------------

    void buildItFromAnEmptyFrameMakesTheProjectFirstThenBuildsIntoIt()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        EditorSession &session = workspace.current().session;
        EditorCanvas canvas(session);
        const QUuid frame = frameFor(session, canvas, true);
        BrowserViews *views = BrowserViews::of(session);
        AgentBridge &bridge = *window.agent();
        views->setAgent(&bridge);
        QVERIFY(bridge.startServer().isEmpty());
        const QString where = folder();
        answer(QString(), PageTemplates::Stack::plainHtml, where);
        Confirm state;
        confirm(state);

        views->act(frame, BrowserViewHost::Action::buildIt);
        // No agent writes the starter page: the plan is the template.
        QTRY_COMPARE_WITH_TIMEOUT(state.asked, 1, 10'000);
        QVERIFY(state.said.contains(where + QStringLiteral("/index.html (new,")));
        // Then the design is built into it, as Build It does.
        const QString text = promptText();
        QVERIFY2(!text.isEmpty(), "Build It never started");
        QVERIFY(text.contains(QLatin1String("Designed at 300 px wide")));
        QVERIFY(text.contains(QLatin1String("mockup.png")));
        QVERIFY(!session.document()->find(frame)->browser->url.isEmpty());
        QCOMPARE(git(where, {"rev-list", "--count", "HEAD"}), QStringLiteral("1"));
        QCOMPARE(bridge.buildingFrame(), frame);
        bridge.stopWaiting();
    }
};

QTEST_MAIN(GeneratePageTests)
#include "GeneratePageTests.moc"
