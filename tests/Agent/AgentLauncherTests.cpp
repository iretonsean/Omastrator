#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Setup.h"
#include "FakeAgents.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <csignal>

namespace {
// Stands in for `omarchy`: names $FAKE_AGENT as the default and records a prompt launch.
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ] && [ \"$2\" = agent ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then\n"
                                    "  if [ -n \"$FAKE_FAIL\" ]; then echo \"$FAKE_FAIL\" >&2; exit 3; fi\n"
                                    "  printf '%s' \"$3\" > \"$FAKE_OUT/prompt\"\n"
                                    "  pwd > \"$FAKE_OUT/cwd\"\n"
                                    "  printf '%s' \"$OMASTRATOR_BIN\" > \"$FAKE_OUT/bin\"\n"
                                    "  printf '%s' \"$OMASTRATOR_SOCKET\" > \"$FAKE_OUT/socket\"\n"
                                    "  printf '%s' \"$#\" > \"$FAKE_OUT/count\"\n"
                                    "  exit 0\n"
                                    "fi\n"
                                    "exit 2\n";

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
}

struct LaunchSetup {
    QString prompt = QStringLiteral("Omastrator task: Roast My Design (request req-7).\nRoast it.");
    AgentAccess access = AgentAccess::omastrator;
    QString task = QStringLiteral("roast");
    int timeoutSeconds = 0;
    bool cancelAfterStart = false;
};

class AgentLauncherTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString out(const QString &name) const { return m_directory.filePath(QStringLiteral("out/") + name); }
    QString outFolder() const { return m_directory.filePath(QStringLiteral("out")); }
    QStringList logs() const
    {
        return QDir(AgentLauncher::logFolder()).entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Name);
    }

    // Launches the default agent headless and waits for it to end.
    AgentRun::End runToEnd(const LaunchSetup &setup, QString *lastLine = nullptr, QString *logPath = nullptr)
    {
        AgentLauncher::LaunchOptions options;
        options.access = setup.access;
        options.task = setup.task;
        options.timeoutSeconds = setup.timeoutSeconds;
        bool finished = false;
        AgentRun::End end = AgentRun::End::running;
        options.finished = [&](AgentRun &run) {
            finished = true;
            end = run.end();
            if (lastLine)
                *lastLine = run.lastLine();
            if (logPath)
                *logPath = run.logPath();
        };
        QPointer<AgentRun> run;
        const QString error = AgentLauncher::launch(setup.prompt, QString(), options, &run);
        if (!error.isEmpty() || !run)
            return AgentRun::End::crashed;
        if (setup.cancelAfterStart) {
            QTest::qWait(300);
            run->cancel();
        }
        if (!QTest::qWaitFor([&] { return finished; }, 15000))
            return AgentRun::End::running;
        return end;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        const QString script = m_directory.filePath(QStringLiteral("omarchy"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fakeOmarchy);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(QDir(m_directory.path()).mkpath(QStringLiteral("out")));
        qputenv("OMASTRATOR_OMARCHY", script.toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("out")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", "/run/user/test/omastrator.sock");
        // Settings land in the temporary folder, never the user's config.
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        // Run logs too, never the user's.
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    }

    void init()
    {
        qunsetenv("FAKE_FAIL");
        qputenv("FAKE_AGENT", "sh");
        qputenv("FAKE_MODE", "quiet");
        QDir(outFolder()).removeRecursively();
        QDir().mkpath(outFolder());
        AgentLauncher::setShowTerminal(false);
    }

    void roastHeatsGetHotterAndKeepTheHardLines()
    {
        QSettings settings;
        settings.remove(QStringLiteral("roast/heat"));
        // Savage until the user picks, then whatever they picked.
        QCOMPARE(AgentLauncher::savedRoastHeat(), AgentLauncher::RoastHeat::savage);
        AgentLauncher::saveRoastHeat(AgentLauncher::RoastHeat::unhinged);
        QCOMPARE(AgentLauncher::savedRoastHeat(), AgentLauncher::RoastHeat::unhinged);
        QCOMPARE(AgentLauncher::roastHeat(QStringLiteral("spicy")), AgentLauncher::RoastHeat::spicy);
        QVERIFY(!AgentLauncher::roastHeat(QStringLiteral("nuclear")));
        settings.remove(QStringLiteral("roast/heat"));

        const QString friendly = AgentLauncher::roastHeatGuide(AgentLauncher::RoastHeat::friendly);
        const QString spicy = AgentLauncher::roastHeatGuide(AgentLauncher::RoastHeat::spicy);
        const QString savage = AgentLauncher::roastHeatGuide(AgentLauncher::RoastHeat::savage);
        const QString unhinged = AgentLauncher::roastHeatGuide(AgentLauncher::RoastHeat::unhinged);
        QVERIFY(friendly.contains(QLatin1String("No swearing")));
        QVERIFY(spicy.contains(QLatin1String("no fuck")));
        QVERIFY(savage.contains(QLatin1String("up to two fucks")));
        QVERIFY(unhinged.contains(QLatin1String("Swearing:** unlimited")));
        for (const QString &guide : {friendly, spicy, savage, unhinged}) {
            QVERIFY(guide.contains(QLatin1String("Never, at any heat:** slurs")));
            QVERIFY(guide.contains(QLatin1String("suicide or self-harm")));
        }
        const QString prompt = AgentLauncher::roastPrompt(QStringLiteral("req-9"), QStringLiteral("/tmp/b.png"), false,
                                                          AgentLauncher::RoastHeat::friendly);
        QVERIFY(prompt.contains(friendly));
        QVERIFY(!prompt.contains(QLatin1String("Heat: Savage")));
    }

    void noDefaultAgentSaysWhereToChooseOne()
    {
        qputenv("FAKE_AGENT", "");
        QString error;
        QVERIFY(AgentLauncher::defaultAgent(&error).isEmpty());
        QCOMPARE(error, QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent."));
        QCOMPARE(AgentLauncher::launch(QStringLiteral("Anything")), error);
        QVERIFY(!QFile::exists(out(QStringLiteral("prompt"))));
    }

    void anUninstalledAgentIsNamed()
    {
        qputenv("FAKE_AGENT", "surely-not-an-installed-agent");
        QString error;
        QVERIFY(AgentLauncher::defaultAgent(&error).isEmpty());
        QVERIFY(error.startsWith(QLatin1String("surely-not-an-installed-agent is selected but is not installed.")));
    }

    void missingOmarchyIsReported()
    {
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("no-such-omarchy")).toUtf8());
        QString error;
        QVERIFY(AgentLauncher::defaultAgent(&error).isEmpty());
        QVERIFY(error.contains(QLatin1String("omastrator --mcp")));
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
    }

    void anUnknownAgentOpensInATerminalWithoutAShell()
    {
        QCOMPARE(AgentLauncher::defaultAgent(), QStringLiteral("sh"));
        const QString prompt = QStringLiteral("Make it \"pop\"; $(rm -rf ~) `true` 'quoted'\nSecond line");
        QPointer<AgentRun> run;
        QCOMPARE(AgentLauncher::launch(prompt, QString(), AgentLauncher::LaunchOptions{}, &run), QString());
        QVERIFY(!run);
        QCOMPARE(read(out(QStringLiteral("prompt"))), prompt);
        QCOMPARE(read(out(QStringLiteral("count"))), QStringLiteral("3"));
        const QString folder = AgentLauncher::folder();
        QCOMPARE(folder, m_directory.filePath(QStringLiteral("data/omastrator/agent")));
        QCOMPARE(read(out(QStringLiteral("cwd"))).trimmed(), folder);
        QCOMPARE(read(out(QStringLiteral("socket"))), QStringLiteral("/run/user/test/omastrator.sock"));
        QVERIFY(!read(out(QStringLiteral("bin"))).isEmpty());

        const QString agents = read(QDir(folder).filePath(QStringLiteral("AGENTS.md")));
        QCOMPARE(read(QDir(folder).filePath(QStringLiteral("CLAUDE.md"))), agents);
        for (const AgentProtocol::Method &method : AgentProtocol::methods())
            QVERIFY2(agents.contains(QStringLiteral("`%1 {").arg(method.name)), qPrintable(method.name));
        QVERIFY(agents.contains(QLatin1String("Each roast task names a **heat**")));
        QVERIFY(agents.contains(QLatin1String("proposal_finish")));
        // The CLI by its absolute path, which is what headless runs allow.
        QVERIFY(agents.contains(Setup::shellQuote(QCoreApplication::applicationFilePath()) + QStringLiteral(" agent document_get")));
        // No MCP server in the folder: it made Claude Code ask for approval on every run.
        QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral(".mcp.json"))));
    }

    void anOldMcpConfigIsRemoved()
    {
        const QString stale = QDir(AgentLauncher::folder()).filePath(QStringLiteral(".mcp.json"));
        QDir().mkpath(AgentLauncher::folder());
        QFile file(stale);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"mcpServers\": {}}");
        file.close();
        QCOMPARE(AgentLauncher::writeInstructions(AgentLauncher::folder(), QStringLiteral("/usr/bin/omastrator"), QString()), QString());
        QVERIFY(!QFile::exists(stale));
    }

    void claudeRunsHeadlessWithOnlyOmastratorsCli()
    {
        qputenv("FAKE_AGENT", "claude");
        const LaunchSetup setup;
        QString logPath;
        QCOMPARE(runToEnd(setup, nullptr, &logPath), AgentRun::End::exited);
        // No terminal: omarchy was only asked which agent, never to prompt it.
        QVERIFY(!QFile::exists(out(QStringLiteral("prompt"))));
        const QString cli = Setup::shellQuote(QCoreApplication::applicationFilePath()) + QStringLiteral(" agent");
        const QStringList expected{"-p", setup.prompt, "--output-format", "text", "--no-session-persistence", "--strict-mcp-config",
                                   "--mcp-config", R"({"mcpServers":{}})", "--permission-mode", "acceptEdits", "--tools", "Bash,Read",
                                   "--allowedTools", QStringLiteral("Bash(%1 *)").arg(cli), "Read"};
        QCOMPARE(FakeAgents::arguments(outFolder(), QStringLiteral("claude")), expected);
        QVERIFY(!FakeAgents::arguments(outFolder(), QStringLiteral("claude")).contains(QStringLiteral("--dangerously-skip-permissions")));
        QCOMPARE(read(out(QStringLiteral("claude.cwd"))).trimmed(), AgentLauncher::folder());
        QCOMPARE(read(out(QStringLiteral("claude.socket"))), QStringLiteral("/run/user/test/omastrator.sock"));
        QVERIFY(!QFile::exists(QDir(AgentLauncher::folder()).filePath(QStringLiteral(".mcp.json"))));

        // The log: what ran and what it said, without the task or the environment.
        QVERIFY(logPath.startsWith(AgentLauncher::logFolder()));
        QVERIFY(logPath.endsWith(QLatin1String("-roast.log")));
        const QString log = read(logPath);
        QVERIFY(log.contains(QLatin1String("agent: claude")));
        QVERIFY(log.contains(QLatin1String("I looked at it and decided not to.")));
        QVERIFY(log.contains(QLatin1String("exited with code 0")));
        QVERIFY(log.contains(QStringLiteral("<the task, %1 characters>").arg(setup.prompt.size())));
        QVERIFY(!log.contains(QLatin1String("Roast it.")));
        QVERIFY(!log.contains(QLatin1String("OMASTRATOR_SOCKET")));
    }

    void projectAccessAllowsEditingItsFolder()
    {
        qputenv("FAKE_AGENT", "claude");
        QTemporaryDir project;
        LaunchSetup setup;
        setup.access = AgentAccess::project;
        setup.task = QStringLiteral("live");
        AgentLauncher::LaunchOptions options;
        options.access = AgentAccess::project;
        options.workingDirectory = project.path();
        bool finished = false;
        options.finished = [&](AgentRun &) { finished = true; };
        QCOMPARE(AgentLauncher::launch(setup.prompt, QString(), options), QString());
        QTRY_VERIFY(finished);
        const QStringList arguments = FakeAgents::arguments(outFolder(), QStringLiteral("claude"));
        QCOMPARE(arguments.mid(arguments.indexOf(QStringLiteral("--tools"))),
                 (QStringList{"--tools", "Bash,Read,Edit,Write,Glob,Grep", "--allowedTools", "Bash", "Read", "Edit", "Write", "Glob", "Grep"}));
        QVERIFY(arguments.contains(QStringLiteral("acceptEdits")));
        QCOMPARE(read(out(QStringLiteral("claude.cwd"))).trimmed(), project.path());
        // Nothing is written into the project.
        QVERIFY(QDir(project.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        // launchIn is the same, for the Live tasks.
        finished = false;
        QFile::remove(out(QStringLiteral("claude.cwd")));
        QCOMPARE(AgentLauncher::launchIn(project.path(), setup.prompt), QString());
        QTRY_VERIFY(QFile::exists(out(QStringLiteral("claude.cwd"))));
        QTRY_VERIFY(read(out(QStringLiteral("claude.cwd"))).trimmed() == project.path());
        QVERIFY(FakeAgents::arguments(outFolder(), QStringLiteral("claude")).contains(QStringLiteral("Bash,Read,Edit,Write,Glob,Grep")));
    }

    void codexOpencodeAndGeminiRunHeadless()
    {
        const QString cli = Setup::shellQuote(QCoreApplication::applicationFilePath()) + QStringLiteral(" agent");
        const LaunchSetup setup;
        qputenv("FAKE_AGENT", "codex");
        QCOMPARE(runToEnd(setup), AgentRun::End::exited);
        QCOMPARE(FakeAgents::arguments(outFolder(), QStringLiteral("codex")),
                 (QStringList{"exec", "--skip-git-repo-check", "--ephemeral", "--color", "never", "-c", "approval_policy=\"never\"",
                              "--sandbox", "workspace-write", "-c", "sandbox_workspace_write.network_access=true", "--", setup.prompt}));

        qputenv("FAKE_AGENT", "opencode");
        QCOMPARE(runToEnd(setup), AgentRun::End::exited);
        QCOMPARE(FakeAgents::arguments(outFolder(), QStringLiteral("opencode")), (QStringList{"run", setup.prompt}));
        const QJsonObject permission =
            QJsonDocument::fromJson(read(out(QStringLiteral("opencode.config"))).toUtf8()).object()["permission"].toObject();
        QCOMPARE(permission["edit"].toString(), QStringLiteral("deny"));
        QCOMPARE(permission["bash"].toObject()["*"].toString(), QStringLiteral("deny"));
        QCOMPARE(permission["bash"].toObject()[cli + QStringLiteral(" *")].toString(), QStringLiteral("allow"));

        qputenv("FAKE_AGENT", "gemini");
        QCOMPARE(runToEnd(setup), AgentRun::End::exited);
        QCOMPARE(FakeAgents::arguments(outFolder(), QStringLiteral("gemini")),
                 (QStringList{"-p", setup.prompt, "--output-format", "text", "--skip-trust", "--approval-mode", "default", "--allowed-tools",
                              "read_file", QStringLiteral("run_shell_command(%1)").arg(cli)}));
        QVERIFY(!QFile::exists(out(QStringLiteral("prompt"))));

        // Project access: Codex's sandbox is the same, the others allow more.
        QVERIFY(AgentLauncher::headlessCommand(QStringLiteral("codex"), AgentAccess::project, QStringLiteral("x"), QStringLiteral("/b"))
                    ->arguments.contains(QStringLiteral("workspace-write")));
        const auto opencode = AgentLauncher::headlessCommand(QStringLiteral("opencode"), AgentAccess::project, QStringLiteral("x"), QStringLiteral("/b"));
        QVERIFY(opencode->environment.value(0).contains(QLatin1String("\"edit\":\"allow\"")));
        QVERIFY(AgentLauncher::headlessCommand(QStringLiteral("gemini"), AgentAccess::project, QStringLiteral("x"), QStringLiteral("/b"))
                    ->arguments.contains(QStringLiteral("auto_edit")));
        // Anyone else goes to a terminal.
        QVERIFY(!AgentLauncher::headlessCommand(QStringLiteral("pi"), AgentAccess::omastrator, QStringLiteral("x"), QStringLiteral("/b")));
        // Quick, watched edits name a model for Claude; the others keep their own.
        const QStringList quick = AgentLauncher::headlessCommand(QStringLiteral("claude"), AgentAccess::omastrator, QStringLiteral("x"),
                                                                 QStringLiteral("/b"), QStringLiteral("sonnet"))->arguments;
        QCOMPARE(quick.mid(quick.indexOf(QStringLiteral("--model")), 2), (QStringList{"--model", "sonnet"}));
        QVERIFY(!AgentLauncher::headlessCommand(QStringLiteral("claude"), AgentAccess::omastrator, QStringLiteral("x"), QStringLiteral("/b"))
                     ->arguments.contains(QStringLiteral("--model")));
        qunsetenv("OMASTRATOR_QUICK_MODEL");
        QCOMPARE(AgentLauncher::quickModel(), QStringLiteral("sonnet"));
        qputenv("OMASTRATOR_QUICK_MODEL", "default");
        QCOMPARE(AgentLauncher::quickModel(), QString());
        qunsetenv("OMASTRATOR_QUICK_MODEL");
    }

    void aFailedRunSaysWhyInItsLastLine()
    {
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "fail");
        QString line;
        QString logPath;
        QCOMPARE(runToEnd(LaunchSetup{}, &line, &logPath), AgentRun::End::exited);
        QCOMPARE(line, QStringLiteral("Error: not logged in. Run /login"));
        QVERIFY(read(logPath).contains(QLatin1String("exited with code 1")));
        // A clean exit's stdout is the agent signing off, not a reason.
        qputenv("FAKE_MODE", "quiet");
        QCOMPARE(runToEnd(LaunchSetup{}, &line), AgentRun::End::exited);
        QCOMPARE(line, QString());
    }

    void cancelStopsTheRunAndWhatItStarted()
    {
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "hang");
        LaunchSetup setup;
        setup.cancelAfterStart = true;
        QString logPath;
        QCOMPARE(runToEnd(setup, nullptr, &logPath), AgentRun::End::cancelled);
        QTRY_VERIFY(QFile::exists(out(QStringLiteral("claude.child"))));
        const pid_t child = pid_t(read(out(QStringLiteral("claude.child"))).trimmed().toInt());
        QVERIFY(child > 0);
        QTRY_VERIFY(::kill(child, 0) != 0);
        QVERIFY(read(logPath).contains(QLatin1String("cancelled")));
    }

    void aRunThatTakesTooLongIsStopped()
    {
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "hang");
        LaunchSetup setup;
        setup.timeoutSeconds = 1;
        QString logPath;
        QCOMPARE(runToEnd(setup, nullptr, &logPath), AgentRun::End::timedOut);
        QVERIFY(read(logPath).contains(QLatin1String("timed out after 1 s")));
        const pid_t child = pid_t(read(out(QStringLiteral("claude.child"))).trimmed().toInt());
        QTRY_VERIFY(child > 0 && ::kill(child, 0) != 0);
        // The default is five minutes, and the setting changes it.
        QCOMPARE(AgentLauncher::timeoutSeconds(AgentAccess::omastrator), 300);
        QSettings().setValue(QStringLiteral("agent/timeoutSeconds"), 42);
        QCOMPARE(AgentLauncher::timeoutSeconds(AgentAccess::omastrator), 42);
        QSettings().remove(QStringLiteral("agent/timeoutSeconds"));
    }

    void onlyTheLastThirtyLogsAreKept()
    {
        QDir().mkpath(AgentLauncher::logFolder());
        for (int index = 0; index < 35; ++index) {
            QFile file(QDir(AgentLauncher::logFolder()).filePath(QStringLiteral("20000101-000000-%1-old.log").arg(index, 3, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        qputenv("FAKE_AGENT", "claude");
        QString logPath;
        QCOMPARE(runToEnd(LaunchSetup{}, nullptr, &logPath), AgentRun::End::exited);
        const QStringList kept = logs();
        QCOMPARE(kept.size(), 30);
        QCOMPARE(kept.last(), QFileInfo(logPath).fileName());
        QVERIFY(!kept.contains(QStringLiteral("20000101-000000-000-old.log")));
    }

    void showTerminalUsesOmarchyForAnyAgent()
    {
        qputenv("FAKE_AGENT", "claude");
        AgentLauncher::setShowTerminal(true);
        QPointer<AgentRun> run;
        QCOMPARE(AgentLauncher::launch(QStringLiteral("In a terminal"), QString(), AgentLauncher::LaunchOptions{}, &run), QString());
        QVERIFY(!run);
        QCOMPARE(read(out(QStringLiteral("prompt"))), QStringLiteral("In a terminal"));
        QVERIFY(!QFile::exists(out(QStringLiteral("claude.argv"))));
        AgentLauncher::setShowTerminal(false);
    }

    void aLongTaskTravelsAsAFile()
    {
        const QString prompt = QStringLiteral("Brief. ") + QString(120 * 1024, QLatin1Char('x'));
        QCOMPARE(AgentLauncher::launch(prompt), QString());
        QVERIFY(read(out(QStringLiteral("prompt"))).contains(QLatin1String("TASK.md")));
        QCOMPARE(read(QDir(AgentLauncher::folder()).filePath(QStringLiteral("TASK.md"))), prompt);
    }

    void aFailedLaunchSaysWhy()
    {
        qputenv("FAKE_FAIL", "no terminal found");
        const QString error = AgentLauncher::launch(QStringLiteral("Hello"));
        QCOMPARE(error, QStringLiteral("Could not launch sh: no terminal found"));
        QCOMPARE(AgentLauncher::launch(QStringLiteral("  ")), QStringLiteral("There is nothing to ask the agent."));
    }

    void promptsNameTheRequestAndTheMethods()
    {
        const QString generate = AgentLauncher::generatePrompt(QStringLiteral("req-1"), QStringLiteral("A fox logo"), 9, QRectF(0, 0, 120, 80));
        QVERIFY(generate.contains(QLatin1String("req-1")) && generate.contains(QLatin1String("A fox logo")));
        QVERIFY(generate.contains(QLatin1String("show_variations {\"requestId\": \"req-1\"")));
        QVERIFY(generate.contains(QLatin1String("Make 6 distinct variations")));
        QVERIFY(generate.contains(QStringLiteral("120 × 80 pt")));
        QVERIFY(generate.contains(QLatin1String("AGENTS.md")));

        const QString refine = AgentLauncher::generatePrompt(QStringLiteral("req-2"), QStringLiteral("rounder"), 3, std::nullopt,
                                                             {{QStringLiteral("A fox logo"), QStringLiteral("<svg id='picked'/>")}});
        QVERIFY(refine.contains(QLatin1String("<svg id='picked'/>")));
        QVERIFY(refine.contains(QLatin1String("1. A fox logo")) && refine.contains(QLatin1String("rounder")));
        QVERIFY(refine.contains(QLatin1String("document_get")));

        const QString edit = AgentLauncher::editPrompt(QStringLiteral("req-3"), QStringLiteral("Recolor to teal"), true);
        QVERIFY(edit.contains(QLatin1String("req-3")) && edit.contains(QLatin1String("Recolor to teal")));
        QVERIFY(edit.contains(QLatin1String("selection_get")) && edit.contains(QLatin1String("proposal_finish")));
        QVERIFY(AgentLauncher::editPrompt(QStringLiteral("r"), QStringLiteral("x"), false).contains(QLatin1String("whole document")));
        // Name Layers: for what things are by default, or the user's convention word for word; one rename call.
        const QString naming = AgentLauncher::namePrompt(QStringLiteral("req-9"), QString(), true);
        QVERIFY(naming.contains(QLatin1String("req-9")) && naming.contains(QLatin1String("selection_get")) && naming.contains(QLatin1String("rename")));
        QVERIFY(naming.contains(QLatin1String("what it is")));
        const QString kebab = AgentLauncher::namePrompt(QStringLiteral("r"), QStringLiteral("kebab-case"), false);
        QVERIFY(kebab.contains(QLatin1String("kebab-case")) && kebab.contains(QLatin1String("document_get")));
        // Every task's instructions ask for real names.
        QVERIFY(AgentLauncher::instructions(QStringLiteral("/bin/omastrator")).contains(QLatin1String("never Path, Group")));

        const QString logo = AgentLauncher::smartTracePrompt(QStringLiteral("req-4"), QStringLiteral("group-id"), QStringLiteral("/tmp/scan.png"),
                                                             AgentLauncher::TraceMode::logo);
        QVERIFY(logo.contains(QLatin1String("group-id")) && logo.contains(QLatin1String("/tmp/scan.png")));
        QVERIFY(logo.contains(QLatin1String("replace_objects {\"ids\": [\"group-id\"]")));
        QVERIFY(logo.contains(QLatin1String("true circles")));
        QVERIFY(AgentLauncher::smartTracePrompt(QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("p"), AgentLauncher::TraceMode::sketch)
                    .contains(QLatin1String("centreline strokes")));

        const QString roast = AgentLauncher::roastPrompt(QStringLiteral("req-5"), QStringLiteral("/tmp/board.png"), false);
        QVERIFY(roast.contains(QLatin1String("show_roast {\"requestId\": \"req-5\"")));
        QVERIFY(roast.contains(QLatin1String("/tmp/board.png")) && roast.contains(QLatin1String("whole artboard")));
        QVERIFY(roast.contains(QLatin1String("Do not edit the document")));
    }
};

QTEST_MAIN(AgentLauncherTests)
#include "AgentLauncherTests.moc"
