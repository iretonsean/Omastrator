#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

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

class AgentLauncherTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString out(const QString &name) const { return m_directory.filePath(QStringLiteral("out/") + name); }

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
    }

    void init()
    {
        qunsetenv("FAKE_FAIL");
        qputenv("FAKE_AGENT", "sh");
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

    void launchPassesThePromptWithoutAShell()
    {
        QCOMPARE(AgentLauncher::defaultAgent(), QStringLiteral("sh"));
        const QString prompt = QStringLiteral("Make it \"pop\"; $(rm -rf ~) `true` 'quoted'\nSecond line");
        QCOMPARE(AgentLauncher::launch(prompt), QString());
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
        QVERIFY(agents.contains(QLatin1String("Never at who someone is")));
        QVERIFY(agents.contains(QLatin1String("proposal_finish")));
        const QJsonObject mcp = QJsonDocument::fromJson(read(QDir(folder).filePath(QStringLiteral(".mcp.json"))).toUtf8()).object();
        const QJsonObject server = mcp["mcpServers"].toObject()["omastrator"].toObject();
        QCOMPARE(server["args"].toArray(), QJsonArray{"--mcp"});
        QCOMPARE(server["env"].toObject()["OMASTRATOR_SOCKET"].toString(), QStringLiteral("/run/user/test/omastrator.sock"));
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
