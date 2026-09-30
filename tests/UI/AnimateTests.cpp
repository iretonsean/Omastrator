#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/AgentBridge.h"
#include "UI/AnimateSheet.h"
#include "UI/BrowserViews.h"
#include "UI/ElementBarActions.h"
#include "UI/LiveFrames.h"
#include "UI/MotionInspector.h"
#include "UI/MotionTimeline.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QFile>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QScopeGuard>
#include <memory>

// Animate (docs/MOTION.md, section 4): what the agent is told, that its motion is only previewed (from a worktree, on a server of
// its own) until Save to code, and that Discard, Stop and a second Animate leave the project as it was. A fake agent records the
// prompt and the test writes what it would write into its worktree. Headless Chromium on a throwaway profile; skips without it.
namespace {
constexpr int patience = 60'000;

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

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Site {
    QString folder;
    StaticServer server;
};

// A page hosted in a frame on `session`, on a canvas of its own that is shown, so the frame gets its tab.
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

class AnimateTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;
    QStringList m_folders;
    QString m_out;

    std::unique_ptr<Site> site()
    {
        auto made = std::make_unique<Site>();
        made->folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/animate%1").arg(++m_sites);
        m_folders.append(made->folder);
        for (const char *name : {"cards.html", "cards.css"}) {
            QFile source(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion/") + QLatin1String(name));
            if (!source.open(QIODevice::ReadOnly))
                return nullptr;
            write(made->folder + QLatin1Char('/') + (QLatin1String(name) == QLatin1String("cards.html") ? QStringLiteral("index.html") : QLatin1String(name)), source.readAll());
        }
        WriteBack::git(made->folder, {"init", "-q", "-b", "main"});
        WriteBack::git(made->folder, {"add", "-A"});
        WriteBack::git(made->folder, {"commit", "-q", "-m", "First"});
        if (!made->server.serve(made->folder).isEmpty())
            return nullptr;
        if (!ProjectRegistry::remember(page(*made), made->folder).isEmpty())
            return nullptr;
        return made;
    }

    QUrl page(const Site &served) const { return QUrl(served.server.url().toString() + QStringLiteral("index.html")); }

    static QJsonValue inPage(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        auto value = std::make_shared<QJsonValue>();
        bool answered = false;
        LiveFrames::of(session)->run(frame, [value, expression](LiveSession &live) {
            *value = live.evaluate(expression);
            return QString();
        }, [&answered](const QString &) { answered = true; });
        for (int i = 0; i < 600 && !answered; ++i)
            QTest::qWait(25);
        return *value;
    }

    int worktrees(const Site &served) { return int(WriteBack::git(served.folder, {"worktree", "list", "--porcelain"}).count(QLatin1String("worktree "))) - 1; }
    int commits(const Site &served) { return WriteBack::git(served.folder, {"rev-list", "--count", "HEAD"}).trimmed().toInt(); }
    QString status(const Site &served) { return WriteBack::git(served.folder, {"status", "--porcelain"}).trimmed(); }

    // The window, a frame on the site with Live running, the cards picked, and the timeline closed.
    struct Rig {
        std::unique_ptr<ProjectWorkspace> workspace = std::make_unique<ProjectWorkspace>();
        std::unique_ptr<ProjectWorkspaceView> window;
        // A document of its own, when a test closes it while the window and its agent carry on.
        std::unique_ptr<EditorSession> own;
        EditorSession *session = nullptr;
        std::unique_ptr<Hosted> hosted;
        // The timeline is on the hosted canvas, so it is the one Animate opens.
        std::unique_ptr<MotionTimeline> timeline;
        AgentBridge *bridge = nullptr;
        BrowserViews *views = nullptr;
    };

    void rig(Rig &r, const Site &served, bool ownDocument = false)
    {
        r.window = std::make_unique<ProjectWorkspaceView>(*r.workspace);
        r.bridge = r.window->agent();
        if (ownDocument)
            r.own = std::make_unique<EditorSession>();
        r.session = ownDocument ? r.own.get() : &r.workspace->current().session;
        r.hosted = std::make_unique<Hosted>(*r.session, page(served));
        r.views = BrowserViews::of(*r.session);
        r.views->setAgent(r.bridge);
        ElementBarActions::attach(r.bridge, r.hosted->canvas);
        r.timeline = std::make_unique<MotionTimeline>(*r.session, r.hosted->canvas);
        QTRY_VERIFY_WITH_TIMEOUT(!r.views->poolKey(r.hosted->frame).isNull(), patience);
        // Live runs on the frame, and the cards are picked.
        QVERIFY(r.hosted->canvas.enterEditPage(r.hosted->frame));
        LiveFrames *frames = LiveFrames::of(*r.session);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(r.hosted->frame).state, LiveSession::State::running, patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("!!(window.__oma && document.querySelector('#guji'))")).toBool(), patience);
        bool picked = false;
        frames->run(r.hosted->frame, [](LiveSession &live) { return live.selectElements({QStringLiteral("#guji")}); }, [&picked](const QString &) { picked = true; });
        QTRY_VERIFY_WITH_TIMEOUT(picked, patience);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(r.hosted->frame).selection.size(), qsizetype(1), patience);
    }

    // The prompt the fake agent was given for the last run.
    QString prompt() const
    {
        for (const QString &argument : FakeAgents::arguments(m_out, QStringLiteral("claude")))
            if (argument.contains(QLatin1String("motion task (request")))
                return argument;
        return {};
    }
    QString agentCwd() const { return FakeAgents::read(m_out + QStringLiteral("/claude.cwd")).trimmed(); }

    // What the agent would write: the cascade slowed, in its worktree.
    static void writeMotion(const QString &worktree, const QByteArray &duration = "900ms")
    {
        QByteArray css = read(worktree + "/cards.css");
        css.replace("--duration-cascade: 640ms;", "--duration-cascade: " + duration + ";");
        write(worktree + "/cards.css", css);
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
        m_out = m_directory.filePath(QStringLiteral("agents"));
        QDir().mkpath(m_out);
        qputenv("FAKE_OUT", m_out.toUtf8());
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
        BrowserViews::setPreviewChooser({});
        // A hung agent that has said it is done is stopped after this long, and then its files are read.
        AgentBridge::setPageDrainMs(150);
        QFile::remove(ProjectRegistry::path());
        // The agent stays alive until the test says what it wrote, as one that is still working does.
        qputenv("FAKE_MODE", "hang");
        for (const char *name : {"argv", "cwd", "child"})
            QFile::remove(m_out + QStringLiteral("/claude.") + QLatin1String(name));
    }

    void cleanup()
    {
        // A fake agent that hangs is let go: its sleep ends, and so does its script.
        const QString child = FakeAgents::read(m_out + QStringLiteral("/claude.child")).trimmed();
        if (!child.isEmpty())
            QProcess::execute(QStringLiteral("kill"), {child});
        for (const QString &folder : std::as_const(m_folders))
            LiveFrames::clearPending(folder);
        m_folders.clear();
        AgentBridge::setPageDrainMs(5000);
        BrowserViews::shutdownPool();
    }

    // The sheet, on its own.
    void theSheetTakesAnInstructionFromAChipAndSaysWhenItIsWriting()
    {
        AnimateSheet sheet(nullptr);
        QVERIFY(!sheet.isVisible());
        sheet.open(QStringLiteral("Claude"), 1);
        QVERIFY(sheet.isVisible());
        QCOMPARE(sheet.findChild<QLabel *>(QStringLiteral("animateAgent"))->text(), QStringLiteral("Your default agent: Claude"));
        QVERIFY(sheet.findChild<QLabel *>(QStringLiteral("animateNote"))->text().contains(QStringLiteral("You preview it here before anything is saved.")));
        const QList<QPushButton *> chips = sheet.findChildren<QPushButton *>(QStringLiteral("animateChip"));
        QCOMPARE(chips.size(), 4);
        QCOMPARE(chips[0]->text(), QStringLiteral("Reveal word by word on load"));
        chips[2]->click();
        QCOMPARE(sheet.text(), QStringLiteral("Lift on hover"));
        QSignalSpy generated(&sheet, &AnimateSheet::generate);
        sheet.findChild<QPushButton *>(QStringLiteral("animateGenerate"))->click();
        QCOMPARE(generated.size(), 1);
        QCOMPARE(generated.first().at(0).toString(), QStringLiteral("Lift on hover"));
        QCOMPARE(generated.first().at(1).toBool(), true);
        // While it writes: "Writing…", the steps, and Stop; Esc stops it rather than closing the sheet.
        sheet.setRunning(true);
        sheet.setSteps({QStringLiteral("✓ Asked Claude"), QStringLiteral("… Writing the motion"), QStringLiteral("· Starting the preview")});
        QCOMPARE(sheet.findChild<QPushButton *>(QStringLiteral("animateGenerate"))->text(), QStringLiteral("Writing…"));
        QVERIFY(!sheet.findChild<QPushButton *>(QStringLiteral("animateGenerate"))->isEnabled());
        QCOMPARE(sheet.findChild<QPushButton *>(QStringLiteral("animateCancel"))->text(), QStringLiteral("Stop"));
        QVERIFY(sheet.findChild<QLabel *>(QStringLiteral("animateSteps"))->text().contains(QStringLiteral("… Writing the motion")));
        QSignalSpy stopped(&sheet, &AnimateSheet::stopped);
        QSignalSpy cancelled(&sheet, &AnimateSheet::cancelled);
        QTest::keyClick(sheet.findChild<QPlainTextEdit *>(QStringLiteral("animateField")), Qt::Key_Escape);
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(cancelled.size(), 0);
        sheet.setRunning(false);
        QTest::keyClick(sheet.findChild<QPlainTextEdit *>(QStringLiteral("animateField")), Qt::Key_Escape);
        QCOMPARE(cancelled.size(), 1);
        // Several picked elements are one group; the reduced-motion rule is on unless turned off in the menu.
        sheet.findChild<QAction *>(QStringLiteral("animateReduced"))->setChecked(false);
        QVERIFY(!sheet.reducedMotion());
        sheet.close();
        QVERIFY(!sheet.isVisible());
    }

    void thePromptCarriesTheSelectionTokensStackAndExistingMotion()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QString failure = r.views->animate(r.hosted->frame, QStringLiteral("Lift on hover"), true);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // The page's motion is read first, so the agent starts a moment after the ask.
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        QCOMPARE(r.bridge->animatingFrame(), r.hosted->frame);
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        const QString text = prompt();
        // The agent works in a worktree of its own, told what to write and what it may not do.
        QVERIFY(agentCwd().startsWith(m_directory.filePath(QStringLiteral("data"))));
        QVERIFY(text.contains(agentCwd()));
        QVERIFY(text.contains(QStringLiteral("Lift on hover")));
        QVERIFY(text.contains(QStringLiteral("Don't commit, push, deploy or start a dev server")));
        // The selection as the page sees it, the page's tokens, the stack and the style file, and the motion already there.
        QVERIFY(text.contains(QStringLiteral("\"selector\": \"#guji\"")));
        QVERIFY(text.contains(QStringLiteral("--duration-cascade")));
        QVERIFY(text.contains(QStringLiteral("--ease-cascade")));
        QVERIFY(text.contains(QStringLiteral("The project: Plain HTML and CSS.")));
        QVERIFY(text.contains(QStringLiteral("cards.css")));
        QVERIFY(text.contains(QStringLiteral("nl-cascade")));
        // The page is on 127.0.0.1, which names no site: the prefix comes from the project's folder ("animate1"), and is a letter word.
        QVERIFY2(text.contains(QStringLiteral("starts with \"an-\"")), qPrintable(text.section(QStringLiteral("Any new @keyframes"), 1).left(80)));
        // The contract, and the way to say it is done.
        QVERIFY(text.contains(QStringLiteral("/* omastrator:motion <name> */")));
        QVERIFY(text.contains(QStringLiteral("prefers-reduced-motion")));
        QVERIFY(text.contains(QStringLiteral("\"action\": \"agentDone\"")));
        // Nothing has been written, and a second ask waits for this one.
        QVERIFY(status(*served).isEmpty());
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("More"), true).contains(QStringLiteral("still working")));
        r.bridge->stopWaiting();
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 0, patience);
    }

    void aResultIsOnlyAPreviewUntilSaveToCode()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
                const QUrl production = r.session->document()->find(r.hosted->frame)->browser->url;
        const int before = commits(*served);
        QSignalSpy notices(r.views, &BrowserViews::notice);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        const QString worktree = agentCwd();
        writeMotion(worktree);
        QString id = prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
        QVERIFY(r.bridge->liveAgentDone(id, QStringLiteral("Slowed the cascade")).isEmpty());
        // Its process is waited for, so what it writes after saying so is in the preview.
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);

        // The project is as it was; the change waits in the worktree, served on a server of its own.
        QVERIFY(status(*served).isEmpty());
        QVERIFY(QFileInfo::exists(worktree));
        auto preview = r.bridge->previewOf(served->folder);
        QVERIFY(preview);
        QCOMPARE(preview->files, QStringList{"cards.css"});
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder)->ready, patience);
        preview = r.bridge->previewOf(served->folder);
        QVERIFY(preview->problems.isEmpty());
        QVERIFY(preview->notice.isEmpty());
        QVERIFY(preview->url != served->server.url());
        // The frame shows the preview, and the document keeps its own address.
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("location.port")).toString() == QString::number(preview->url.port()), patience);
        QCOMPARE(r.session->document()->find(r.hosted->frame)->browser->url, production);
        QVERIFY(r.views->previewing(r.hosted->frame));
        const BrowserViewHost::Bar bar = r.views->bar(r.hosted->frame);
        QVERIFY(bar.dev);
        QCOMPARE(bar.devLabel, QStringLiteral("preview"));
        QVERIFY(bar.devTip.contains(preview->branch));
        // The toast says so, and the timeline is open on the new motion.
        bool told = false;
        for (const auto &args : notices)
            told = told || (args.first().toString().contains(QStringLiteral("cards.css")) && args.first().toString().contains(QStringLiteral("Preview only until you save.")));
        QVERIFY(told);
        MotionTimeline *timeline = r.timeline.get();
        QVERIFY(timeline && timeline->isOpen());
        QTRY_VERIFY_WITH_TIMEOUT(timeline->selectedTrack() || !timeline->timeline().tracks.isEmpty(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("getComputedStyle(document.documentElement).getPropertyValue('--duration-cascade').trim()")).toString() == QLatin1String("900ms"), patience);

        // Tuned in the preview, then Save to code: the agent's change, then the tuning on top, in one commit and one Review.
        timeline->setToken(QStringLiteral("--stagger-cascade"), QStringLiteral("200ms"), false);
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(*r.session)->snapshot(r.hosted->frame).edits.size(), size_t(1), patience);
        const int reviews = int(r.bridge->liveReviews().size());
        QVERIFY2(r.views->savePreview(r.hosted->frame).isEmpty(), "saved");
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->previewOf(served->folder), patience);
        QByteArray css = read(served->folder + "/cards.css");
        QVERIFY(css.contains("--duration-cascade: 900ms;"));
        QVERIFY(css.contains("--stagger-cascade: 200ms;"));
        QVERIFY(css.contains("--ease-cascade: cubic-bezier(0.25, 1, 0.5, 1);"));
        QTRY_COMPARE_WITH_TIMEOUT(commits(*served), before + 1, patience);
        QCOMPARE(int(r.bridge->liveReviews().size()), reviews + 2);
        QCOMPARE(r.bridge->liveReviews()[size_t(reviews)].title, QStringLiteral("Animate: article#guji"));
        QVERIFY(status(*served).isEmpty());
        QCOMPARE(worktrees(*served), 0);
        QVERIFY(!QFileInfo::exists(worktree));
        // The frame is back on the project, and the server the preview used is let go.
        QVERIFY(!r.views->previewing(r.hosted->frame));
        QVERIFY(!DevServers::shared().running(worktree));
    }

    void saveToCodeWritesOnlyWhatTheAgentWrote()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        const int before = commits(*served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        const QString worktree = agentCwd();
        writeMotion(worktree);
        QVERIFY(r.bridge->liveAgentDone(prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0), QStringLiteral("Slowed the cascade")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder) && r.bridge->previewOf(served->folder)->ready, patience);
        // The preview's server ran in the worktree, and what it changed there (a tracked file, a generated one) is not the agent's.
        write(worktree + "/index.html", read(worktree + "/index.html") + "<!-- rewritten by the preview's server -->\n");
        write(worktree + "/routeTree.gen.ts", "// generated\n");
        QVERIFY2(r.views->savePreview(r.hosted->frame).isEmpty(), "saved");
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->previewOf(served->folder), patience);
        QTRY_COMPARE_WITH_TIMEOUT(commits(*served), before + 1, patience);
        QVERIFY(read(served->folder + "/cards.css").contains("--duration-cascade: 900ms;"));
        QVERIFY(!read(served->folder + "/index.html").contains("rewritten"));
        QVERIFY(!QFileInfo::exists(served->folder + "/routeTree.gen.ts"));
        // The commit holds the agent's file and nothing else.
        QCOMPARE(WriteBack::git(served->folder, {"show", "--name-only", "--format=", "HEAD"}).trimmed(), QStringLiteral("cards.css"));
        QVERIFY(status(*served).isEmpty());
    }

    void whatTheAgentWritesAfterSayingItIsDoneIsInThePreview()
    {
        NEEDS_CHROMIUM;
        // A claude that formats once more a second after it has said it is done, and then exits.
        write(m_directory.filePath(QStringLiteral("late/claude")), "#!/bin/sh\nsleep 1\nsed -i 's/--duration-cascade: 640ms;/--duration-cascade: 777ms;/' cards.css\nexit 0\n", true);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", (m_directory.filePath(QStringLiteral("late")) + QLatin1Char(':') + QString::fromLocal8Bit(path)).toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("PATH", path); });
        AgentBridge::setPageDrainMs(15'000);
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        const QString id = r.bridge->waiting()->requestId;
        QVERIFY(r.bridge->liveAgentDone(id, QStringLiteral("Done")).isEmpty());
        // Its process is still running: the preview waits for it, and the agent still counts as working.
        QTest::qWait(300);
        QVERIFY(!r.bridge->previewOf(served->folder));
        QVERIFY(r.bridge->waiting());
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder).has_value(), patience);
        QCOMPARE(r.bridge->previewOf(served->folder)->files, QStringList{"cards.css"});
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder)->ready, patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("getComputedStyle(document.documentElement).getPropertyValue('--duration-cascade').trim()")).toString() == QLatin1String("777ms"), patience);
        QVERIFY(status(*served).isEmpty());
    }

    void closingTheDocumentDuringAPreviewDiscardsItWithoutTouchingTheDyingSession()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served, true);
        const int before = commits(*served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        const QString worktree = agentCwd();
        writeMotion(worktree);
        QVERIFY(r.bridge->liveAgentDone(prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0), QStringLiteral("Slowed")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder) && r.bridge->previewOf(served->folder)->ready, patience);
        QVERIFY(r.views->previewing(r.hosted->frame));
        // The document closes while the preview shows: the timeline, the canvas, then the session.
        r.timeline.reset();
        r.hosted.reset();
        r.own.reset();
        QVERIFY(!r.bridge->previewOf(served->folder));
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 0, patience);
        QVERIFY(!QFileInfo::exists(worktree));
        QVERIFY(status(*served).isEmpty());
        QCOMPARE(commits(*served), before);
        QVERIFY(!DevServers::shared().running(worktree));
    }

    void stopBeforeTheAgentLaunchesAsksNothingAndSaysSo()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QSignalSpy ended(r.views, &BrowserViews::animateEnded);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Lift on hover"), true).isEmpty());
        // The page's motion is being read before the agent is asked; Stop lands in that gap.
        r.views->stopAnimate(r.hosted->frame);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended.first().at(0).toUuid(), r.hosted->frame);
        QTest::qWait(1500);
        QVERIFY(!r.bridge->waiting());
        QVERIFY(prompt().isEmpty());
        QCOMPARE(worktrees(*served), 0);
        // The frame can be asked again.
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Lift on hover"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        r.bridge->stopWaiting();
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 0, patience);
    }

    void discardingLeavesGitCleanAndRemovesTheWorktreeAndDropsWhatWasTuned()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        const int before = commits(*served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        const QString worktree = agentCwd();
        writeMotion(worktree);
        QVERIFY(r.bridge->liveAgentDone(prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0), QStringLiteral("Slowed")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder)->ready, patience);
        MotionTimeline *timeline = r.timeline.get();
        QTRY_VERIFY_WITH_TIMEOUT(timeline && timeline->isOpen() && !timeline->timeline().tracks.isEmpty(), patience);
        timeline->setToken(QStringLiteral("--stagger-cascade"), QStringLiteral("200ms"), false);
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(*r.session)->snapshot(r.hosted->frame).edits.size(), size_t(1), patience);

        r.views->discardPreview(r.hosted->frame);
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->previewOf(served->folder), patience);
        QVERIFY(status(*served).isEmpty());
        QCOMPARE(commits(*served), before);
        QCOMPARE(worktrees(*served), 0);
        QVERIFY(!QFileInfo::exists(worktree));
        // What was tuned against the preview's motion is dropped, and the page is the project's again.
        QVERIFY(LiveFrames::pendingEdits(served->folder).empty());
        QVERIFY(!r.views->previewing(r.hosted->frame));
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("location.port")).toString() == QString::number(served->server.url().port()), patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(*r.session, r.hosted->frame, QStringLiteral("getComputedStyle(document.documentElement).getPropertyValue('--duration-cascade').trim()")).toString() == QLatin1String("640ms"), patience);
        QVERIFY(!DevServers::shared().running(worktree));
    }

    void stoppingWhileItWritesEndsTheRunAndRemovesTheWorktree()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 1, patience);
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        r.bridge->stopWaiting();
        QVERIFY(!r.bridge->waiting());
        QVERIFY(r.bridge->animatingFrame().isNull());
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 0, patience);
        QVERIFY(!r.bridge->previewOf(served->folder));
        QVERIFY(status(*served).isEmpty());
        // And another can be asked for at once.
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Again"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        r.bridge->stopWaiting();
    }

    void aSecondAnimateAsksWhetherToKeepOrDiscardThePreview()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        writeMotion(agentCwd());
        QVERIFY(r.bridge->liveAgentDone(prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0), QStringLiteral("Slowed")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->previewOf(served->folder)->ready, patience);

        int asked = 0;
        BrowserViews::PreviewAnswer answer = BrowserViews::PreviewAnswer::keep;
        BrowserViews::setPreviewChooser([&] {
            ++asked;
            return answer;
        });
        // Keep, and Cancel: nothing is started and the preview stays. Each says so, so the sheet does not wait for a run.
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("More"), true).contains(QLatin1String("Kept")));
        answer = BrowserViews::PreviewAnswer::cancel;
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("More"), true).contains(QLatin1String("Cancelled")));
        QCOMPARE(asked, 2);
        QVERIFY(!r.bridge->waiting());
        QVERIFY(r.bridge->previewOf(served->folder));
        // Discard: the preview goes and the new ask starts.
        answer = BrowserViews::PreviewAnswer::discard;
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("More"), true).isEmpty());
        QCOMPARE(asked, 3);
        QVERIFY(!r.bridge->previewOf(served->folder));
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        r.bridge->stopWaiting();
        QTRY_COMPARE_WITH_TIMEOUT(worktrees(*served), 0, patience);
    }

    void aResultThatBreaksTheContractIsStillShownWithTheLine()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        QVERIFY(r.views->animate(r.hosted->frame, QStringLiteral("Slow the cascade"), true).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!prompt().isEmpty(), patience);
        // The agent used a token and never declared it.
        QByteArray css = read(agentCwd() + "/cards.css");
        css.replace("--duration-cascade: 640ms; ", "");
        write(agentCwd() + "/cards.css", css);
        QVERIFY(r.bridge->liveAgentDone(prompt().section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0), QStringLiteral("Slowed")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting(), patience);
        const auto preview = r.bridge->previewOf(served->folder);
        QVERIFY(preview);
        QVERIFY(!preview->problems.isEmpty());
        QVERIFY(preview->problems.join(QLatin1Char(' ')).contains(QStringLiteral("--duration-cascade")));
        QCOMPARE(preview->notice, QStringLiteral("Some of this motion isn't tunable here. Ask to fix it, or edit it in code."));
        QVERIFY(r.bridge->discardPreview(served->folder).isEmpty());
    }

    void theElementBarsAnimateButtonOpensTheSheetAndGenerateStartsTheAgent()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        Rig r;
        rig(r, *served);
        EditorCanvas &canvas = r.hosted->canvas;
        auto *bar = canvas.findChild<ElementBar *>();
        QVERIFY(bar);
        QTRY_VERIFY_WITH_TIMEOUT(bar->findChild<QToolButton *>(QStringLiteral("elementAnimate")) != nullptr, patience);
        auto *button = bar->findChild<QToolButton *>(QStringLiteral("elementAnimate"));
        QCOMPARE(button->text(), QStringLiteral("Animate"));
        button->click();
        auto *sheet = canvas.findChild<AnimateSheet *>(QStringLiteral("animateSheet"));
        QVERIFY(sheet && sheet->isVisible());
        sheet->findChildren<QPushButton *>(QStringLiteral("animateChip"))[2]->click();
        sheet->findChild<QPushButton *>(QStringLiteral("animateGenerate"))->click();
        // Writing…: the sheet says so with Stop, and the agent is on its way.
        QTRY_VERIFY_WITH_TIMEOUT(r.bridge->waiting().has_value(), patience);
        QVERIFY(sheet->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(prompt().contains(QStringLiteral("Lift on hover")), patience);
        // Stop ends it, and Esc after that closes the sheet.
        sheet->findChild<QPushButton *>(QStringLiteral("animateCancel"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(!r.bridge->waiting().has_value(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->isRunning(), patience);
    }
};

QTEST_MAIN(AnimateTests)
#include "AnimateTests.moc"
