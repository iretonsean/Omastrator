#include "Agent/AgentClient.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Agent/Island.h"
#include "Agent/StatusStream.h"
#include "FakeAgentHost.h"
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

// Phase 0 of docs/OS-SUITE.md: select_tool, status --follow, and `omastrator island`.
namespace {
struct Backend {
    FakeAgentHost host;
    AgentTools tools{host};
    AgentServer server{tools};
};

QJsonObject line(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}
}

class IslandTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QThread m_thread;
    QObject *m_anchor = nullptr;
    Backend *m_backend = nullptr;
    QString m_path;

    template<typename Work>
    void onBackend(Work work)
    {
        QMetaObject::invokeMethod(m_anchor, work, Qt::BlockingQueuedConnection);
    }

    int island(const QStringList &args, QString *out = nullptr, QString *err = nullptr)
    {
        QString output, errors;
        QTextStream outStream(&output), errStream(&errors);
        const int code = Island::runCli(args, outStream, errStream);
        outStream.flush();
        errStream.flush();
        if (out)
            *out = output;
        if (err)
            *err = errors;
        return code;
    }

    Tool backendTool()
    {
        Tool tool = Tool::select;
        onBackend([&] { tool = m_backend->host.editor.tool(); });
        return tool;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_path = m_directory.filePath(QStringLiteral("omastrator.sock"));
        qputenv("OMASTRATOR_SOCKET", m_path.toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        // Nothing here may start a real Omastrator.
        qputenv("OMASTRATOR_APP", "/bin/true");
        m_anchor = new QObject;
        m_anchor->moveToThread(&m_thread);
        m_thread.start();
        QString failure;
        onBackend([&] {
            m_backend = new Backend;
            failure = m_backend->server.listen();
        });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void cleanupTestCase()
    {
        onBackend([&] { delete m_backend; });
        QMetaObject::invokeMethod(m_anchor, &QObject::deleteLater);
        m_thread.quit();
        m_thread.wait();
    }

    void selectToolWorksWithoutADocument()
    {
        onBackend([&] {
            QCOMPARE(m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "pen"}})["tool"].toString(), QStringLiteral("pen"));
            QCOMPARE(m_backend->host.editor.tool(), Tool::pen);
            // Aliases and case: what the island and a voice command send.
            QCOMPARE(m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "Move"}})["tool"].toString(), QStringLiteral("select"));
            QCOMPARE(m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "directselect"}})["tool"].toString(),
                     QStringLiteral("directSelect"));
            QCOMPARE(m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "type"}})["tool"].toString(), QStringLiteral("text"));
            bool refused = false;
            try {
                m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "lasso"}});
            } catch (const AgentProtocol::Error &failure) {
                refused = failure.code == AgentProtocol::invalidParams && failure.message().contains(QLatin1String("lasso"));
            }
            QVERIFY(refused);
            m_backend->tools.call(QStringLiteral("select_tool"), {{"tool", "select"}});
        });
    }

    void statusCarriesTheAppsExtras()
    {
        onBackend([&] {
            m_backend->host.extras = {{"waiting", "Waiting for Claude…"}, {"agent", "Claude"}};
            const QJsonObject status = m_backend->tools.call(QStringLiteral("status_get"), {});
            QVERIFY(status["running"].toBool());
            QVERIFY(!status["document"].toBool());
            QCOMPARE(status["tool"].toString(), QStringLiteral("select"));
            QCOMPARE(status["agent"].toString(), QStringLiteral("Claude"));
            m_backend->host.extras = {};
        });
        const QJsonObject composed = StatusStream::compose({}, Island::State());
        QVERIFY(!composed["running"].toBool());
        QCOMPARE(composed["mode"].toString(), QStringLiteral("normal"));
        for (const char *key : {"tool", "proposal", "waiting", "variations", "ready", "error", "live", "activity", "expanded"})
            QVERIFY2(composed.contains(QLatin1String(key)), key);
    }

    void followersHearEachChangeOnce()
    {
        QLocalSocket follower;
        follower.connectToServer(m_path);
        QVERIFY(follower.waitForConnected(1000));
        follower.write(AgentProtocol::frame(AgentProtocol::request(1, QStringLiteral("status_follow"), {})));
        QByteArray received;
        auto nextLine = [&]() -> QJsonObject {
            while (!received.contains('\n')) {
                if (!follower.waitForReadyRead(2000))
                    return {};
                received += follower.readAll();
            }
            const qsizetype end = received.indexOf('\n');
            const QJsonObject message = line(received.left(end));
            received.remove(0, end + 1);
            return message;
        };
        QCOMPARE(nextLine()["result"].toObject()["tool"].toString(), QStringLiteral("select"));

        // Another client changes the tool: the follower hears it as a notification.
        AgentClient::Connection other;
        other.call(QStringLiteral("select_tool"), {{"tool", "rectangle"}});
        const QJsonObject changed = nextLine();
        QCOMPARE(changed["method"].toString(), QStringLiteral("status"));
        QCOMPARE(changed["params"].toObject()["tool"].toString(), QStringLiteral("rectangle"));

        // A read changes nothing, so nothing is sent.
        other.call(QStringLiteral("status_get"), {});
        QVERIFY(!follower.waitForReadyRead(200));

        // The app's own changes go out once they settle.
        onBackend([&] {
            m_backend->host.editor.selectTool(Tool::ellipse);
            m_backend->host.editor.selectTool(Tool::star);
            m_backend->server.statusMayHaveChanged();
        });
        QCOMPARE(nextLine()["params"].toObject()["tool"].toString(), QStringLiteral("star"));
        QVERIFY(!follower.waitForReadyRead(200));
        other.call(QStringLiteral("select_tool"), {{"tool", "select"}});
        QCOMPARE(nextLine()["params"].toObject()["tool"].toString(), QStringLiteral("select"));
    }

    void islandCliKeepsTheMode()
    {
        QString out, err;
        QCOMPARE(island({QStringLiteral("state")}, &out), 0);
        QCOMPARE(line(out.toUtf8())["mode"].toString(), QStringLiteral("normal"));
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("capture")}), 0);
        Island::State state = Island::read();
        QCOMPARE(state.mode, QStringLiteral("capture"));
        QVERIFY(state.expanded);
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("next")}), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("ai"));
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("next")}), 0);
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("next")}), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QVERIFY(!Island::read().expanded);
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("previous")}), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("live"));
        QCOMPARE(island({QStringLiteral("rest")}), 0);
        QVERIFY(!Island::read().expanded);
        QCOMPARE(island({QStringLiteral("toggle")}), 0);
        QVERIFY(Island::read().expanded);

        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("sideways")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("normal, draw")));
        QCOMPARE(island({QStringLiteral("launch")}, &out, &err), 1);
        QCOMPARE(island({}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("Usage")));

        QCOMPARE(island({QStringLiteral("activity"), QStringLiteral("Saved"), QStringLiteral("--seconds"), QStringLiteral("5")}), 0);
        state = Island::read();
        QCOMPARE(state.activity, QStringLiteral("Saved"));
        QCOMPARE(state.activitySeconds, 5);
        QVERIFY(state.activityId > 0);
        QCOMPARE(state.mode, QStringLiteral("live"));

        // First-use labels are remembered across sessions, apart from the mode.
        QCOMPARE(island({QStringLiteral("seen"), QStringLiteral("draw")}), 0);
        QVERIFY(Island::read().seen.contains(QStringLiteral("draw")));
        QFile::remove(Island::statePath());
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QVERIFY(Island::read().seen.contains(QStringLiteral("draw")));
        QVERIFY(Island::statePath().startsWith(m_directory.path()));
        QVERIFY(Island::seenPath().startsWith(m_directory.path()));
    }

    void islandToolRoundTripsThroughTheSocket()
    {
        QString out, err;
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("normal")}), 0);
        QCOMPARE(island({QStringLiteral("tool"), QStringLiteral("pen")}, &out, &err), 0);
        QCOMPARE(out.trimmed(), QStringLiteral("pen"));
        QCOMPARE(backendTool(), Tool::pen);
        // Choosing a tool is Draw mode.
        QCOMPARE(Island::read().mode, QStringLiteral("draw"));
        QCOMPARE(island({QStringLiteral("tool"), QStringLiteral("blob")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("blob")));
        QCOMPARE(island({QStringLiteral("tool"), QStringLiteral("select")}), 0);
    }

    void drawStartsTheAppOnlyWhenItIsClosed()
    {
        QVERIFY(Island::appIsRunning());
        QVERIFY(Island::ensureAppRunning(500).isEmpty());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("closed.sock")).toUtf8());
        QVERIFY(!Island::appIsRunning());
        // The stand-in app never listens, so this reports it plainly.
        QVERIFY(Island::ensureAppRunning(300).contains(QLatin1String("did not start")));
        qputenv("OMASTRATOR_APP", "/nonexistent/omastrator");
        QVERIFY(Island::ensureAppRunning(300).contains(QLatin1String("Could not start")));
        qputenv("OMASTRATOR_APP", "/bin/true");
        qputenv("OMASTRATOR_SOCKET", m_path.toUtf8());
    }

    void followPrintsALinePerChange()
    {
        QString text;
        QTextStream out(&text);
        StatusStream::Follower follower(out);
        QSignalSpy printed(&follower, &StatusStream::Follower::printed);
        follower.start();
        // The first line may come before the app answers; wait for the app's.
        QTRY_VERIFY(follower.last()["running"].toBool());

        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("capture")}), 0);
        QTRY_COMPARE(follower.last()["mode"].toString(), QStringLiteral("capture"));

        AgentClient::Connection other;
        other.call(QStringLiteral("select_tool"), {{"tool", "zoom"}});
        QTRY_COMPARE(follower.last()["tool"].toString(), QStringLiteral("zoom"));
        const qsizetype lines = printed.size();
        // No change, no line.
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("capture")}), 0);
        QTest::qWait(200);
        QCOMPARE(printed.size(), lines);
        QCOMPARE(text.count(QLatin1Char('\n')), lines);

        // The app closing is a change too, and its coming back.
        onBackend([&] { m_backend->server.close(); });
        QTRY_VERIFY(!follower.last()["running"].toBool());
        QCOMPARE(follower.last()["mode"].toString(), QStringLiteral("capture"));
        QString failure;
        onBackend([&] { failure = m_backend->server.listen(m_path); });
        QVERIFY(failure.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(follower.last()["running"].toBool(), 5000);
        other.call(QStringLiteral("select_tool"), {{"tool", "select"}});
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("normal")}), 0);
    }
};

QTEST_GUILESS_MAIN(IslandTests)
#include "IslandTests.moc"
