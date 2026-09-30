#include "Agent/AgentClient.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Agent/DesignCli.h"
#include "Agent/Island.h"
#include "Agent/StatusStream.h"
#include "FakeAgentHost.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
    QString m_hyprctl;

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
        // Nothing here may start a real Omastrator, or reach the real Hyprland.
        qputenv("OMASTRATOR_APP", "/bin/true");
        // Nothing here may pop a notification on the real desktop.
        qputenv("OMASTRATOR_NOTIFY", "/bin/true");
        m_hyprctl = m_directory.filePath(QStringLiteral("hyprctl"));
        QFile hyprctl(m_hyprctl);
        QVERIFY(hyprctl.open(QIODevice::WriteOnly));
        hyprctl.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$0.log\"\n");
        hyprctl.close();
        hyprctl.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_HYPRCTL", m_hyprctl.toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
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
        for (const char *key : {"tool", "proposal", "waiting", "variations", "ready", "error", "live", "activity"})
            QVERIFY2(composed.contains(QLatin1String(key)), key);
        // What only the pill read is gone.
        for (const char *key : {"expanded", "labelsSeen", "islandShow"})
            QVERIFY2(!composed.contains(QLatin1String(key)), key);
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

    // The pill is gone, so is its "show it everywhere" choice.
    void theVisibilityChoiceIsGone()
    {
        QString out, err;
        for (const char *choice : {"always", "with-app"}) {
            QCOMPARE(island({QStringLiteral("show"), QLatin1String(choice)}, &out, &err), 1);
            QCOMPARE(err.trimmed().count(QLatin1Char('\n')), 0);
            QVERIFY(err.contains(QLatin1String("pill is gone")));
        }
        QVERIFY(!QFileInfo::exists(QDir(m_directory.filePath(QStringLiteral("config"))).filePath(QStringLiteral("omastrator/island-visibility.json"))));
    }

    void islandCliKeepsTheMode()
    {
        QString out, err;
        QCOMPARE(island({QStringLiteral("state")}, &out), 0);
        QCOMPARE(line(out.toUtf8())["mode"].toString(), QStringLiteral("normal"));
        QCOMPARE(Island::modes(), (QStringList{QStringLiteral("normal"), QStringLiteral("design")}));
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("design")}), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("design"));
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("normal")}), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        // The state has only what still means something.
        QCOMPARE(line(QJsonDocument(Island::read().toJson()).toJson()).keys(),
                 (QStringList{QStringLiteral("activity"), QStringLiteral("activityId"), QStringLiteral("activitySeconds"), QStringLiteral("mode")}));
        // A mode left in island.json by an older version reads as Normal.
        QFile old(Island::statePath());
        QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
        old.write("{\"mode\":\"draw\",\"expanded\":true}\n");
        old.close();
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));

        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("sideways")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("normal, design")));
        QCOMPARE(island({QStringLiteral("launch")}, &out, &err), 1);
        QCOMPARE(island({}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("Usage")));

        QCOMPARE(island({QStringLiteral("activity"), QStringLiteral("Saved"), QStringLiteral("--seconds"), QStringLiteral("5")}), 0);
        const Island::State state = Island::read();
        QCOMPARE(state.activity, QStringLiteral("Saved"));
        QCOMPARE(state.activitySeconds, 5);
        QVERIFY(state.activityId > 0);
        QVERIFY(Island::statePath().startsWith(m_directory.path()));
    }

    // Each removed verb fails with one line that says where its job went.
    void removedVerbsSayWhereTheyWent()
    {
        QString out, err;
        struct Removed {
            QStringList args;
            const char *says;
        };
        const Removed gone[] = {
            {{"mode", "draw"}, "Draw mode is gone: use Omastrator's toolbar."},
            {{"mode", "capture"}, "Capture mode is gone: use the Capture tab in Omastrator, or `omastrator island capture …`."},
            {{"mode", "ai"}, "AI mode is gone: use the Ask field in Omastrator (`omastrator island ask` focuses it), or `omastrator island ai …`."},
            {{"mode", "live"}, "Live mode is gone: use a Browser View in Omastrator, or `omastrator island live …`."},
            {{"mode", "next"}, "Modes no longer step: choose `omastrator island mode normal` or `mode design`."},
            {{"mode", "previous"}, "Modes no longer step: choose `omastrator island mode normal` or `mode design`."},
            {{"tool", "pen"}, "Draw mode is gone: use Omastrator's toolbar."},
            {{"expand"}, "The island pill is gone, so there is nothing to expand."},
            {{"rest"}, "The island pill is gone, so there is nothing to rest."},
            {{"toggle"}, "The island pill is gone, so there is nothing to expand or rest."},
            {{"seen", "draw"}, "The island pill is gone, so it shows no first-use labels."},
            {{"show", "always"}, "The island pill is gone, so it has no place to show: notifications and Omastrator's window replace it."},
            {{"show", "with-app"}, "The island pill is gone, so it has no place to show: notifications and Omastrator's window replace it."},
        };
        QVERIFY(island({QStringLiteral("mode"), QStringLiteral("normal")}) == 0);
        const QString before = QString::fromUtf8(QJsonDocument(Island::read().toJson()).toJson());
        onBackend([&] { m_backend->host.editor.selectTool(Tool::select); });
        for (const Removed &each : gone) {
            QCOMPARE(island(each.args, &out, &err), 1);
            QCOMPARE(err.trimmed(), QString::fromUtf8(each.says));
            QVERIFY2(out.isEmpty(), qPrintable(each.args.join(QLatin1Char(' '))));
        }
        // None of them changed the mode or reached the app (`tool pen` used to).
        QCOMPARE(QString::fromUtf8(QJsonDocument(Island::read().toJson()).toJson()), before);
        QCOMPARE(backendTool(), Tool::select);
        // What still works still says so in the help.
        QCOMPARE(island({QStringLiteral("--help")}, &out), 0);
        for (const char *word : {"mode <normal|design>", "ask ", "capture color", "dictate start", "live start", "new "})
            QVERIFY2(out.contains(QLatin1String(word)), word);
        for (const char *word : {"expand", "seen <mode>", "always|with-app", "tool <name>", "draw|capture"})
            QVERIFY2(!out.contains(QLatin1String(word)), word);
    }

    // `island ask` starts the app if needed, then show_window with raise and focus = ask (the tray light's click).
    void askBringsTheWindowForwardWithAskFocused()
    {
        onBackend([&] {
            m_backend->host.windowShown = 0;
            m_backend->host.shownFocus.clear();
            m_backend->host.shownRaise = false;
        });
        QString out, err;
        QCOMPARE(island({QStringLiteral("ask")}, &out, &err), 0);
        onBackend([&] {
            QCOMPARE(m_backend->host.windowShown, 1);
            QVERIFY(m_backend->host.shownRaise);
            QCOMPARE(m_backend->host.shownFocus, QStringLiteral("ask"));
            QVERIFY(m_backend->host.shownFiles.isEmpty());
        });
        // The method itself: focus is optional, and only "ask" exists.
        onBackend([&] {
            m_backend->tools.call(QStringLiteral("show_window"), {{"files", QJsonArray()}});
            QVERIFY(m_backend->host.shownFocus.isEmpty());
            bool refused = false;
            try {
                m_backend->tools.call(QStringLiteral("show_window"), {{"focus", "layers"}});
            } catch (const AgentProtocol::Error &failure) {
                refused = failure.code == AgentProtocol::invalidParams;
            }
            QVERIFY(refused);
        });
        // A refusal from the app is the command's failure.
        onBackend([&] { m_backend->host.failure = QStringLiteral("Omastrator is showing a dialog. Try again when it's answered."); });
        QCOMPARE(island({QStringLiteral("ask")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("showing a dialog")));
        onBackend([&] { m_backend->host.failure.clear(); });
        // With no app to start, it says so.
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("closed.sock")).toUtf8());
        QCOMPARE(island({QStringLiteral("ask")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("did not start")) || err.contains(QLatin1String("Could not start")));
        qputenv("OMASTRATOR_SOCKET", m_path.toUtf8());
        // The tray light's tooltip and click are the shell plugin's; here only the verb exists in the help.
        QCOMPARE(island({QStringLiteral("--help")}, &out), 0);
        QVERIFY(out.contains(QLatin1String("ask ")));
    }

    // Messages from commands started outside the app reach the desktop through notify-send (OMASTRATOR_NOTIFY in tests).
    void activityIsAlsoANotification()
    {
        const QString log = m_directory.filePath(QStringLiteral("notify.log"));
        const QString script = m_directory.filePath(QStringLiteral("notify"));
        QFile::remove(log);
        QFile program(script);
        QVERIFY(program.open(QIODevice::WriteOnly));
        // One line per notification, written at once so a reader never sees half of one; a newline inside an argument shows as "|".
        program.write("#!/bin/sh\nline=$(for word in \"$@\"; do printf '[%s]' \"$(printf %s \"$word\" | tr '\\n' '|')\"; done)\nprintf '%s\\n' \"$line\" >> \""
                      + log.toUtf8() + "\"\n");
        program.close();
        program.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_NOTIFY", script.toUtf8());
        auto lines = [&] {
            QFile file(log);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
        };
        const QString sameCard = QStringLiteral("[-h][string:x-canonical-private-synchronous:omastrator]");

        QCOMPARE(island({QStringLiteral("activity"), QStringLiteral("Saved"), QStringLiteral("--seconds"), QStringLiteral("5")}), 0);
        QTRY_COMPARE(lines().size(), 1);
        QCOMPARE(lines().at(0), QStringLiteral("[-a][Omastrator][-t][5000]") + sameCard + QStringLiteral("[Saved]"));

        // The first line is the summary, the rest the body.
        QCOMPARE(Island::setActivity(QStringLiteral("Live couldn't start\nThe port is busy.\nTry another."), 6), QString());
        QTRY_COMPARE(lines().size(), 2);
        QCOMPARE(lines().at(1), QStringLiteral("[-a][Omastrator][-t][6000]") + sameCard + QStringLiteral("[Live couldn't start][The port is busy.|Try another.]"));
        // The line is still in the state file for anything that follows the status stream.
        QCOMPARE(Island::read().activity, QStringLiteral("Live couldn't start\nThe port is busy.\nTry another."));

        // An empty line clears the state and says nothing.
        QCOMPARE(Island::setActivity(QString(), 3), QString());
        QTest::qWait(300);
        QCOMPARE(lines().size(), 2);

        // A command that fails outside the app says why the same way (the app's refusal here).
        onBackend([&] { m_backend->host.failure = QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent."); });
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("roast")}), 1);
        QTRY_COMPARE(lines().size(), 3);
        QVERIFY(lines().at(2).endsWith(QStringLiteral("[Choose an agent in Omarchy → Setup → Default → Agent.]")));
        onBackend([&] { m_backend->host.failure.clear(); });

        // A missing program is no failure of the command.
        qputenv("OMASTRATOR_NOTIFY", "/nonexistent/notify-send");
        QCOMPARE(Island::setActivity(QStringLiteral("Nobody hears this")), QString());
        qputenv("OMASTRATOR_NOTIFY", "/bin/true");
    }

    // Design mode everywhere (docs/ANYWHERE.md): the hotkey's `design on`, the overlay's clicks, and Esc.
    void designModeIsTheModeFileAndTheAppsMethod()
    {
        QString output, errors;
        QTextStream out(&output), err(&errors);
        auto calls = [&] {
            std::vector<std::pair<QString, QJsonObject>> seen;
            onBackend([&] { seen = m_backend->host.designCalls; });
            return seen;
        };
        onBackend([&] { m_backend->host.designCalls.clear(); });
        QFile::remove(m_hyprctl + QStringLiteral(".log"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("on")}, out, err), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("design"));
        QCOMPARE(calls().back().first, QStringLiteral("on"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("tool"), QStringLiteral("rectangle")}, out, err), 0);
        QCOMPARE(calls().back().second["tool"].toString(), QStringLiteral("rectangle"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("draw"), QStringLiteral("arrow"), QStringLiteral("10,20"), QStringLiteral("300,40.5")}, out, err), 0);
        QCOMPARE(calls().back().second["points"].toArray(), (QJsonArray{QJsonArray{10, 20}, QJsonArray{300, 40.5}}));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("draw"), QStringLiteral("note"), QStringLiteral("5,5"), QStringLiteral("--text"), QStringLiteral("Too tight")}, out, err), 0);
        QCOMPARE(calls().back().second["text"].toString(), QStringLiteral("Too tight"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("draw"), QStringLiteral("pen"), QStringLiteral("5;5")}, out, err), 1);
        QCOMPARE(DesignCli::runDesign({QStringLiteral("ask"), QStringLiteral("--target"), QStringLiteral("7"), QStringLiteral("tighten"), QStringLiteral("this")}, out, err), 0);
        QCOMPARE(calls().back().second["prompt"].toString(), QStringLiteral("tighten this"));
        QCOMPARE(calls().back().second["target"].toInt(), 7);
        QCOMPARE(DesignCli::runDesign({QStringLiteral("send"), QStringLiteral("desk"), QStringLiteral("--surface"), QStringLiteral("window:foot")}, out, err), 0);
        QCOMPARE(calls().back().second["destination"].toString(), QStringLiteral("desk"));
        QCOMPARE(calls().back().second["surface"].toString(), QStringLiteral("window:foot"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("onboarding"), QStringLiteral("makes"), QStringLiteral("web"), QStringLiteral("rice")}, out, err), 0);
        QCOMPARE(calls().back().second["values"].toArray(), (QJsonArray{"web", "rice"}));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("onboarding"), QStringLiteral("skip")}, out, err), 0);
        QCOMPARE(calls().back().second["finish"].toBool(true), false);
        QCOMPARE(DesignCli::runDesk({QStringLiteral("window")}, out, err), 0);
        QCOMPARE(calls().back().first, QStringLiteral("desk"));
        QCOMPARE(calls().back().second["how"].toString(), QStringLiteral("window"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("sideways")}, out, err), 1);
        // Esc: the mode goes back to Normal and Hyprland gives the keys back.
        QCOMPARE(DesignCli::runDesign({QStringLiteral("off")}, out, err), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QFile log(m_hyprctl + QStringLiteral(".log"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        // Without setup's keys no submap is ever held, only given back: the last word is a reset.
        const QStringList dispatched = QString::fromUtf8(log.readAll()).trimmed().split(QLatin1Char('\n'));
        QVERIFY(!dispatched.isEmpty());
        for (const QString &line : dispatched)
            QCOMPARE(line, QStringLiteral("dispatch submap reset"));
        // Toggle turns it on and off again.
        QCOMPARE(DesignCli::runDesign({QStringLiteral("toggle")}, out, err), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("design"));
        QCOMPARE(DesignCli::runDesign({QStringLiteral("toggle")}, out, err), 0);
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        // The composed status always has a design key, so the overlay never reads undefined.
        QVERIFY(StatusStream::compose({}, Island::read())["design"].toObject().contains("on"));
    }

    void islandAiStartsFlowsInTheApp()
    {
        QString out, err;
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("generate"), QStringLiteral("--prompt"), QStringLiteral("a fox"),
                         QStringLiteral("--count"), QStringLiteral("4"), QStringLiteral("--fit")}, &out, &err), 0);
        onBackend([&] {
            const auto &request = m_backend->host.lastAi;
            QCOMPARE(request.flow, QStringLiteral("generate"));
            QCOMPARE(request.prompt, QStringLiteral("a fox"));
            QCOMPARE(request.count, 4);
            QVERIFY(request.fitToSelection);
        });
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("vectorize"), QStringLiteral("--mode"), QStringLiteral("sketch")}), 0);
        onBackend([&] { QVERIFY(m_backend->host.lastAi.sketch); });
        // A sheet opening is said as a notification, since the window may be elsewhere.
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("edit")}), 0);
        QCOMPARE(Island::read().activity, QStringLiteral("Edit with Instruction is open in Omastrator"));
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("fly")}, &out, &err), 1);
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("generate"), QStringLiteral("--count")}, &out, &err), 1);
        // The app's refusal reaches the desktop as its plain reason.
        onBackend([&] { m_backend->host.failure = QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent."); });
        QCOMPARE(island({QStringLiteral("ai"), QStringLiteral("roast")}, &out, &err), 1);
        QCOMPARE(Island::read().activity, QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent."));
        onBackend([&] { m_backend->host.failure.clear(); });
    }

    void islandLiveDeploysAndShowsHistory()
    {
        QString out, err;
        QCOMPARE(island({"live", "deploy"}), 0);
        onBackend([&] {
            QCOMPARE(m_backend->host.liveAction, QStringLiteral("deploy"));
            QVERIFY(!m_backend->host.liveParams["confirm"].toBool());
            QVERIFY(!m_backend->host.liveParams.contains("github"));
        });
        QCOMPARE(island({"live", "deploy", "--confirm", "--remember", "--github", "my-site", "--folder", "/tmp/site"}), 0);
        onBackend([&] {
            const QJsonObject &params = m_backend->host.liveParams;
            QVERIFY(params["confirm"].toBool() && params["remember"].toBool());
            QCOMPARE(params["github"].toString(), QStringLiteral("my-site"));
            QCOMPARE(params["folder"].toString(), QStringLiteral("/tmp/site"));
        });
        QCOMPARE(island({"live", "save", "--no-github"}), 0);
        onBackend([&] {
            QCOMPARE(m_backend->host.liveAction, QStringLiteral("save"));
            QVERIFY(m_backend->host.liveParams.contains("github") && m_backend->host.liveParams["github"].toString().isEmpty());
        });
        QCOMPARE(island({"live", "deploy", "--github"}, &out, &err), 1);
        QCOMPARE(island({"live", "deploy", "--prod"}, &out, &err), 1);
        // The first deploy asks in the app; the notification says where.
        onBackend([&] { m_backend->host.liveResult = {{"sheet", true}}; });
        QCOMPARE(island({"live", "deploy"}), 0);
        QCOMPARE(Island::read().activity, QStringLiteral("Confirm the deploy in Omastrator"));
        onBackend([&] {
            m_backend->host.liveResult = {{"commits", QJsonArray{QJsonObject{{"sha", "0123456789abcdef"}, {"subject", "Bigger buttons"}, {"time", "2026-09-26T10:00:00"},
                                                                            {"deployed", true}, {"url", "https://site.example.test"}}}}};
        });
        QCOMPARE(island({"live", "history", "--list"}, &out), 0);
        QVERIFY(out.contains(QLatin1String("0123456  2026-09-26T10:00:00  Bigger buttons  [deployed https://site.example.test]")));
        onBackend([&] {
            QCOMPARE(m_backend->host.liveAction, QStringLiteral("history"));
            QVERIFY(m_backend->host.liveParams["list"].toBool());
            m_backend->host.liveResult = {};
        });
        QCOMPARE(island({"live", "restore"}, &out, &err), 1);
        QCOMPARE(island({"live", "restore", "abc1234"}), 0);
        onBackend([&] { QCOMPARE(m_backend->host.liveParams["id"].toString(), QStringLiteral("abc1234")); });
        QCOMPARE(island({"live", "changes"}), 0);
        onBackend([&] { QCOMPARE(m_backend->host.liveAction, QStringLiteral("review")); });
        for (const char *action : {"details", "remember", "cancel"}) {
            QCOMPARE(island({"live", action}), 0);
            onBackend([&] { QCOMPARE(m_backend->host.liveAction, QLatin1String(action)); });
        }
        QCOMPARE(island({"live", "github", "connect"}, &out), 0);
        onBackend([&] { QVERIFY(m_backend->host.liveParams["connect"].toBool()); });
        QCOMPARE(island({"live", "publish"}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("deploy")));
        // A refusal is said as a notification.
        onBackend([&] { m_backend->host.failure = QStringLiteral("A deploy is already running. Wait for it, or cancel it."); });
        QCOMPARE(island({"live", "deploy"}, &out, &err), 1);
        QCOMPARE(Island::read().activity, QStringLiteral("A deploy is already running. Wait for it, or cancel it."));
        onBackend([&] { m_backend->host.failure.clear(); });
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

        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("design")}), 0);
        QTRY_COMPARE(follower.last()["mode"].toString(), QStringLiteral("design"));

        AgentClient::Connection other;
        other.call(QStringLiteral("select_tool"), {{"tool", "zoom"}});
        QTRY_COMPARE(follower.last()["tool"].toString(), QStringLiteral("zoom"));
        const qsizetype lines = printed.size();
        // No change, no line.
        QCOMPARE(island({QStringLiteral("mode"), QStringLiteral("design")}), 0);
        QTest::qWait(200);
        QCOMPARE(printed.size(), lines);
        QCOMPARE(text.count(QLatin1Char('\n')), lines);

        // The app closing is a change too, and its coming back.
        onBackend([&] { m_backend->server.close(); });
        QTRY_VERIFY(!follower.last()["running"].toBool());
        QCOMPARE(follower.last()["mode"].toString(), QStringLiteral("design"));
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
