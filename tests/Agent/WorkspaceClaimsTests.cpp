#include "Agent/DesignCli.h"
#include "Agent/Hyprland.h"
#include "Agent/WorkspaceClaims.h"
#include "FakeHyprctl.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

// The claims file and giving workspaces back from it (docs/WORKSPACES.md), with a fake hyprctl.
namespace {
QByteArray clients(const QList<std::tuple<QString, int, QString>> &windows)
{
    QJsonArray array;
    for (const auto &[address, pid, workspace] : windows) {
        array.append(QJsonObject{{"address", address},
                                 {"pid", pid},
                                 {"class", "x"},
                                 {"title", "t"},
                                 {"at", QJsonArray{0, 0}},
                                 {"size", QJsonArray{100, 100}},
                                 {"workspace", QJsonObject{{"id", -99}, {"name", workspace}}}});
    }
    return QJsonDocument(array).toJson();
}

QByteArray activeWorkspace(const QString &name)
{
    return QJsonDocument(QJsonObject{{"id", -99}, {"name", name}}).toJson();
}

const QString pageA = QStringLiteral("design:Poster · Front"), pageB = QStringLiteral("design:Poster · Back");
}

class WorkspaceClaimsTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    std::unique_ptr<FakeHyprctl> m_hyprctl;

    WorkspaceClaims::State sample(qint64 pid) const
    {
        WorkspaceClaims::State state;
        state.signature = QStringLiteral("sig1");
        state.pid = pid;
        state.returnWorkspace = QStringLiteral("3");
        state.claims = {{pageA, QStringLiteral("tab-1"), QStringLiteral("page-a"), {QStringLiteral("0xed1"), QStringLiteral("0x5a1")}},
                        {pageB, QStringLiteral("tab-1"), QStringLiteral("page-b"), {}}};
        return state;
    }

    // A pid that isn't running: a process that has ended.
    static qint64 deadPid()
    {
        QProcess process;
        process.start(QStringLiteral("/bin/true"));
        process.waitForFinished();
        return process.processId();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("none.sock")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        m_hyprctl = std::make_unique<FakeHyprctl>(m_directory.path());
        qputenv("OMASTRATOR_HYPRCTL", m_hyprctl->path().toUtf8());
    }

    void init()
    {
        QFile::remove(WorkspaceClaims::path());
        m_hyprctl->clearLog();
        m_hyprctl->answer(QStringLiteral("clients"), clients({}));
        m_hyprctl->answer(QStringLiteral("activeworkspace"), activeWorkspace(QStringLiteral("3")));
    }

    void theFileRoundTrips()
    {
        QVERIFY(WorkspaceClaims::read().isEmpty());
        const WorkspaceClaims::State state = sample(4242);
        QVERIFY(WorkspaceClaims::write(state).isEmpty());
        QCOMPARE(WorkspaceClaims::read(), state);
        QVERIFY(WorkspaceClaims::write({}).isEmpty());
        QVERIFY(WorkspaceClaims::read().isEmpty());
    }

    void emptyingNothingWritesNothing()
    {
        QVERIFY(WorkspaceClaims::write({}).isEmpty());
        QVERIFY(!QFile::exists(WorkspaceClaims::path()));
    }

    void givingBackMovesOnlyTheUsersWindows()
    {
        WorkspaceClaims::State state = sample(4242);
        m_hyprctl->answer(QStringLiteral("clients"),
                          clients({{QStringLiteral("0xed1"), 4242, pageA},      // the editor
                                   {QStringLiteral("0x5a1"), 4242, pageA},      // a stand-in
                                   {QStringLiteral("0x777"), 999, pageA},       // the user's, dragged in
                                   {QStringLiteral("0x778"), 999, pageB},       // the user's
                                   {QStringLiteral("0x779"), 4242, pageB},      // ours, not yet recorded: by pid
                                   {QStringLiteral("0x800"), 999, QStringLiteral("2")}}));  // elsewhere
        const WorkspaceClaims::GiveBack result = WorkspaceClaims::giveBack(state);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.moved, 2);
        QCOMPARE(result.to, QStringLiteral("3"));
        QCOMPARE(m_hyprctl->dispatches(),
                 (QStringList{QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x777"),
                              QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x778")}));
    }

    void aNumberedReturnWorkspaceIsSelectedByNumber()
    {
        // Named "1" it might be gone by now, and `name:1` would make a new named workspace.
        WorkspaceClaims::State state = sample(4242);
        state.returnId = 3;
        QVERIFY(WorkspaceClaims::write(state).isEmpty());
        QCOMPARE(WorkspaceClaims::read(), state);
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageA}}));
        WorkspaceClaims::giveBack(state);
        QCOMPARE(m_hyprctl->dispatches(), QStringList{QStringLiteral("dispatch movetoworkspacesilent 3,address:0x777")});
        m_hyprctl->clearLog();
        m_hyprctl->answer(QStringLiteral("activeworkspace"), activeWorkspace(pageB));
        WorkspaceClaims::giveBack(state);
        QVERIFY(m_hyprctl->dispatches().contains(QStringLiteral("dispatch workspace 3")));
    }

    void aFileFromBeforeReturnIdsStillLoads()
    {
        QDir().mkpath(QFileInfo(WorkspaceClaims::path()).absolutePath());
        QFile file(WorkspaceClaims::path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"signature":"sig1","pid":4242,"return":"3","claims":[{"name":"design:Poster · Front","tab":"t","page":"p","windows":["0x5a1"]}]})");
        file.close();
        const WorkspaceClaims::State state = WorkspaceClaims::read();
        QCOMPARE(state.returnWorkspace, QStringLiteral("3"));
        QCOMPARE(state.returnId, 0);
        QCOMPARE(state.claims.size(), 1);
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageA}}));
        WorkspaceClaims::giveBack(state);
        QCOMPARE(m_hyprctl->dispatches(), QStringList{QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x777")});
    }

    void standingOnAClaimedWorkspaceGoesBackToo()
    {
        m_hyprctl->answer(QStringLiteral("activeworkspace"), activeWorkspace(pageB));
        WorkspaceClaims::giveBack(sample(4242));
        QCOMPARE(m_hyprctl->dispatches(), QStringList{QStringLiteral("dispatch workspace name:3")});
    }

    void nothingClaimedDispatchesNothing()
    {
        QCOMPARE(WorkspaceClaims::giveBack({}).moved, 0);
        QVERIFY(m_hyprctl->log().isEmpty());
        QVERIFY(!WorkspaceClaims::cleanUp());
        QVERIFY(m_hyprctl->log().isEmpty());
    }

    void resetGivesBackFromTheFileWithNoAppRunning()
    {
        QVERIFY(WorkspaceClaims::write(sample(deadPid())).isEmpty());
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageA}}));
        QString out, err;
        QTextStream outStream(&out), errStream(&err);
        QCOMPARE(DesignCli::runReset(outStream, errStream), 0);
        outStream.flush();
        QVERIFY(out.contains(QStringLiteral("Moved 1 window back to workspace 3")));
        QVERIFY(m_hyprctl->dispatches().contains(QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x777")));
        QVERIFY(WorkspaceClaims::read().isEmpty());
    }

    void startupCleansUpAfterADeadApp()
    {
        QVERIFY(WorkspaceClaims::write(sample(deadPid())).isEmpty());
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageB}}));
        QVERIFY(WorkspaceClaims::cleanUp());
        QVERIFY(m_hyprctl->dispatches().contains(QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x777")));
        QVERIFY(WorkspaceClaims::read().isEmpty());
    }

    void startupLeavesALiveAppsClaims()
    {
        // The test's own pid stands for another running Omastrator.
        QVERIFY(WorkspaceClaims::write(sample(QCoreApplication::applicationPid())).isEmpty());
        QVERIFY(!WorkspaceClaims::cleanUp(1));
        QVERIFY(!WorkspaceClaims::read().isEmpty());
        QVERIFY(m_hyprctl->log().isEmpty());
        // And an app's own file is its own.
        QVERIFY(!WorkspaceClaims::cleanUp(QCoreApplication::applicationPid()));
    }

    void aReusedPidIsNotALiveApp()
    {
        // The file's pid is running, but it is a sleep now, not Omastrator.
        QProcess stranger;
        stranger.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
        QVERIFY(stranger.waitForStarted());
        QVERIFY(WorkspaceClaims::write(sample(stranger.processId())).isEmpty());
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageA}}));
        QVERIFY(WorkspaceClaims::cleanUp());
        QVERIFY(m_hyprctl->dispatches().contains(QStringLiteral("dispatch movetoworkspacesilent name:3,address:0x777")));
        QVERIFY(WorkspaceClaims::read().isEmpty());
        stranger.kill();
        stranger.waitForFinished();
    }

    void startupOnlyEmptiesAnOldHyprlandSessionsClaims()
    {
        qputenv("HYPRLAND_INSTANCE_SIGNATURE", "another-session");
        QVERIFY(WorkspaceClaims::write(sample(deadPid())).isEmpty());
        m_hyprctl->answer(QStringLiteral("clients"), clients({{QStringLiteral("0x777"), 999, pageA}}));
        QVERIFY(WorkspaceClaims::cleanUp());
        QVERIFY(m_hyprctl->log().isEmpty());
        QVERIFY(WorkspaceClaims::read().isEmpty());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }

    void whenHyprlandDoesntAnswerTheFileIsStillEmptied()
    {
        QVERIFY(WorkspaceClaims::write(sample(deadPid())).isEmpty());
        m_hyprctl->setFailing(true);
        const WorkspaceClaims::GiveBack result = WorkspaceClaims::giveBackFromFile();
        m_hyprctl->setFailing(false);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(WorkspaceClaims::read().isEmpty());
    }
};

QTEST_MAIN(WorkspaceClaimsTests)
#include "WorkspaceClaimsTests.moc"
