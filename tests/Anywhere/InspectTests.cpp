#include "Agent/Hyprland.h"
#include "Anywhere/DesignMode.h"
#include "Anywhere/Inspect.h"
#include "FakeDesktop.h"
#include "InspectFixtures.h"
#include "Live/Browser.h"
#include "Live/LiveSession.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Inspect and measure anywhere (docs/ANYWHERE.md): Hyprland's windows, the
// accessibility tree, grim's colour, a page's DOM in headless Chromium, and
// Figma's Alt distances.
namespace {
QString writeScript(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

}

class InspectTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        // Nothing here may reach the real Hyprland.
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
    }

    void hyprlandsAnswersAreRead()
    {
        const auto windows = Hyprland::parseClients(QJsonDocument::fromJson(InspectFixtures::clients).array());
        QCOMPARE(windows.size(), size_t(4));
        QCOMPARE(windows[1].className, QStringLiteral("pavucontrol"));
        QCOMPARE(windows[1].rect, QRect(100, 100, 400, 300));
        QVERIFY(windows[1].floating);
        QCOMPARE(windows[3].workspace, -98);
        const auto monitors = Hyprland::parseMonitors(QJsonDocument::fromJson(InspectFixtures::monitors).array());
        // Pixels become layout points; a quarter turn swaps the sides.
        QCOMPARE(monitors[0].rect, QRect(0, 0, 1920, 1080));
        QCOMPARE(monitors[0].scale, 2.0);
        // The bar's reserved space, which the overlay needs to find the island under it.
        QCOMPARE(monitors[0].reservedTop, 26);
        QCOMPARE(monitors[1].reservedTop, 0);
        QCOMPARE(monitors[1].rect, QRect(1920, 0, 1920, 1080));
        QCOMPARE(Hyprland::focusedMonitor(monitors)->name, QStringLiteral("DP-1"));
        QCOMPARE(Hyprland::parseCursor(QJsonDocument::fromJson(InspectFixtures::cursor).object()), std::optional<QPoint>(QPoint(12, 34)));
        QVERIFY(!Hyprland::parseCursor(QJsonObject()));
    }

    void theWindowUnderThePointerIsTheTopmostShownOne()
    {
        auto windows = Hyprland::parseClients(QJsonDocument::fromJson(InspectFixtures::clients).array());
        auto monitors = Hyprland::parseMonitors(QJsonDocument::fromJson(InspectFixtures::monitors).array());
        // Floating over tiled; a window on a hidden workspace never counts.
        QCOMPARE(Hyprland::windowAt(QPoint(200, 200), windows, monitors)->className, QStringLiteral("pavucontrol"));
        QCOMPARE(Hyprland::windowAt(QPoint(50, 900), windows, monitors)->className, QStringLiteral("foot"));
        QVERIFY(!Hyprland::windowAt(QPoint(1500, 900), windows, monitors));
        // A special workspace shown over the monitor covers the rest.
        monitors[0].specialWorkspace = -98;
        monitors[0].specialName = QStringLiteral("special:omastrator-desk");
        QCOMPARE(Hyprland::windowAt(QPoint(700, 300), windows, monitors)->title, QStringLiteral("Desk"));
        QCOMPARE(Hyprland::monitorAt(QPoint(2000, 10), monitors)->name, QStringLiteral("HDMI-A-1"));
    }

    void queriesAndDispatchesGoThroughTheOverride()
    {
        const QString log = m_directory.filePath(QStringLiteral("hyprctl.log"));
        const QString fake = writeScript(m_directory.filePath(QStringLiteral("hyprctl")),
                                         "printf '%s\\n' \"$*\" >> \"" + log.toUtf8() + "\"\n"
                                         "case \"$2\" in cursorpos) echo '{\"x\": 5, \"y\": 6}';; *) echo '[]';; esac\n");
        qputenv("OMASTRATOR_HYPRCTL", fake.toUtf8());
        QCOMPARE(Hyprland::parseCursor(Hyprland::query(QStringLiteral("cursorpos"))), std::optional<QPoint>(QPoint(5, 6)));
        // Omarchy 4's Lua config takes Lua through eval; hyprlang takes the dispatcher.
        QVERIFY(!Hyprland::usesLua());
        QCOMPARE(Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.submap(\"reset\"))"), QStringLiteral("submap reset")), QString());
        QDir().mkpath(m_directory.filePath(QStringLiteral("config/hypr")));
        QFile lua(m_directory.filePath(QStringLiteral("config/hypr/hyprland.lua")));
        QVERIFY(lua.open(QIODevice::WriteOnly));
        lua.close();
        QVERIFY(Hyprland::usesLua());
        QCOMPARE(Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.submap(\"reset\"))"), QStringLiteral("submap reset")), QString());
        QFile written(log);
        QVERIFY(written.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(written.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines, (QStringList{"-j cursorpos", "dispatch submap reset", "eval hl.dispatch(hl.dsp.submap(\"reset\"))"}));
        QFile::remove(lua.fileName());
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    void altDistancesAreFigmasGapsAndInsets()
    {
        // Apart side by side: one horizontal gap through the overlap.
        auto lines = Inspect::distances(QRect(0, 0, 100, 50), QRect(140, 10, 60, 60));
        QCOMPARE(lines.size(), size_t(1));
        QCOMPARE(lines[0].value, 40);
        QCOMPARE(lines[0].line, QLine(100, 30, 140, 30));
        // Diagonal: a horizontal and a vertical gap.
        lines = Inspect::distances(QRect(0, 0, 100, 100), QRect(150, 130, 20, 20));
        QCOMPARE(lines.size(), size_t(2));
        QCOMPARE(lines[0].value, 50);
        QCOMPARE(lines[1].value, 30);
        // One inside the other: the four insets.
        lines = Inspect::distances(QRect(10, 20, 30, 40), QRect(0, 0, 100, 100));
        QCOMPARE(lines.size(), size_t(4));
        QCOMPARE(lines[0].value, 20);
        QCOMPARE(lines[1].value, 40);
        QCOMPARE(lines[2].value, 10);
        QCOMPARE(lines[3].value, 60);
        // Overlapping: how far the near edges are apart.
        lines = Inspect::distances(QRect(0, 0, 100, 100), QRect(30, 20, 100, 100));
        QCOMPARE(lines.size(), size_t(2));
        QCOMPARE(lines[0].value, 30);
        QCOMPARE(lines[1].value, 20);
        QVERIFY(Inspect::distances(QRect(), QRect(0, 0, 10, 10)).empty());
    }

    void accessibleElementsLandInsideTheirWindow()
    {
        Surface surface;
        surface.kind = Surface::Kind::window;
        surface.app = QStringLiteral("gnome-calculator");
        surface.rect = QRect(200, 100, 400, 500);
        surface.key = Surface::keyFor(surface.kind, surface.app, {});
        const QJsonObject answer = QJsonDocument::fromJson(InspectFixtures::equalsButton).object();
        const auto inspection = Inspect::fromAccessible(answer, surface);
        QVERIFY(inspection);
        QCOMPARE(inspection->bounds, QRect(500, 500, 80, 40));
        QCOMPARE(inspection->role, QStringLiteral("push button"));
        QCOMPARE(inspection->fontFamily, QStringLiteral("Cantarell"));
        QCOMPARE(inspection->fontSize, 11.0);
        QCOMPARE(inspection->background, QColor(0x35, 0x84, 0xe4));
        QCOMPARE(inspection->summary(), QStringLiteral("Equals 80 × 40"));
        // Outside the window, or empty, isn't an element.
        QVERIFY(!Inspect::fromAccessible(QJsonDocument::fromJson(InspectFixtures::offWindow).object(), surface));
        QVERIFY(!Inspect::fromAccessible(QJsonDocument::fromJson(InspectFixtures::emptyPanel).object(), surface));

        // The helper is replaceable; without an answer, nothing.
        const QString fake = writeScript(m_directory.filePath(QStringLiteral("atspi")),
                                         "printf '{\"role\": \"label\", \"name\": \"pid %s at %s,%s\", \"rect\": [1, 2, 3, 4]}' \"$1\" \"$2\" \"$3\"\n");
        qputenv("OMASTRATOR_ATSPI", fake.toUtf8());
        const auto read = Inspect::accessibleAt(42, QPoint(7, 8));
        QVERIFY(read);
        QCOMPARE(read->value("name").toString(), QStringLiteral("pid 42 at 7,8"));
        writeScript(fake, "echo '{\"error\": \"This app has no accessibility tree.\"}'\n");
        QString error;
        QVERIFY(!Inspect::accessibleAt(42, QPoint(7, 8), &error));
        QCOMPARE(error, QStringLiteral("This app has no accessibility tree."));
        qunsetenv("OMASTRATOR_ATSPI");
    }

    void theAccessibilityHelperIsValidPython()
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            QSKIP("python3 isn't installed");
        QProcess check;
        check.start(python, {QStringLiteral("-c"), QStringLiteral("import sys; compile(sys.stdin.read(), 'atspi', 'exec')")});
        QVERIFY(check.waitForStarted());
        check.write(Inspect::accessibleScript().toUtf8());
        check.closeWriteChannel();
        QVERIFY(check.waitForFinished(10000));
        QVERIFY2(check.exitCode() == 0, check.readAllStandardError().constData());
    }

    void grimsPixelIsAColour()
    {
        QCOMPARE(Inspect::parsePpm(QByteArray("P6\n1 1\n255\n") + QByteArray("\x12\x34\x56", 3)), std::optional<QColor>(QColor(0x12, 0x34, 0x56)));
        QCOMPARE(Inspect::parsePpm(QByteArray("P6\n# grim\n1 1\n255\n") + QByteArray("\xff\x00\x10", 3)), std::optional<QColor>(QColor(255, 0, 16)));
        QVERIFY(!Inspect::parsePpm("P3\n1 1\n255\n0 0 0\n"));
        QVERIFY(!Inspect::parsePpm("P6\n1 1\n255\n"));
    }

    void aPagesViewportSitsUnderItsToolbars()
    {
        // A 1200 × 800 window with an 1196 × 700 page: 2 on each side, the rest on top.
        QCOMPARE(Inspect::viewportOrigin(QRect(100, 50, 1200, 800), QSizeF(1196, 700)), QPoint(102, 148));
        QCOMPARE(Inspect::viewportOrigin(QRect(100, 50, 1200, 800), QSizeF(1200, 800)), QPoint(100, 50));
        Surface surface;
        surface.rect = QRect(100, 50, 1200, 800);
        const QJsonObject answer = QJsonDocument::fromJson(InspectFixtures::pageAnswer).object();
        const auto inspection = Inspect::fromWeb(answer, surface);
        QVERIFY(inspection);
        QCOMPARE(inspection->surface.kind, Surface::Kind::web);
        QCOMPARE(inspection->surface.key, QStringLiteral("web:https://example.com/pricing"));
        QCOMPARE(inspection->surface.label(), QStringLiteral("example.com/pricing"));
        QCOMPARE(inspection->bounds, QRect(112, 168, 120, 40));
        // Art on a page is kept in page coordinates: the viewport's corner, scrolled.
        QCOMPARE(inspection->surface.origin(), QPointF(102, 148 - 250));
        QCOMPARE(inspection->background.alpha(), 128);
        QCOMPARE(inspection->styles["padding"].toString(), QStringLiteral("8px 16px"));
        QCOMPARE(inspection->summary(), QStringLiteral("button.buy 120 × 40"));
    }

    // A real page in headless Chromium, read through design mode as it reads Omastrator's browser.
    void aWebElementIsInspectedThroughTheDevToolsProtocol()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed, so pages can't be inspected here.");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        const QString page = m_directory.filePath(QStringLiteral("page.html"));
        QFile html(page);
        QVERIFY(html.open(QIODevice::WriteOnly));
        html.write(InspectFixtures::page);
        html.close();
        LiveSession live;
        LiveSession::Target target;
        target.url = QUrl::fromLocalFile(page);
        target.headless = true;
        target.profile = m_directory.filePath(QStringLiteral("profile"));
        QVERIFY(live.start(target).isEmpty());
        QElapsedTimer clock;
        clock.start();
        while (live.state() == LiveSession::State::starting && clock.elapsed() < 60'000)
            QTest::qWait(50);
        QVERIFY2(live.state() == LiveSession::State::running, qPrintable(live.message()));
        const QSize inner(live.evaluate(QStringLiteral("innerWidth")).toInt(), live.evaluate(QStringLiteral("innerHeight")).toInt());
        QVERIFY(!inner.isEmpty());

        // Its window, as Hyprland would place it: a fake desktop whose window has the browser's pid.
        FakeDesktop desktop;
        desktop.addWindow(QStringLiteral("chromium"), QRect(QPoint(500, 100), inner), live.browser().processId());
        DesignMode mode(desktop);
        mode.webPage = [&](const Hyprland::Window &window, QPoint point) -> std::optional<QJsonObject> {
            if (window.pid != live.browser().processId())
                return std::nullopt;
            return live.evaluate(Inspect::webScript(point, window.rect.size())).toObject();
        };
        mode.setOn(true);
        desktop.pointer = QPoint(500 + 100, 100 + 80);
        mode.poll();
        QVERIFY(mode.hover());
        const Inspection &button = *mode.hover();
        QCOMPARE(button.source, QStringLiteral("dom"));
        QCOMPARE(button.role, QStringLiteral("button"));
        QCOMPARE(button.name, QStringLiteral("button#buy"));
        QCOMPARE(button.bounds, QRect(540, 160, 120, 40));
        QCOMPARE(button.color, QColor(Qt::white));
        QCOMPARE(button.background, QColor(0x33, 0x55, 0xff));
        QCOMPARE(button.fontFamily, QStringLiteral("DejaVu Sans"));
        QCOMPARE(button.fontSize, 18.0);
        QCOMPARE(button.fontWeight, QStringLiteral("700"));
        QCOMPARE(button.styles["borderRadius"].toString(), QStringLiteral("8px"));
        QCOMPARE(button.text, QStringLiteral("Buy now"));
        QCOMPARE(button.surface.kind, Surface::Kind::web);
        QVERIFY(button.surface.key.startsWith(QLatin1String("web:file:")));

        // Alt from the button to the box: a 140 px gap.
        mode.setAlt(true);
        desktop.pointer = QPoint(500 + 320, 100 + 80);
        mode.poll();
        QCOMPARE(mode.hover()->name, QStringLiteral("div#box"));
        QCOMPARE(mode.hover()->background, QColor(Qt::red));
        const auto lines = mode.distances();
        QCOMPARE(lines.size(), size_t(1));
        QCOMPARE(lines[0].value, 140);
        mode.setOn(false);
        live.stop();
    }
};

QTEST_GUILESS_MAIN(InspectTests)
#include "InspectTests.moc"
