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
// /usr/share/omarchy/shell/README.md, and the desktop island's plugin is gone.
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
        QVERIFY(plugins.contains(QStringLiteral("omastrator.design")));
        QVERIFY(plugins.contains(QStringLiteral("omastrator.ai")));
        QVERIFY(plugins.contains(QStringLiteral("omastrator.pages")));
        // The pill moved into the app: nothing installs it any more.
        QVERIFY(!plugins.contains(QStringLiteral("omastrator.island")));
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

    // The island's pill is gone; design mode kept its overlay in a service plugin of its own.
    void theIslandIsGoneAndDesignModeKeepsItsOverlay()
    {
        QVERIFY(!m_shell.exists(QStringLiteral("omastrator.island")));
        const QString entry = read(QStringLiteral("omastrator.design/Design.qml"));
        QVERIFY(!entry.isEmpty());
        // It makes the status stream and the overlay, as Island.qml did, without the pill's size.
        QVERIFY(entry.contains(QStringLiteral("O.Status {")));
        QVERIFY(entry.contains(QStringLiteral("Overlay { status: status }")));
        QVERIFY(!entry.contains(QStringLiteral("PanelWindow")));
        for (const QString &plugin : m_shell.entryList({QStringLiteral("omastrator*")}, QDir::Dirs)) {
            for (const QString &name : QDir(m_shell.filePath(plugin)).entryList({QStringLiteral("*.qml"), QStringLiteral("*.js")}, QDir::Files)) {
                const QString source = read(plugin + QLatin1Char('/') + name);
                for (const char *gone : {"islandWidth", "islandHeight", "islandHole", "islandShown", "islandShow", "labelsSeen", "\"expanded\""})
                    QVERIFY2(!source.contains(QLatin1String(gone)), qPrintable(plugin + QLatin1Char('/') + name + QStringLiteral(": ") + QLatin1String(gone)));
                QVERIFY2(!source.contains(QStringLiteral("omastrator.island")), qPrintable(plugin + QLatin1Char('/') + name));
                // Nothing runs the verbs that only served the pill.
                for (const char *verb : {"\"island\", \"tool\"", "\"island\", \"mode\", \"draw\"", "\"island\", \"expand\"", "\"island\", \"rest\"",
                                         "\"island\", \"seen\""})
                    QVERIFY2(!source.contains(QLatin1String(verb)), qPrintable(plugin + QLatin1Char('/') + name + QStringLiteral(": ") + QLatin1String(verb)));
            }
        }
    }

    // The tray light's click brings Omastrator forward with Ask focused, and its tooltip logic comes from the design plugin.
    void theTrayLightAsks()
    {
        const QString light = read(QStringLiteral("omastrator.ai/TrayLight.qml"));
        QVERIFY(light.contains(QStringLiteral("status.run([\"island\", \"ask\"])")));
        QVERIFY(!light.contains(QStringLiteral("\"mode\", \"ai\"")));
        QVERIFY(light.contains(QStringLiteral("import \"../omastrator.design/OverlayLogic.js\" as Logic")));
        QVERIFY(!read(QStringLiteral("omastrator.design/OverlayLogic.js")).isEmpty());
        QVERIFY(!read(QStringLiteral("omastrator.ai/manifest.json")).contains(QStringLiteral("island")));
    }

    // The page dots (docs/WORKSPACES.md, "In the bar") are drawn like Omarchy's own workspace numbers and click through the
    // same Lua dispatch; the widget can't load without quickshell, so its source is checked and its decisions run.
    void thePageDotsLookLikeTheWorkspaceNumbers()
    {
        const QString widget = read(QStringLiteral("omastrator.pages/PageDots.qml"));
        QVERIFY(!widget.isEmpty());
        // The same button, sizes and spacing as /usr/share/omarchy/shell/plugins/bar/widgets/Workspaces.qml.
        for (const char *same : {"WidgetButton {", "horizontalMargin: 6", "verticalPadding: 6", "fixedWidth: root.vertical ? root.barSize : Style.space(20)",
                                 "fixedHeight: root.barSize", "columnSpacing: root.vertical ? 0 : Style.space(1)", "rowSpacing: root.vertical ? Style.space(2) : 0"})
            QVERIFY2(widget.contains(QLatin1String(same)), same);
        // Focus comes from Hyprland's own state, not from a query; the page order from the status stream.
        QVERIFY(widget.contains(QStringLiteral("Hyprland.focusedWorkspace.name")));
        QVERIFY(widget.contains(QStringLiteral("O.Status { id: status }")));
        QVERIFY(!widget.contains(QStringLiteral("Process")));
        // Nothing (zero width) when no document claims workspaces.
        QVERIFY(widget.contains(QStringLiteral("implicitWidth: pageList.length === 0 ? 0 :")));
        QVERIFY(widget.contains(QStringLiteral("visible: pageList.length > 0")));
        QVERIFY(widget.contains(QStringLiteral("Quickshell.execDetached(Logic.pageCommand(status.binary, name))")));
        QVERIFY(widget.contains(QStringLiteral("onPressed: function() { root.focusPage(modelData.name) }")));
        QCOMPARE(QJsonDocument::fromJson(read(QStringLiteral("omastrator.pages/manifest.json")).toUtf8()).object()["barWidget"].toObject()["allowMultiple"].toBool(true), false);
    }

    void thePageDotsDecisionsRunWithoutAShell()
    {
#ifndef OMASTRATOR_HAVE_QML
        QSKIP("Qt Qml isn't installed, so the page dots' JavaScript can't run here.");
#else
        QString source = read(QStringLiteral("omastrator.pages/PageDotsLogic.js"));
        QVERIFY(source.startsWith(QStringLiteral(".pragma library")));
        source.remove(0, source.indexOf(QLatin1Char('\n')));
        QJSEngine engine;
        const QJSValue loaded = engine.evaluate(source, QStringLiteral("PageDotsLogic.js"));
        QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        auto call = [&](const char *name, const QJSValueList &args) {
            const QJSValue result = engine.globalObject().property(QLatin1String(name)).call(args);
            if (result.isError())
                qWarning() << result.toString();
            return result;
        };
        auto json = [&](const QByteArray &text) { return engine.evaluate(QStringLiteral("(%1)").arg(QString::fromUtf8(text))); };

        // A stream that isn't there yet, or has no field (an older Omastrator), shows nothing.
        QCOMPARE(call("pages", {json("{}")}).property(QStringLiteral("length")).toInt(), 0);
        QCOMPARE(call("pages", {json("{\"pageWorkspaces\": 3}")}).property(QStringLiteral("length")).toInt(), 0);
        QCOMPARE(call("pages", {engine.toScriptValue(QVariant())}).property(QStringLiteral("length")).toInt(), 0);

        const QByteArray status = "{\"pageWorkspaces\": ["
                                  "{\"name\": \"design:Poster · Front\", \"page\": \"Front\", \"document\": \"Poster\"},"
                                  "{\"name\": \"design:Poster · Back\", \"page\": \"Back\", \"document\": \"Poster\"},"
                                  "{\"page\": \"no name\"}]}";
        const QJSValue pages = call("pages", {json(status)});
        // In the stream's order, minus the entry with no workspace to focus.
        QCOMPARE(pages.property(QStringLiteral("length")).toInt(), 2);
        // The second page is focused: the glyph Omarchy uses for its own focused number, and full opacity.
        const QJSValue buttons = call("buttons", {pages, QJSValue(QStringLiteral("design:Poster · Back"))});
        QCOMPARE(buttons.property(0).property(QStringLiteral("text")).toString(), QStringLiteral("\u2022"));
        QCOMPARE(buttons.property(0).property(QStringLiteral("opacity")).toNumber(), 0.5);
        QVERIFY(!buttons.property(0).property(QStringLiteral("focused")).toBool());
        QCOMPARE(buttons.property(1).property(QStringLiteral("text")).toString(), QStringLiteral("\U000F14FB"));
        QCOMPARE(buttons.property(1).property(QStringLiteral("opacity")).toNumber(), 1.0);
        QVERIFY(buttons.property(1).property(QStringLiteral("focused")).toBool());
        // One document: the page's name alone.
        QCOMPARE(buttons.property(0).property(QStringLiteral("tooltip")).toString(), QStringLiteral("Front"));
        // Nothing focused (another workspace, or none yet): every page is a dot.
        const QJSValue none = call("buttons", {pages, QJSValue(QStringLiteral("3"))});
        QVERIFY(!none.property(0).property(QStringLiteral("focused")).toBool() && !none.property(1).property(QStringLiteral("focused")).toBool());

        // Two documents: the tooltip names the document too.
        const QJSValue two = call("pages", {json("{\"pageWorkspaces\": ["
                                                 "{\"name\": \"design:A · One\", \"page\": \"One\", \"document\": \"A\"},"
                                                 "{\"name\": \"design:B · One\", \"page\": \"One\", \"document\": \"B\"}]}")});
        const QJSValue twoButtons = call("buttons", {two, QJSValue(QString())});
        QCOMPARE(twoButtons.property(0).property(QStringLiteral("tooltip")).toString(), QStringLiteral("One (A)"));
        QCOMPARE(twoButtons.property(1).property(QStringLiteral("tooltip")).toString(), QStringLiteral("One (B)"));

        // A click focuses the workspace by name, the way Omarchy's widget focuses one by number.
        QCOMPARE(call("focusLua", {QJSValue(QStringLiteral("design:Poster · Back"))}).toString(),
                 QStringLiteral("hl.dsp.focus({ workspace = \"name:design:Poster · Back\" })"));
        // The click goes through the running app first; the Hyprland focus is what runs when that fails.
        const QJSValue command = call("pageCommand", {QJSValue(QStringLiteral("/opt/omastrator")), QJSValue(QStringLiteral("design:Poster · Back"))});
        QCOMPARE(command.property(QStringLiteral("length")).toInt(), 6);
        QCOMPARE(command.property(0).toString(), QStringLiteral("sh"));
        QCOMPARE(command.property(1).toString(), QStringLiteral("-c"));
        QCOMPARE(command.property(2).toString(), QStringLiteral("\"$0\" island page \"$1\" || hyprctl dispatch \"$2\""));
        QCOMPARE(command.property(3).toString(), QStringLiteral("/opt/omastrator"));
        QCOMPARE(command.property(4).toString(), QStringLiteral("design:Poster · Back"));
        QCOMPARE(command.property(5).toString(), QStringLiteral("hl.dsp.focus({ workspace = \"name:design:Poster · Back\" })"));
        // A binary that isn't known yet is `omastrator` on PATH, as the stream's own default.
        QCOMPARE(call("pageCommand", {QJSValue(QString()), QJSValue(QStringLiteral("design:A · B"))}).property(3).toString(), QStringLiteral("omastrator"));
        // Quotes and backslashes in a page's name stay inside the Lua string.
        QCOMPARE(call("focusLua", {QJSValue(QStringLiteral("design:Say \"hi\" \\ now"))}).toString(),
                 QStringLiteral("hl.dsp.focus({ workspace = \"name:design:Say \\\"hi\\\" \\\\ now\" })"));
#endif
    }

    // Design mode's overlay (docs/ANYWHERE.md) sits over every window and must never steal their clicks
    // unless the user asked it to: an empty input mask by default.
    void theOverlayIsClickThroughByDefault()
    {
        const QString overlay = read(QStringLiteral("omastrator.design/Overlay.qml"));
        QVERIFY(!overlay.isEmpty());
        // The plugin's entry says what design mode says about itself (the pill that showed it is gone).
        QVERIFY(read(QStringLiteral("omastrator.design/Design.qml")).contains(QStringLiteral("Logic.designOnLine(next)")));
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
        // The island's hole is gone with the island.
        QVERIFY(!mask.contains(QStringLiteral("islandHole")));
        // ...and so does the Omarchy bar: a drawing tool must not turn a click on its clock or tray light into a shape.
        QVERIFY(mask.contains(QStringLiteral("Region { x: 0; y: 0; width: window.width; height: window.place.reservedTop || 0; intersection: Intersection.Subtract }")));
        // A proposal left waiting keeps its Keep and Discard reachable, with or without design mode.
        QVERIFY(mask.contains(QStringLiteral("Region { item: proposalCard.visible ? proposalCard : null }")));
        // Keep and Discard take over from the bar whenever it isn't up (its home window off screen, a drawing tool,
        // onboarding), on the design monitor, so a waiting proposal never depends on where the home window is.
        QVERIFY(overlay.contains(QStringLiteral("visible: (root.proposal !== null || !!root.design.waiting) && !barCard.visible && root.proposalScreen === window.modelData.name")));
        QVERIFY(overlay.contains(QStringLiteral("onClicked: root.run([\"design\", \"keep\"])")));
        QVERIFY(overlay.contains(QStringLiteral("onClicked: root.run([\"design\", \"discard\"])")));
        // The keyboard stays with the apps unless something is being typed.
        QVERIFY(window.contains(QStringLiteral("WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None")));
        QVERIFY(!overlay.contains(QStringLiteral("WlrKeyboardFocus.Exclusive")));
    }

    void theOverlaysDecisionsRunWithoutACompositor()
    {
#ifndef OMASTRATOR_HAVE_QML
        QSKIP("Qt Qml isn't installed, so the overlay's JavaScript can't run here.");
#else
        QString source = read(QStringLiteral("omastrator.design/OverlayLogic.js"));
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

        // Design mode's line when it turns on: Esc only leaves with setup's keys loaded, or under a drawing tool.
        const QString escLeaves = QStringLiteral("Design mode: point at anything. Clicks still reach the app; Esc leaves");
        const QString clickLeaves = QStringLiteral("Design mode: point at anything. Clicks still reach the app; run `omastrator reset` to leave");
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "point"}, {"keysLoaded", true}}}).toString(), escLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "point"}, {"keysLoaded", false}}}).toString(), clickLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "inspect"}, {"keysLoaded", false}}}).toString(), clickLeaves);
        QCOMPARE(call("designOnLine", {QVariantMap{{"tool", "rectangle"}, {"keysLoaded", false}}}).toString(), escLeaves);

        // The island's pill and the rule that showed it are gone from the overlay's logic.
        for (const char *gone : {"islandHole", "islandShown", "ownClasses"})
            QVERIFY2(!engine.globalObject().hasProperty(QLatin1String(gone)), gone);

        // The bar under the thing, above it near the bottom, always inside the screen.
        QVariantMap spot = call("barPosition", {QVariantList{2000, 100, 200, 40}, 300, 60, screen, 10}).toMap();
        QCOMPARE(spot["x"].toInt(), 30);
        QCOMPARE(spot["y"].toInt(), 150);
        spot = call("barPosition", {QVariantList{3700, 1000, 100, 60}, 300, 60, screen, 10}).toMap();
        QCOMPARE(spot["x"].toInt(), 1610);
        QCOMPARE(spot["y"].toInt(), 930);
        // Never over the Omarchy bar: a thing at the very top puts the bar below topClear.
        spot = call("barPosition", {QVariantList{2400, 0, 300, 40}, 300, 60, screen, 10, 80}).toMap();
        QCOMPARE(spot["y"].toInt(), 80);
        spot = call("barPosition", {QVariantList{2400, 0, 300, 1070}, 300, 60, screen, 10, 80}).toMap();
        QVERIFY(spot["y"].toInt() >= 80);
        // Something as big as the screen (the desktop): the bar waits at the bottom, centred.
        spot = call("barPosition", {QVariantList{1920, 0, 1920, 1080}, 300, 60, screen, 10, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 810);
        QCOMPARE(spot["y"].toInt(), 990);
        // Dragged or pinned, it stays on the screen and below the Omarchy bar.
        spot = call("clampBar", {-50, 10, 300, 60, screen, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 4);
        QCOMPARE(spot["y"].toInt(), 80);
        spot = call("clampBar", {5000, 5000, 300, 60, screen, 80}).toMap();
        QCOMPARE(spot["x"].toInt(), 1616);
        QCOMPARE(spot["y"].toInt(), 1016);

        // The bar sticks to its window: it shows on the monitor its target is on, and not at all without one.
        QVERIFY(call("barShownOn", {QVariantMap{{"bounds", QVariantList{2000, 100, 200, 40}}}, screen}).toBool());
        QVERIFY(!call("barShownOn", {QVariantMap{{"bounds", QVariantList{100, 100, 200, 40}}}, screen}).toBool());
        QVERIFY(!call("barShownOn", {QVariant(), screen}).toBool());

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

    // The floating bar takes the installed theme's look through one place, O.Theme,
    // which reads Graphite's tokens when the theme has them.
    void theBarFollowsTheThemeTokens()
    {
        const QString theme = read(QStringLiteral("omastrator-ui/Theme.qml"));
        QVERIFY(theme.contains(QStringLiteral("pragma Singleton")));
        QVERIFY(theme.contains(QStringLiteral("Color.shellValues[\"graphite.\" + name]")));
        QVERIFY(read(QStringLiteral("omastrator-ui/qmldir")).contains(QStringLiteral("singleton Theme 1.0 Theme.qml")));
        for (const char *file : {"omastrator.design/Overlay.qml"}) {
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
        const QString overlay = read(QStringLiteral("omastrator.design/Overlay.qml"));
        const QRegularExpression focus(QStringLiteral("keyboardFocus: Logic\\.wantsKeyboard\\([^\\n]*asking"));
        QVERIFY(focus.match(overlay).hasMatch());
        const QString ask = overlay.mid(overlay.indexOf(QStringLiteral("id: askField")));
        QVERIFY(ask.contains(QStringLiteral("window.asking = true")));
        QVERIFY(ask.contains(QStringLiteral("mouse.accepted = false")));
    }

    // A binary that can't start never sends exited; the overlay would never connect.
    void theStatusStreamRetriesAFailedStart()
    {
        const QString process = block(read(QStringLiteral("omastrator-ui/Status.qml")), QStringLiteral("Process"));
        QVERIFY(process.contains(QStringLiteral("onRunningChanged")));
        QVERIFY(process.contains(QStringLiteral("restart.start()")));
    }
};

QTEST_GUILESS_MAIN(ShellPluginTests)
#include "ShellPluginTests.moc"
