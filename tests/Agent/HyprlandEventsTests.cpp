#include "Agent/Hyprland.h"
#include "Agent/HyprlandEvents.h"
#include "FakeHyprctl.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Hyprland's window and workspace dispatchers and its event stream (docs/WORKSPACES.md), against fakes.
Q_DECLARE_METATYPE(HyprlandEvents::Event)

class HyprlandEventsTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    static HyprlandEvents::Event workspace(int id, const QString &name)
    {
        HyprlandEvents::Event event;
        event.kind = HyprlandEvents::Event::Kind::workspace;
        event.workspaceId = id;
        event.workspaceName = name;
        return event;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<HyprlandEvents::Event>();
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qunsetenv("OMASTRATOR_HYPRLAND_EVENTS");
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
    }

    void parsesEveryEventWePageOn()
    {
        using Kind = HyprlandEvents::Event::Kind;
        auto event = HyprlandEvents::parse(QStringLiteral("workspacev2>>-99,design:Poster · Front"));
        QVERIFY(event);
        QCOMPARE(event->kind, Kind::workspace);
        QCOMPARE(event->workspaceId, -99);
        QCOMPARE(event->workspaceName, QStringLiteral("design:Poster · Front"));

        event = HyprlandEvents::parse(QStringLiteral("destroyworkspacev2>>4,4"));
        QVERIFY(event);
        QCOMPARE(event->kind, Kind::destroyWorkspace);
        QCOMPARE(event->workspaceId, 4);

        event = HyprlandEvents::parse(QStringLiteral("openwindow>>5588a1,design:Poster · Front,io.github.iretonsean.Omastrator,omastrator-standin-3"));
        QVERIFY(event);
        QCOMPARE(event->kind, Kind::openWindow);
        QCOMPARE(event->address, QStringLiteral("0x5588a1"));
        QCOMPARE(event->workspaceName, QStringLiteral("design:Poster · Front"));
        QCOMPARE(event->title, QStringLiteral("omastrator-standin-3"));

        event = HyprlandEvents::parse(QStringLiteral("closewindow>>5588a1"));
        QVERIFY(event);
        QCOMPARE(event->kind, Kind::closeWindow);
        QCOMPARE(event->address, QStringLiteral("0x5588a1"));

        event = HyprlandEvents::parse(QStringLiteral("movewindowv2>>5588a1,-98,design:Poster · Back"));
        QVERIFY(event);
        QCOMPARE(event->kind, Kind::moveWindow);
        QCOMPARE(event->workspaceId, -98);
        QCOMPARE(event->workspaceName, QStringLiteral("design:Poster · Back"));
    }

    void namesMayHoldCommasAndArrows()
    {
        auto event = HyprlandEvents::parse(QStringLiteral("workspacev2>>-99,a,b>>c, ünï · 日本"));
        QVERIFY(event);
        QCOMPARE(event->workspaceId, -99);
        QCOMPARE(event->workspaceName, QStringLiteral("a,b>>c, ünï · 日本"));

        event = HyprlandEvents::parse(QStringLiteral("movewindowv2>>abc,-99,x,y>>z"));
        QVERIFY(event);
        QCOMPARE(event->workspaceName, QStringLiteral("x,y>>z"));

        // A window's title is last, so its commas and arrows stay.
        event = HyprlandEvents::parse(QStringLiteral("openwindow>>abc,1,class,Title, with >> stuff"));
        QVERIFY(event);
        QCOMPARE(event->title, QStringLiteral("Title, with >> stuff"));
    }

    void ignoresWhatItDoesNotUse()
    {
        QVERIFY(!HyprlandEvents::parse(QStringLiteral("activewindow>>kitty,shell")));
        QVERIFY(!HyprlandEvents::parse(QStringLiteral("workspace>>1")));
        QVERIFY(!HyprlandEvents::parse(QStringLiteral("workspacev2>>x,y")));
        QVERIFY(!HyprlandEvents::parse(QStringLiteral("movewindowv2>>abc,notanumber,x")));
        QVERIFY(!HyprlandEvents::parse(QStringLiteral("no arrows here")));
        QVERIFY(!HyprlandEvents::parse(QString()));
    }

    void partialLinesJoinAcrossReads()
    {
        HyprlandEvents events;
        QSignalSpy spy(&events, &HyprlandEvents::event);
        events.feed("workspacev2>>-99,design:Po");
        QCOMPARE(spy.count(), 0);
        events.feed("ster · Front\nactivewindow>>x,y\nworkspacev2>>3,3\nclosewin");
        QCOMPARE(spy.count(), 2);
        events.feed("dow>>ff\n");
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(0).value<HyprlandEvents::Event>(), workspace(-99, QStringLiteral("design:Poster · Front")));
        QCOMPARE(spy.at(1).at(0).value<HyprlandEvents::Event>(), workspace(3, QStringLiteral("3")));
    }

    void readsTheSocketAndSeesItClose()
    {
        const QString path = m_directory.filePath(QStringLiteral("events.sock"));
        QLocalServer server;
        QVERIFY(server.listen(path));
        qputenv("OMASTRATOR_HYPRLAND_EVENTS", path.toUtf8());
        HyprlandEvents events;
        QSignalSpy spy(&events, &HyprlandEvents::event), closed(&events, &HyprlandEvents::closed);
        QVERIFY2(events.start().isEmpty(), "connects");
        QVERIFY(server.waitForNewConnection(1000));
        QLocalSocket *peer = server.nextPendingConnection();
        peer->write("workspacev2>>-99,design:A · B\n");
        peer->flush();
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<HyprlandEvents::Event>(), workspace(-99, QStringLiteral("design:A · B")));
        peer->disconnectFromServer();
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!events.isRunning());
        qunsetenv("OMASTRATOR_HYPRLAND_EVENTS");
    }

    void stopIsNotAClose()
    {
        const QString path = m_directory.filePath(QStringLiteral("events2.sock"));
        QLocalServer server;
        QVERIFY(server.listen(path));
        qputenv("OMASTRATOR_HYPRLAND_EVENTS", path.toUtf8());
        HyprlandEvents events;
        QSignalSpy closed(&events, &HyprlandEvents::closed);
        QVERIFY(events.start().isEmpty());
        events.stop();
        QTest::qWait(50);
        QCOMPARE(closed.count(), 0);
        qunsetenv("OMASTRATOR_HYPRLAND_EVENTS");
    }

    void withoutHyprlandItFailsPlainly()
    {
        HyprlandEvents events;
        QVERIFY(!events.start().isEmpty());
        qputenv("OMASTRATOR_HYPRLAND_EVENTS", m_directory.filePath(QStringLiteral("nothing.sock")).toUtf8());
        QVERIFY(!events.start().isEmpty());
        qunsetenv("OMASTRATOR_HYPRLAND_EVENTS");
    }

    void dispatchersBuildLuaAndHyprlang()
    {
        FakeHyprctl hyprctl(m_directory.path());
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        const QString name = Hyprland::workspaceSelector(-1337, QStringLiteral("design:Poster · Front"));

        // hyprlang: no hyprland.lua.
        QVERIFY(Hyprland::moveWindow(QStringLiteral("5588a1"), name, false).isEmpty());
        QVERIFY(Hyprland::moveWindow(QStringLiteral("0x5588a1"), name, true).isEmpty());
        QVERIFY(Hyprland::focusWorkspace(name).isEmpty());
        QVERIFY(Hyprland::focusWindow(QStringLiteral("0x5588a1")).isEmpty());
        QCOMPARE(hyprctl.log(),
                 (QStringList{QStringLiteral("dispatch movetoworkspacesilent name:design:Poster · Front,address:0x5588a1"),
                              QStringLiteral("dispatch movetoworkspace name:design:Poster · Front,address:0x5588a1"),
                              QStringLiteral("dispatch workspace name:design:Poster · Front"),
                              QStringLiteral("dispatch focuswindow address:0x5588a1")}));

        // Lua: a hyprland.lua exists.
        QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("config/hypr"))));
        QFile lua(m_directory.filePath(QStringLiteral("config/hypr/hyprland.lua")));
        QVERIFY(lua.open(QIODevice::WriteOnly));
        lua.close();
        hyprctl.clearLog();
        Hyprland::moveWindow(QStringLiteral("5588a1"), name, false);
        Hyprland::moveWindow(QStringLiteral("5588a1"), Hyprland::workspaceSelector(0, QStringLiteral("we\"ird\\")), true);
        Hyprland::focusWorkspace(name);
        Hyprland::focusWindow(QStringLiteral("5588a1"));
        QCOMPARE(hyprctl.log(),
                 (QStringList{QStringLiteral("eval hl.dispatch(hl.dsp.window.move({ workspace = \"name:design:Poster · Front\", follow = false, window = \"address:0x5588a1\" }))"),
                              QStringLiteral("eval hl.dispatch(hl.dsp.window.move({ workspace = \"name:we\\\"ird\\\\\", follow = true, window = \"address:0x5588a1\" }))"),
                              QStringLiteral("eval hl.dispatch(hl.dsp.focus({ workspace = \"name:design:Poster · Front\" }))"),
                              QStringLiteral("eval hl.dispatch(hl.dsp.focus({ window = \"address:0x5588a1\" }))")}));
        QFile::remove(lua.fileName());
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    void aWorkspaceIsSelectedByNumberWhenItHasOne()
    {
        // A bare name would make a new named workspace, even for "1".
        QCOMPARE(Hyprland::workspaceSelector(1, QStringLiteral("1")), QStringLiteral("1"));
        QCOMPARE(Hyprland::workspaceSelector(7, QStringLiteral("seven")), QStringLiteral("7"));
        QCOMPARE(Hyprland::workspaceSelector(-1337, QStringLiteral("design:A")), QStringLiteral("name:design:A"));
        QCOMPARE(Hyprland::workspaceSelector(0, QStringLiteral("3")), QStringLiteral("name:3"));
        QCOMPARE(Hyprland::workspaceSelector(-99, QStringLiteral("special:omastrator-spare")), QStringLiteral("special:omastrator-spare"));

        FakeHyprctl hyprctl(m_directory.path());
        hyprctl.clearLog();
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        Hyprland::moveWindow(QStringLiteral("5588a1"), Hyprland::workspaceSelector(2, QStringLiteral("2")), false);
        Hyprland::focusWorkspace(Hyprland::workspaceSelector(2, QStringLiteral("2")));
        Hyprland::moveWindow(QStringLiteral("5588a1"), Hyprland::workspaceSelector(-99, QStringLiteral("special:omastrator-spare")), false);
        QCOMPARE(hyprctl.log(),
                 (QStringList{QStringLiteral("dispatch movetoworkspacesilent 2,address:0x5588a1"), QStringLiteral("dispatch workspace 2"),
                              QStringLiteral("dispatch movetoworkspacesilent special:omastrator-spare,address:0x5588a1")}));
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    void luaStringsEscapeNewlines()
    {
        FakeHyprctl hyprctl(m_directory.path());
        hyprctl.clearLog();
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("config/hypr"))));
        QFile lua(m_directory.filePath(QStringLiteral("config/hypr/hyprland.lua")));
        QVERIFY(lua.open(QIODevice::WriteOnly));
        lua.close();
        // A line break in a name would end the Lua string, and with it the dispatcher.
        Hyprland::focusWorkspace(QStringLiteral("name:a\nb\rc"));
        QCOMPARE(hyprctl.log(), QStringList{QStringLiteral("eval hl.dispatch(hl.dsp.focus({ workspace = \"name:a\\nb\\rc\" }))")});
        QFile::remove(lua.fileName());
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    void aFailedDispatchSaysWhy()
    {
        FakeHyprctl hyprctl(m_directory.path());
        hyprctl.setFailing(true);
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        QCOMPARE(Hyprland::focusWorkspace(QStringLiteral("x")), QStringLiteral("no"));
        hyprctl.setFailing(false);
        qunsetenv("OMASTRATOR_HYPRCTL");
        QVERIFY(!Hyprland::focusWorkspace(QStringLiteral("x")).isEmpty());
    }
};

QTEST_MAIN(HyprlandEventsTests)
#include "HyprlandEventsTests.moc"
