#include "Agent/Island.h"
#include "Anywhere/DesignMode.h"
#include "FakeDesktop.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Design mode's state (docs/ANYWHERE.md): it follows the island's mode, which
// the hotkey, the island and Esc all set; it covers the monitor that had focus;
// hover inspects windows cheaply and the accessibility tree once the pointer rests.
class DesignModeTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("HOME", m_directory.filePath(QStringLiteral("home")).toUtf8());
        qunsetenv("XDG_CONFIG_HOME");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
    }

    // The island says "Esc leaves" only where setup's Hyprland keys are loaded (OverlayLogic.designOnLine).
    void itReportsWhetherTheDesignKeysAreLoaded()
    {
        FakeDesktop desktop;
        DesignMode mode(desktop);
        QVERIFY(!mode.status()["keysLoaded"].toBool());

        const QString config = m_directory.filePath(QStringLiteral("home/.config"));
        QVERIFY(QDir().mkpath(config + QStringLiteral("/omastrator")));
        QVERIFY(QDir().mkpath(config + QStringLiteral("/hypr")));
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        QVERIFY(write(config + QStringLiteral("/omastrator/hyprland.conf"), "submap = omastrator-design\nbind = , escape, exec, omastrator design off\nsubmap = reset\n"));
        QVERIFY(write(config + QStringLiteral("/hypr/hyprland.conf"), "source = ~/.config/omastrator/hyprland.conf\n"));
        QVERIFY(mode.status()["keysLoaded"].toBool());
    }

    void itFollowsTheIslandsModeBothWays()
    {
        FakeDesktop desktop;
        DesignMode mode(desktop);
        QSignalSpy toggled(&mode, &DesignMode::toggled);
        mode.followIsland();
        QVERIFY(!mode.isOn());
        // The hotkey runs `omastrator design on`, which sets the island's mode.
        Island::State state = Island::read();
        state.mode = QStringLiteral("design");
        QCOMPARE(Island::write(state), QString());
        QTRY_VERIFY(mode.isOn());
        QCOMPARE(toggled.count(), 1);
        QCOMPARE(mode.monitor(), QStringLiteral("DP-1"));
        // It starts on Point: nothing is inspected until Inspect is chosen.
        QCOMPARE(mode.tool(), QStringLiteral("point"));
        // Esc (design off) sets it back; so does anything that turns it off here.
        mode.setOn(false);
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        mode.setOn(true, QStringLiteral("HDMI-A-1"));
        QCOMPARE(Island::read().mode, QStringLiteral("design"));
        QVERIFY(Island::read().expanded);
        QCOMPARE(mode.monitor(), QStringLiteral("HDMI-A-1"));
        state = Island::read();
        state.mode = QStringLiteral("normal");
        Island::write(state);
        QTRY_VERIFY(!mode.isOn());
    }

    void toolsAreTheOverlaysOwn()
    {
        FakeDesktop desktop;
        DesignMode mode(desktop);
        QCOMPARE(mode.setTool(QStringLiteral("arrow")), QString());
        QCOMPARE(mode.tool(), QStringLiteral("arrow"));
        QVERIFY(mode.setTool(QStringLiteral("shapeBuilder")).contains(QLatin1String("inspect")));
        mode.setOn(true);
        mode.setOn(false);
        // Leaving puts the pointer back to Point, so the overlay is click-through and quiet next time.
        QCOMPARE(mode.tool(), QStringLiteral("point"));
    }

    void hoverInspectsTheWindowThenItsAccessibleElementOnceThePointerRests()
    {
        FakeDesktop desktop;
        desktop.addWindow(QStringLiteral("foot"), QRect(0, 0, 960, 1080), 100);
        desktop.addWindow(QStringLiteral("gnome-calculator"), QRect(1000, 100, 400, 500), 200, 1, true);
        desktop.accessibility.insert(200, QJsonObject{{"role", "push button"}, {"name", "Equals"}, {"rect", QJsonArray{300, 400, 80, 40}}});
        DesignMode mode(desktop);
        mode.setOn(true);
        // Point, where design mode starts, inspects nothing; Inspect does.
        desktop.pointer = QPoint(1350, 520);
        mode.poll();
        QVERIFY(!mode.hover());
        mode.setTool(QStringLiteral("inspect"));
        QSignalSpy changed(&mode, &DesignMode::changed);

        desktop.pointer = QPoint(1350, 520);
        mode.poll();
        // Moving: the window's bounds at once, no slow reads.
        QVERIFY(mode.hover());
        QCOMPARE(mode.hover()->source, QStringLiteral("window"));
        QCOMPARE(mode.hover()->bounds, QRect(1000, 100, 400, 500));
        QCOMPARE(mode.hover()->surface.key, QStringLiteral("window:gnome-calculator"));
        QCOMPARE(desktop.accessibleCalls, 0);
        const int windowId = mode.hover()->id;
        mode.poll();
        QCOMPARE(desktop.accessibleCalls, 0);
        // Rested: the accessibility tree and the colour under the pointer.
        mode.poll();
        QCOMPARE(desktop.accessibleCalls, 1);
        QCOMPARE(mode.hover()->source, QStringLiteral("accessibility"));
        QCOMPARE(mode.hover()->bounds, QRect(1300, 500, 80, 40));
        QCOMPARE(mode.hover()->pixel, desktop.screenColor);
        QVERIFY(mode.hover()->id != windowId);
        // Still resting: nothing is read again.
        mode.poll();
        mode.poll();
        QCOMPARE(desktop.accessibleCalls, 1);
        // What the bar showed stays reachable by its id after the pointer moves on.
        QVERIFY(mode.target(windowId));
        desktop.pointer = QPoint(100, 100);
        mode.poll();
        QCOMPARE(mode.hover()->surface.app, QStringLiteral("foot"));
        QVERIFY(changed.count() >= 3);

        // An app without a tree keeps the window's bounds after resting.
        mode.poll();
        mode.poll();
        QCOMPARE(mode.hover()->source, QStringLiteral("window"));
        QCOMPARE(mode.hover()->pixel, desktop.screenColor);
    }

    // The bar sticks to its window, so it has to know when that window moves, closes or leaves the screen while the pointer rests.
    void aWindowThatMovesOrLeavesIsNoticedWhileThePointerRests()
    {
        FakeDesktop desktop;
        desktop.addWindow(QStringLiteral("foot"), QRect(0, 0, 960, 1080), 100);
        DesignMode mode(desktop);
        mode.setOn(true);
        mode.setTool(QStringLiteral("inspect"));
        desktop.pointer = QPoint(100, 100);
        mode.poll();
        QSignalSpy changed(&mode, &DesignMode::changed);
        // Nothing moved: nothing to say.
        QTest::qWait(600);
        mode.poll();
        const int quiet = changed.count();
        desktop.clients[0].workspace = 3;
        QTest::qWait(600);
        mode.poll();
        QVERIFY(changed.count() > quiet);
        QVERIFY(!Hyprland::isShown(mode.windows().front(), mode.monitors()));
    }

    void itCoversOnlyItsMonitorAndTheDesktopUnderNoWindow()
    {
        FakeDesktop desktop;
        DesignMode mode(desktop);
        mode.setOn(true);
        mode.setTool(QStringLiteral("inspect"));
        desktop.pointer = QPoint(500, 500);
        mode.poll();
        QCOMPARE(mode.hover()->source, QStringLiteral("screen"));
        QCOMPARE(mode.hover()->surface.kind, Surface::Kind::desktop);
        QCOMPARE(mode.hover()->surface.key, QStringLiteral("desktop:DP-1"));
        QCOMPARE(mode.hover()->bounds, QRect(0, 0, 1920, 1080));
        // The other monitor isn't design mode's.
        desktop.pointer = QPoint(2500, 500);
        mode.poll();
        QVERIFY(!mode.hover());
    }

    void altMeasuresFromOneHoveredThingToTheNext()
    {
        FakeDesktop desktop;
        desktop.addWindow(QStringLiteral("foot"), QRect(0, 0, 800, 600), 100);
        desktop.addWindow(QStringLiteral("thunar"), QRect(900, 0, 600, 600), 101);
        DesignMode mode(desktop);
        mode.setOn(true);
        mode.setTool(QStringLiteral("inspect"));
        desktop.pointer = QPoint(100, 100);
        mode.poll();
        const int foot = mode.hover()->id;
        mode.setAlt(true);
        QCOMPARE(mode.anchor()->id, foot);
        QVERIFY(mode.distances().empty());
        desktop.pointer = QPoint(1000, 100);
        mode.poll();
        QCOMPARE(mode.distances().size(), size_t(1));
        QCOMPARE(mode.distances()[0].value, 100);
        const QJsonObject status = mode.status();
        QVERIFY(status["measuring"].toBool());
        QCOMPARE(status["distances"].toArray().size(), 1);
        mode.setAlt(false);
        QVERIFY(mode.distances().empty());

        // The bar's Measure holds the anchor without Alt; selecting pins the bar.
        mode.setMeasuring(foot, true);
        QCOMPARE(mode.distances().size(), size_t(1));
        mode.setMeasuring(foot, false);
        QVERIFY(mode.distances().empty());
        QVERIFY(mode.select(foot));
        QCOMPARE(mode.selected()->surface.app, QStringLiteral("foot"));
        QVERIFY(!mode.select(9999));
        QVERIFY(mode.select(0));
        QVERIFY(!mode.selected());
    }

    void drawingFindsTheSurfaceUnderThePoint()
    {
        FakeDesktop desktop;
        desktop.addWindow(QStringLiteral("foot"), QRect(10, 20, 800, 600), 100);
        desktop.addWindow(QStringLiteral("firefox"), QRect(900, 20, 800, 600), 102);
        DesignMode mode(desktop);
        const Surface foot = mode.surfaceAt(QPoint(50, 50));
        QCOMPARE(foot.kind, Surface::Kind::window);
        QCOMPARE(foot.rect, QRect(10, 20, 800, 600));
        QCOMPARE(foot.origin(), QPointF(10, 20));
        QCOMPARE(foot.monitor, QStringLiteral("DP-1"));
        // Another browser's pages can't be read: the bar offers Omastrator's own.
        QVERIFY(mode.surfaceAt(QPoint(1000, 50)).otherBrowser);
        QVERIFY(!foot.otherBrowser);
        QCOMPARE(mode.surfaceAt(QPoint(1850, 1000)).key, QStringLiteral("desktop:DP-1"));
    }
};

QTEST_GUILESS_MAIN(DesignModeTests)
#include "DesignModeTests.moc"
