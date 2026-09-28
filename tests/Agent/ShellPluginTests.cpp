#include "Document/EditorSession.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>
#ifdef OMASTRATOR_HAVE_QML
#include <QJSEngine>
#endif

// The omarchy-shell plugins in shell/: each manifest keeps the contract in
// /usr/share/omarchy/shell/README.md, and the island can draw every tool.
class ShellPluginTests : public QObject {
    Q_OBJECT

private:
    QDir m_shell{QStringLiteral(OMASTRATOR_SOURCE_DIR "/shell")};

    QString read(const QString &path)
    {
        QFile file(m_shell.filePath(path));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    // The text of the first `name {` block, braces balanced.
    static QString block(const QString &source, const QString &name)
    {
        const qsizetype start = source.indexOf(name + QStringLiteral(" {"));
        if (start < 0)
            return {};
        int depth = 0;
        for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{'))
                ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0)
                return source.mid(start, i - start + 1);
        }
        return {};
    }

private slots:
    void manifestsKeepTheContract()
    {
        const QStringList plugins = m_shell.entryList({QStringLiteral("omastrator.*")}, QDir::Dirs);
        QVERIFY(plugins.contains(QStringLiteral("omastrator.island")));
        for (const QString &plugin : plugins) {
            QJsonParseError error{};
            const QJsonObject manifest = QJsonDocument::fromJson(read(plugin + QStringLiteral("/manifest.json")).toUtf8(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(plugin));
            QCOMPARE(manifest["schemaVersion"].toInt(), 1);
            // The folder is named by the id, as `omarchy plugin` installs it.
            QCOMPARE(manifest["id"].toString(), plugin);
            for (const char *key : {"name", "version", "description"})
                QVERIFY2(!manifest[QLatin1String(key)].toString().isEmpty(), key);
            const QJsonObject entries = manifest["entryPoints"].toObject();
            QVERIFY(!manifest["kinds"].toArray().isEmpty());
            for (const QJsonValue &kind : manifest["kinds"].toArray()) {
                const QString key = kind.toString() == QLatin1String("bar-widget") ? QStringLiteral("barWidget") : kind.toString();
                QVERIFY2(entries.contains(key), qPrintable(plugin + QLatin1Char(' ') + key));
                QVERIFY2(!read(plugin + QLatin1Char('/') + entries[key].toString()).isEmpty(), qPrintable(entries[key].toString()));
            }
        }
    }

    void theIslandDrawsEveryToolItOffers()
    {
        const QString icons = read(QStringLiteral("omastrator-ui/Icons.js"));
        const QString island = read(QStringLiteral("omastrator.island/Island.qml"));
        QVERIFY(!icons.isEmpty() && !island.isEmpty());
        auto hasIcon = [&](const QString &name) { return icons.contains(QStringLiteral("\n  ") + name + QStringLiteral(": [")); };
        const QRegularExpression item(QStringLiteral("\\{ id: \"(\\w+)\"(?:, icon: \"(\\w+)\")?"));
        // Draw mode's buttons are the app's own tools.
        const qsizetype draw = island.indexOf(QStringLiteral("draw: ["));
        const QString drawList = island.mid(draw, island.indexOf(QLatin1Char(']'), draw) - draw);
        int tools = 0;
        for (auto match = item.globalMatch(drawList); match.hasNext(); ++tools)
            QVERIFY2(toolNamed(match.next().captured(1)).has_value(), qPrintable(drawList));
        QVERIFY(tools >= 13);
        // Every button anywhere has an icon.
        for (auto match = item.globalMatch(island); match.hasNext();) {
            const auto found = match.next();
            const QString name = found.captured(2).isEmpty() ? found.captured(1) : found.captured(2);
            QVERIFY2(hasIcon(name), qPrintable(name));
        }
        for (const char *mode : {"normal", "draw", "capture", "ai", "live", "design", "previous", "next"})
            QVERIFY2(hasIcon(QLatin1String(mode)), mode);
        // Design mode's row: the overlay's own tools, as `omastrator design tool` takes them.
        const qsizetype design = island.indexOf(QStringLiteral("design: ["));
        QVERIFY(design > 0);
        const QString designList = island.mid(design, island.indexOf(QLatin1Char(']'), design) - design);
        for (const char *tool : {"inspect", "pen", "rectangle", "ellipse", "arrow", "text", "note", "desk", "done"})
            QVERIFY2(designList.contains(QStringLiteral("id: \"%1\"").arg(QLatin1String(tool))), tool);
    }

    // Design mode's overlay (docs/ANYWHERE.md) sits over every window and must never steal their clicks
    // unless the user asked it to: an empty input mask by default.
    void theOverlayIsClickThroughByDefault()
    {
        const QString overlay = read(QStringLiteral("omastrator.island/Overlay.qml"));
        QVERIFY(!overlay.isEmpty());
        QVERIFY(read(QStringLiteral("omastrator.island/Island.qml")).contains(QStringLiteral("Logic.designOnLine(next)")));
        QVERIFY(read(QStringLiteral("omastrator.island/Island.qml")).contains(QStringLiteral("Overlay { status: status; islandWidth: root.pillWidth; islandHeight: root.pillHeight }")));
        const QString window = block(overlay, QStringLiteral("PanelWindow"));
        QVERIFY(window.contains(QStringLiteral("WlrLayershell.layer: WlrLayer.Overlay")));
        QVERIFY(window.contains(QStringLiteral("exclusionMode: ExclusionMode.Ignore")));
        // The mask's own item is empty unless a drawing tool is chosen; each card joins only while shown.
        const QString mask = block(window, QStringLiteral("mask: Region"));
        QVERIFY(mask.contains(QStringLiteral("item: window.maskMode === \"full\" ? everything : null")));
        const QRegularExpression part(QStringLiteral("Region \\{ item: window\\.maskMode === \"panels\" && (\\w+)\\.visible \\? \\1 : null \\}"));
        int parts = 0;
        for (auto match = part.globalMatch(mask); match.hasNext(); match.next())
            ++parts;
        QCOMPARE(parts, 5);
        // Even a drawing tool leaves the island reachable: its buttons change the tool and leave design mode.
        QVERIFY(mask.contains(QStringLiteral("Region { item: islandHole; intersection: Intersection.Subtract }")));
        // ...and so does the Omarchy bar: a drawing tool must not turn a click on its clock or tray light into a shape.
        QVERIFY(mask.contains(QStringLiteral("Region { x: 0; y: 0; width: window.width; height: window.place.reservedTop || 0; intersection: Intersection.Subtract }")));
        // A proposal left waiting keeps its Keep and Discard reachable, with or without design mode.
        QVERIFY(mask.contains(QStringLiteral("Region { item: proposalCard.visible ? proposalCard : null }")));
        // The keyboard stays with the apps unless something is being typed.
        QVERIFY(window.contains(QStringLiteral("WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None")));
        QVERIFY(!overlay.contains(QStringLiteral("WlrKeyboardFocus.Exclusive")));
    }

    void theOverlaysDecisionsRunWithoutACompositor()
    {
#ifndef OMASTRATOR_HAVE_QML
        QSKIP("Qt Qml isn't installed, so the overlay's JavaScript can't run here.");
#else
        QString source = read(QStringLiteral("omastrator.island/OverlayLogic.js"));
        QVERIFY(source.startsWith(QStringLiteral(".pragma library")));
        source.remove(0, source.indexOf(QLatin1Char('\n')));
        QJSEngine engine;
        const QJSValue loaded = engine.evaluate(source, QStringLiteral("OverlayLogic.js"));
        QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        auto call = [&](const char *name, const QVariantList &args) {
            QJSValueList values;
            for (const QVariant &arg : args)
                values << engine.toScriptValue(arg);
            const QJSValue result = engine.globalObject().property(QLatin1String(name)).call(values);
            if (result.isError())
                qWarning() << result.toString();
            return result.toVariant();
        };
        const QVariantMap screen{{"name", "DP-1"}, {"x", 1920}, {"y", 0}, {"width", 1920}, {"height", 1080}};
        QVariantMap design{{"on", false}, {"monitor", "DP-1"}, {"tool", "inspect"}};
        // Off, or inspecting with nothing shown: nothing takes a click.
        QCOMPARE(call("maskMode", {design, screen, true}).toString(), QStringLiteral("none"));
        design["on"] = true;
        QCOMPARE(call("maskMode", {design, screen, false}).toString(), QStringLiteral("none"));
        // The bar or a card takes clicks only where it is.
        QCOMPARE(call("maskMode", {design, screen, true}).toString(), QStringLiteral("panels"));
        // Another monitor stays click-through.
        QVariantMap other = screen;
        other["name"] = "HDMI-A-1";
        QCOMPARE(call("maskMode", {design, other, true}).toString(), QStringLiteral("none"));
        // A drawing tool takes the monitor until Inspect gives it back.
        design["tool"] = "rectangle";
        QCOMPARE(call("maskMode", {design, screen, false}).toString(), QStringLiteral("full"));
        // A drawing tool takes the keyboard too, so Esc reaches the overlay without Hyprland's keys.
        QVERIFY(call("wantsKeyboard", {design, screen, false}).toBool());
        design["tool"] = "text";
        QVERIFY(call("wantsKeyboard", {design, screen, false}).toBool());
        design["tool"] = "inspect";
        QVERIFY(call("wantsKeyboard", {design, screen, true}).toBool());
        QVERIFY(!call("wantsKeyboard", {design, other, true}).toBool());

        // The island's line when design mode turns on: Esc only leaves with setup's keys loaded, or under a drawing tool.
        const QString escLeaves = QStringLiteral("Design mode: point at anything. Clicks still reach the app; Esc leaves");
        const QString clickLeaves = QStringLiteral("Design mode: point at anything. Clicks still reach the app; click the island's Leave (or run `omastrator reset`) to leave");
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "point"}, {"keysLoaded", true}}}).toString(), escLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "point"}, {"keysLoaded", false}}}).toString(), clickLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "inspect"}, {"keysLoaded", false}}}).toString(), clickLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "rectangle"}, {"keysLoaded", false}}}).toString(), escLeaves);

        // The island's pill: centred under the bar's reserved space, with a margin; nothing before the island has a size.
        QVariantMap barred = screen;
        barred["reservedTop"] = 26;
        const QVariantMap hole = call("islandHole", {barred, 400, 34, 5}).toMap();
        QCOMPARE(hole["x"].toInt(), 754);
        QCOMPARE(hole["y"].toInt(), 25);
        QCOMPARE(hole["width"].toInt(), 412);
        QCOMPARE(hole["height"].toInt(), 46);
        QCOMPARE(call("islandHole", {screen, 0, 34, 5}).toMap()["width"].toInt(), 0);

        // The bar under the thing, above it near the bottom, always inside the screen.
        QVariantMap spot = call("barPosition", {QVariantList{2000, 100, 200, 40}, 300, 60, screen, 10}).toMap();
        QCOMPARE(spot["x"].toInt(), 30);
        QCOMPARE(spot["y"].toInt(), 150);
        spot = call("barPosition", {QVariantList{3700, 1000, 100, 60}, 300, 60, screen, 10}).toMap();
        QCOMPARE(spot["x"].toInt(), 1610);
        QCOMPARE(spot["y"].toInt(), 930);
        // Never over the bar and island: a thing at the very top puts the bar below topClear.
        spot = call("barPosition", {QVariantList{2400, 0, 300, 40}, 300, 60, screen, 10, 80}).toMap();
        QCOMPARE(spot["y"].toInt(), 80);
        spot = call("barPosition", {QVariantList{2400, 0, 300, 1070}, 300, 60, screen, 10, 80}).toMap();
        QVERIFY(spot["y"].toInt() >= 80);
        // Something as big as the screen (the desktop): the bar waits at the bottom, centred.
        spot = call("barPosition", {QVariantList{1920, 0, 1920, 1080}, 300, 60, screen, 10, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 810);
        QCOMPARE(spot["y"].toInt(), 990);
        // Dragged or pinned, it stays on the screen and below the island.
        spot = call("clampBar", {-50, 10, 300, 60, screen, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 4);
        QCOMPARE(spot["y"].toInt(), 80);
        spot = call("clampBar", {5000, 5000, 300, 60, screen, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 1616);
        QCOMPARE(spot["y"].toInt(), 1016);

        // A lift's progress reads plainly on the bar.
        QCOMPARE(call("liftText", {QVariantMap{{"label", "div.card"}, {"stage", "Fetching pictures…"}, {"done", 3}, {"total", 8}}}).toString(),
                 QStringLiteral("Lifting div.card: Fetching pictures… 3 of 8"));
        QCOMPARE(call("liftText", {QVariant()}).toString(), QString());
        // A drawing becomes `omastrator design draw`.
        const QVariantList points{QVariantMap{{"x", 10.4}, {"y", 20}}, QVariantMap{{"x", 300}, {"y", 40.6}}};
        QCOMPARE(call("drawArgs", {"arrow", points, ""}).toStringList(), (QStringList{"design", "draw", "arrow", "10,20", "300,41"}));
        QCOMPARE(call("drawArgs", {"note", points, "Too tight"}).toStringList().mid(5), (QStringList{"--text", "Too tight"}));
        QCOMPARE(call("addPoint", {points, 301, 41, 3}).toList().size(), 2);
        QCOMPARE(call("addPoint", {points, 320, 60, 3}).toList().size(), 3);
        // The bar's buttons, chips and destinations name what they act on.
        const QVariantMap bar{{"target", 7}, {"surface", "window:foot"}};
        QCOMPARE(call("actionArgs", {bar, QVariantMap{{"id", "capture"}}}).toStringList(), (QStringList{"design", "action", "capture", "--target", "7"}));
        QCOMPARE(call("askArgs", {bar, "tighten  this"}).toStringList(), (QStringList{"design", "ask", "--target", "7", "tighten", "this"}));
        QCOMPARE(call("sendArgs", {QVariantMap{{"target", 0}, {"surface", "window:foot"}}, "desk", ""}).toStringList(),
                 (QStringList{"design", "send", "desk", "--surface", "window:foot"}));
        QCOMPARE(call("chipArgs", {bar, QVariantMap{{"action", "ask"}, {"prompt", "Pull its palette"}}}).toStringList().mid(4),
                 (QStringList{"Pull", "its", "palette"}));
        QCOMPARE(call("sizeLabel", {QVariantMap{{"bounds", QVariantList{0, 0, 120, 40}}, {"source", "dom"}, {"name", "button.buy"}}}).toString(),
                 QStringLiteral("button.buy  120 × 40"));
        // Desktop Look's gap handle: the outer gap at the monitor's edge, twice the inner gap between windows.
        const QVariantMap look{{"handles", true}, {"gapsIn", 4}, {"gapsOut", 10}, {"borderSize", 1}};
        QVariantMap gap = call("gapHandle", {look, QVariantList{1920 + 10, 40, 1920 - 21, 1030}, screen}).toMap();
        QCOMPARE(gap["key"].toString(), QStringLiteral("gapsOut"));
        QCOMPARE(gap["x"].toInt(), 1909);
        QCOMPARE(gap["width"].toInt(), 10);
        gap = call("gapHandle", {look, QVariantList{1920 + 10, 40, 900, 1030}, screen}).toMap();
        QCOMPARE(gap["key"].toString(), QStringLiteral("gapsIn"));
        QCOMPARE(gap["width"].toInt(), 8);
        QCOMPARE(call("gapAfterDrag", {gap, 12}).toInt(), 10);
        QCOMPARE(call("gapAfterDrag", {gap, -40}).toInt(), 0);
        QCOMPARE(call("gapArgs", {gap, 10}).toStringList(), (QStringList{"design", "look", "gapsIn=10"}));
        QVERIFY(call("gapHandle", {QVariantMap{{"handles", false}}, QVariantList{0, 0, 10, 10}, screen}).isNull());

        // The tray light's tooltip (shell/omastrator.ai/TrayLight.qml) when a result is ready: an
        // overlay proposal is answered with the bar's own Keep and Discard, never the keyboard, so
        // it reads differently from an app-window one, which really is Enter and Esc.
        QCOMPARE(call("readyTooltip", {QVariantMap{{"design", QVariantMap{{"proposal", QVariantMap{{"title", "AI: Palette"}}}}},
                                                    {"proposal", "AI: Something else"}}})
                     .toString(),
                 QStringLiteral("AI: Palette is on the overlay: Keep or Discard it in the bar"));
        QCOMPARE(call("readyTooltip", {QVariantMap{{"design", QVariantMap{{"proposal", QVariantMap{{"title", ""}}}}}}}).toString(),
                 QStringLiteral("A proposal is on the overlay: Keep or Discard it in the bar"));
        QCOMPARE(call("readyTooltip", {QVariantMap{{"proposal", "AI: Mock-up"}}}).toString(),
                 QStringLiteral("AI: Mock-up is ready: Enter keeps it, Esc discards it"));
        QCOMPARE(call("readyTooltip", {QVariantMap{{"variations", 3}}}).toString(), QStringLiteral("3 variations ready"));
        QCOMPARE(call("readyTooltip", {QVariantMap{{"variations", 1}}}).toString(), QStringLiteral("1 variation ready"));
        QCOMPARE(call("readyTooltip", {QVariantMap()}).toString(), QStringLiteral("Results ready in Omastrator"));
#endif
    }

    void everyQmlFileParses()
    {
        QString qmlformat = QStandardPaths::findExecutable(QStringLiteral("qmlformat"), {QLibraryInfo::path(QLibraryInfo::BinariesPath)});
        if (qmlformat.isEmpty())
            qmlformat = QStandardPaths::findExecutable(QStringLiteral("qmlformat"));
        if (qmlformat.isEmpty())
            QSKIP("qmlformat isn't installed");
        int files = 0;
        for (const QString &plugin : m_shell.entryList({QStringLiteral("omastrator*")}, QDir::Dirs)) {
            for (const QString &name : QDir(m_shell.filePath(plugin)).entryList({QStringLiteral("*.qml")}, QDir::Files)) {
                QProcess process;
                process.start(qmlformat, {m_shell.filePath(plugin + QLatin1Char('/') + name)});
                QVERIFY(process.waitForFinished(30000));
                QVERIFY2(process.exitCode() == 0, qPrintable(plugin + QLatin1Char('/') + name + QLatin1Char(' ') + QString::fromUtf8(process.readAllStandardError())));
                ++files;
            }
        }
        QVERIFY(files >= 4);
    }

    // Sizing the surface to the tooltip moved the pill from under the pointer,
    // which hid the tooltip and moved it back: hover flickered many times a second.
    void theIslandNeverResizesOnHover()
    {
        const QString window = block(read(QStringLiteral("omastrator.island/Island.qml")), QStringLiteral("PanelWindow"));
        QVERIFY(!window.isEmpty());
        for (const char *anchor : {"anchors.top: true", "anchors.left: true", "anchors.right: true"})
            QVERIFY2(window.contains(QLatin1String(anchor)), anchor);
        // Only the window's own lines count, not its children's.
        const QString own = window.left(window.indexOf(QStringLiteral("O.Panel {")));
        QVERIFY(!own.contains(QStringLiteral("implicitWidth")));
        const QRegularExpression height(QStringLiteral("implicitHeight:([^\\n]*)"));
        const QString heightBinding = height.match(own).captured(1);
        QVERIFY(!heightBinding.isEmpty());
        for (const char *hoverDependent : {"tip", "hover", "pill.", "row.", "expanded", "activity"})
            QVERIFY2(!heightBinding.contains(QLatin1String(hoverDependent)), qPrintable(heightBinding));
        QVERIFY(own.contains(QStringLiteral("mask: Region { item: pill }")));
    }

    // The island and the floating bar take the installed theme's look through one place, O.Theme,
    // which reads Graphite's tokens when the theme has them.
    void theIslandAndBarFollowTheThemeTokens()
    {
        const QString theme = read(QStringLiteral("omastrator-ui/Theme.qml"));
        QVERIFY(theme.contains(QStringLiteral("pragma Singleton")));
        QVERIFY(theme.contains(QStringLiteral("Color.shellValues[\"graphite.\" + name]")));
        QVERIFY(read(QStringLiteral("omastrator-ui/qmldir")).contains(QStringLiteral("singleton Theme 1.0 Theme.qml")));
        for (const char *file : {"omastrator.island/Island.qml", "omastrator.island/Overlay.qml"}) {
            const QString source = read(QString::fromLatin1(file));
            QVERIFY2(source.contains(QStringLiteral("O.Theme.")), file);
            QVERIFY2(!source.contains(QStringLiteral("Color.popups")), file);
            QVERIFY2(!source.contains(QStringLiteral("Style.font.family")), file);
        }
    }

    // A click in the floating bar's Ask must take the keyboard itself: the field only gets focus once the
    // surface has the keyboard, so waiting for its focus sent every letter to the window underneath.
    void askingTakesTheKeyboardOnTheClick()
    {
        const QString overlay = read(QStringLiteral("omastrator.island/Overlay.qml"));
        const QRegularExpression focus(QStringLiteral("keyboardFocus: Logic\\.wantsKeyboard\\([^\\n]*asking"));
        QVERIFY(focus.match(overlay).hasMatch());
        const QString ask = overlay.mid(overlay.indexOf(QStringLiteral("id: askField")));
        QVERIFY(ask.contains(QStringLiteral("window.asking = true")));
        QVERIFY(ask.contains(QStringLiteral("mouse.accepted = false")));
    }

    // A binary that can't start never sends exited; the island would never connect.
    void theStatusStreamRetriesAFailedStart()
    {
        const QString process = block(read(QStringLiteral("omastrator-ui/Status.qml")), QStringLiteral("Process"));
        QVERIFY(process.contains(QStringLiteral("onRunningChanged")));
        QVERIFY(process.contains(QStringLiteral("restart.start()")));
    }
};

QTEST_GUILESS_MAIN(ShellPluginTests)
#include "ShellPluginTests.moc"
