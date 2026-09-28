#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Cli.h"
#include "Agent/Island.h"
#include "Anywhere/AnywhereSettings.h"
#include "Anywhere/Desk.h"
#include "Anywhere/DesktopSource.h"
#include "IO/ProjectStore.h"
#include "UI/DesignController.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include "../Anywhere/FakeDesktop.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QVBoxLayout>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

// Design mode everywhere in the app (docs/ANYWHERE.md): drawing on a window's
// overlay, the floating bar's actions per surface kind, the Desk, Ask as a
// proposal, onboarding tuning the suggestions, and the background app that
// starts without a window.
namespace {
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "exit 2\n";

QStringList ids(const QJsonArray &list)
{
    QStringList result;
    for (const QJsonValue &each : list)
        result << each.toObject()["id"].toString();
    return result;
}

void writeExecutable(const QString &path, const QByteArray &body)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(body);
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
}

class DesignModeUiTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    // One app, hidden as the background app keeps it, on a fake desktop with a terminal at 100, 50.
    struct App {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window{workspace};
        FakeDesktop *desktop = nullptr;
        AgentBridge &bridge() { return *window.agent(); }
        DesignController &design() { return window.agent()->designMode(); }
        App()
        {
            window.setProperty("background", true);
            auto fake = std::make_unique<FakeDesktop>();
            desktop = fake.get();
            desktop->addWindow(QStringLiteral("foot"), QRect(100, 50, 800, 600), 4242);
            design().setSource(std::move(fake));
            design().start();
        }
        QJsonObject call(const QString &action, QJsonObject params = {}, QString *error = nullptr)
        {
            QJsonObject result;
            const QString failure = design().run(action, params, result);
            if (error)
                *error = failure;
            else if (!failure.isEmpty())
                qWarning().noquote() << action << failure;
            return result;
        }
        QJsonObject status() { return bridge().statusExtras()["design"].toObject(); }
    };

    QString read(const QString &path) const
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        // Nothing here may reach the real Hyprland, shell, clipboard or agent.
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        writeExecutable(m_directory.filePath(QStringLiteral("hyprctl")), "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$0.log\"\n");
        qputenv("OMASTRATOR_HYPRCTL", m_directory.filePath(QStringLiteral("hyprctl")).toUtf8());
        writeExecutable(m_directory.filePath(QStringLiteral("wl-copy")), "#!/bin/sh\ncat > \"$0.out\"\n");
        qputenv("OMASTRATOR_WL_COPY", m_directory.filePath(QStringLiteral("wl-copy")).toUtf8());
        writeExecutable(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("OMASTRATOR_RCLONE", "/bin/false");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("app.sock")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("FAKE_OUT", m_directory.path().toUtf8());
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    }

    void init()
    {
        QDir(m_directory.filePath(QStringLiteral("data"))).removeRecursively();
        QDir(m_directory.filePath(QStringLiteral("config"))).removeRecursively();
        QFile::remove(Island::statePath());
    }

    void drawingOnAWindowIsItsOverlayAndTheBarOffersTheTaskBarsActions()
    {
        App app;
        QVERIFY(!app.window.isVisible());
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        QVERIFY(app.design().mode().isOn());
        QCOMPARE(Island::read().mode, QStringLiteral("design"));
        // The first run asks about the designer's work, plainly saying what stays local.
        QJsonObject status = app.status();
        QVERIFY(status["onboarding"].toObject()["open"].toBool());
        QVERIFY(status["onboarding"].toObject()["note"].toString().contains(QLatin1String("stay on this machine")));
        app.call(QStringLiteral("onboarding"), {{"finish", false}});
        QVERIFY(!app.status()["onboarding"].toObject()["open"].toBool());

        // Hovering the terminal: the window's bar.
        app.desktop->pointer = QPoint(300, 300);
        app.design().mode().poll();
        status = app.status();
        QJsonObject bar = status["bar"].toObject();
        QCOMPARE(bar["kind"].toString(), QStringLiteral("window"));
        QCOMPARE(ids(bar["actions"].toArray()), (QStringList{"capture", "lift", "measure", "gapsAndBorders", "restyleApp", "handToAgent"}));
        QCOMPARE(bar["surface"].toString(), QStringLiteral("window:foot"));
        QVERIFY(!bar["placeholder"].toString().isEmpty());

        // A rectangle drawn over it is one undo step on its own layer, placed with the window.
        app.call(QStringLiteral("tool"), {{"tool", "rectangle"}});
        QCOMPARE(app.status()["tool"].toString(), QStringLiteral("rectangle"));
        const QJsonObject drawn = app.call(QStringLiteral("draw"), {{"tool", "rectangle"}, {"points", QJsonArray{QJsonArray{200, 150}, QJsonArray{400, 250}}}});
        QCOMPARE(drawn["surface"].toString(), QStringLiteral("window:foot"));
        QCOMPARE(drawn["step"].toString(), QStringLiteral("Draw Rectangle on foot"));
        OverlayStore &overlays = app.design().overlays();
        QCOMPARE(overlays.art(QStringLiteral("window:foot")).size(), size_t(1));
        QCOMPARE(overlays.session().document()->bounds(overlays.art(QStringLiteral("window:foot")).front()), QRectF(100, 100, 200, 100));
        QTRY_VERIFY(!app.status()["overlays"].toArray().isEmpty());
        const QJsonObject placed = app.status()["overlays"].toArray().first().toObject();
        QCOMPARE(placed["key"].toString(), QStringLiteral("window:foot"));
        QVERIFY(QFileInfo::exists(placed["png"].toString()));
        const QJsonArray rect = placed["rect"].toArray();
        QVERIFY(QRect(rect[0].toInt(), rect[1].toInt(), rect[2].toInt(), rect[3].toInt()).contains(QRect(200, 150, 200, 100)));
        // The window moves; its art goes with it.
        app.desktop->clients.front().rect.moveTopLeft(QPoint(500, 60));
        app.design().mode().refresh();
        QTRY_VERIFY(app.status()["overlays"].toArray().first().toObject()["rect"].toArray()[0].toInt() > 500);

        // The new art is selected: the bar offers the in-app task bar's actions for it.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        bar = app.status()["bar"].toObject();
        QCOMPARE(bar["kind"].toString(), QStringLiteral("art:path"));
        QCOMPARE(ids(bar["actions"].toArray()), (QStringList{"makeComponent", "designSystem", "handToAgent", "duplicate", "delete", "undo"}));
        QCOMPARE(ids(bar["destinations"].toArray()), (QStringList{"overlay", "desk", "document", "source", "agent"}));
        // Apply to Source waits for a page with its code on this machine.
        QCOMPARE(bar["destinations"].toArray()[3].toObject()["enabled"].toBool(true), false);
        app.call(QStringLiteral("action"), {{"id", "duplicate"}});
        QCOMPARE(overlays.art(QStringLiteral("window:foot")).size(), size_t(2));
        QCOMPARE(overlays.session().undoName(), QStringLiteral("Duplicate"));
        app.call(QStringLiteral("undo"));
        QCOMPARE(overlays.art(QStringLiteral("window:foot")).size(), size_t(1));
        QString error;
        app.call(QStringLiteral("action"), {{"id", "unite"}}, &error);
        QVERIFY(error.contains(QLatin1String("two or more")));
        // Deselecting gives the bar back to what's hovered.
        app.call(QStringLiteral("deselect"));
        app.desktop->pointer = QPoint(520, 100);
        app.design().mode().poll();
        QCOMPARE(app.status()["bar"].toObject()["kind"].toString(), QStringLiteral("window"));

        // The art is kept for the next session.
        QCOMPARE(overlays.save(), QString());
        QVERIFY(QFileInfo::exists(m_directory.filePath(QStringLiteral("data/omastrator/overlays.omai"))));

        // Esc: design mode ends and the island says so; the art stays on show.
        app.call(QStringLiteral("off"));
        QVERIFY(!app.design().mode().isOn());
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QVERIFY(!app.status()["overlays"].toArray().isEmpty());
        QVERIFY(!app.status().contains("bar"));
        // Clear all is the one step back to a clean screen: every surface's art goes.
        app.call(QStringLiteral("clear"), {{"surface", "all"}});
        QVERIFY(overlays.surfaces().isEmpty());
        QTRY_VERIFY(app.status()["overlays"].toArray().isEmpty());

        // Reset is the escape hatch from any state: drawing, with art up, design mode on.
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("tool"), {{"tool", "rectangle"}});
        app.call(QStringLiteral("draw"), {{"tool", "rectangle"}, {"points", QJsonArray{QJsonArray{200, 150}, QJsonArray{400, 250}}}});
        QVERIFY(!overlays.surfaces().isEmpty());
        app.call(QStringLiteral("reset"));
        QVERIFY(!app.design().mode().isOn());
        QCOMPARE(app.status()["tool"].toString(), QStringLiteral("point"));
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QVERIFY(overlays.surfaces().isEmpty());
        // Undo brings the drawings back.
        app.call(QStringLiteral("undo"));
        QVERIFY(!overlays.surfaces().isEmpty());
    }

    // Audit leftover: the escape hatch when setup never ran --apply (Hyprland has no Omastrator
    // binds loaded) and the user's attention has moved to another monitor since. The author's
    // rule: Omastrator never takes over the computer without a way out, Super+Alt+Escape and
    // `omastrator reset` work in every state, and nothing sits over the bar or the island.
    void resetIsTheEscapeHatchWithoutHyprlandKeysAndAcrossMonitors()
    {
        App app;
        QFile::remove(m_directory.filePath(QStringLiteral("hyprctl.log")));
        // This fixture never writes Omastrator's own Hyprland key file, so Setup::designKeysLoaded
        // is false here already, the same as a real setup that never ran --apply.
        app.call(QStringLiteral("on"));
        app.call(QStringLiteral("tool"), {{"tool", "rectangle"}});
        // Island::holdDesignKeys() checks that first: told to hold a submap Hyprland was never
        // given, every key in it would go unbound, which is worse than not switching at all.
        QVERIFY(!read(m_directory.filePath(QStringLiteral("hyprctl.log"))).contains(QLatin1String("dispatch submap omastrator-design")));

        // Art goes on the focused monitor (DP-1); the user's attention then moves to the other one.
        OverlayStore &overlays = app.design().overlays();
        app.call(QStringLiteral("draw"), {{"tool", "rectangle"}, {"points", QJsonArray{QJsonArray{200, 150}, QJsonArray{400, 250}}}});
        QCOMPARE(app.status()["monitor"].toString(), QStringLiteral("DP-1"));
        QVERIFY(!overlays.surfaces().isEmpty());
        app.desktop->screens[0].focused = false;
        app.desktop->screens[1].focused = true;

        // The escape hatch doesn't care which monitor it's called from, or which one the art is
        // on: `omastrator reset` (what Super+Alt+Escape runs) clears every monitor's art.
        app.call(QStringLiteral("reset"));
        QVERIFY(!app.design().mode().isOn());
        QCOMPARE(Island::read().mode, QStringLiteral("normal"));
        QVERIFY(overlays.surfaces().isEmpty());
    }

    void workGoesWhereTheUserChoosesAndTheDeskLabelsItsSource()
    {
        App app;
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("draw"), {{"tool", "arrow"}, {"points", QJsonArray{QJsonArray{150, 100}, QJsonArray{300, 100}}}});
        const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        // To the Desk: a frame with the window's screenshot and the art, labelled with where it came from.
        const QJsonObject sent = app.call(QStringLiteral("send"), {{"destination", "desk"}});
        QVERIFY(sent["label"].toString().startsWith(QStringLiteral("foot · ")));
        QCOMPARE(app.desktop->grabs, 1);
        ProjectTab *desk = app.design().deskTab();
        QVERIFY(desk);
        QCOMPARE(*desk->path, Desk::defaultPath());
        auto frames = Desk::frames(*desk->session.document());
        QCOMPARE(frames.size(), size_t(1));
        QVERIFY(frames[0].second == QStringLiteral("foot · ") + time || frames[0].second.startsWith(QStringLiteral("foot · ")));
        QCOMPARE(desk->session.undoName(), QStringLiteral("Send to Desk"));
        // The frame holds the screenshot and the arrow, in place over it.
        const std::vector<QUuid> parts = desk->session.document()->children(frames[0].first);
        QCOMPARE(parts.size(), size_t(4));
        QCOMPARE(desk->session.document()->find(parts[1])->kind, ObjectKind::image);
        // The Desk saves itself, and the window stayed hidden.
        QTRY_VERIFY(QFileInfo::exists(Desk::defaultPath()));
        QTRY_COMPARE(Desk::frames(ProjectStore::read(Desk::defaultPath())).size(), size_t(1));
        QVERIFY(!app.window.isVisible());
        // The choice is remembered as this surface's default.
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("window:foot")), QStringLiteral("desk"));
        QCOMPARE(app.status()["bar"].toObject()["destination"].toString(), QStringLiteral("desk"));

        // Capture to Desk from a hovered window, and the desktop under no window.
        app.call(QStringLiteral("deselect"));
        app.desktop->pointer = QPoint(1500, 900);
        app.design().mode().poll();
        QCOMPARE(app.status()["bar"].toObject()["kind"].toString(), QStringLiteral("desktop"));
        const QJsonObject captured = app.call(QStringLiteral("action"), {{"id", "capture"}});
        QVERIFY(captured["label"].toString().startsWith(QStringLiteral("Desktop (DP-1) · ")));
        QCOMPARE(Desk::frames(*desk->session.document()).size(), size_t(2));
        QString error;
        app.desktop->grabFails = true;
        app.call(QStringLiteral("action"), {{"id", "capture"}}, &error);
        QVERIFY(!error.isEmpty());
        app.desktop->grabFails = false;

        // To a document: a new tab with the art, and the window comes forward.
        app.call(QStringLiteral("send"), {{"destination", "document"}, {"surface", "window:foot"}});
        QVERIFY(app.window.isVisible());
        QCOMPARE(app.workspace.current().title(), QStringLiteral("foot"));
        QVERIFY(app.workspace.current().session.isModified());
        // To the source: only a page Live has the code for.
        app.call(QStringLiteral("send"), {{"destination", "source"}, {"surface", "window:foot"}}, &error);
        QVERIFY(error.contains(QLatin1String("Only pages open in Omastrator's browser")));
        // To the agent without words: the bar asks for them.
        QVERIFY(app.call(QStringLiteral("send"), {{"destination", "agent"}, {"surface", "window:foot"}})["needsPrompt"].toBool());

        // The Desk on its workspace: Hyprland shows the special workspace, then the window opens there.
        QFile::remove(m_directory.filePath(QStringLiteral("hyprctl.log")));
        app.window.hide();
        QCOMPARE(app.design().desk(QStringLiteral("show")), QString());
        QVERIFY(app.window.isVisible());
        QCOMPARE(app.workspace.current().path.value_or(QString()), Desk::defaultPath());
        QVERIFY(read(m_directory.filePath(QStringLiteral("hyprctl.log"))).contains(QLatin1String("dispatch togglespecialworkspace omastrator-desk")));
        // As a normal window, from the launcher.
        app.window.hide();
        QCOMPARE(app.design().desk(QStringLiteral("window")), QString());
        QVERIFY(app.window.isVisible());
        // In the background, closing the window only hides it: the Desk and documents stay open.
        app.window.close();
        QVERIFY(!app.window.isVisible());
        QVERIFY(app.design().deskTab());
    }

    void liftLandsWhereTheUserChoosesAsOneUndoStep()
    {
        App app;
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("onboarding"), {{"finish", false}});
        const QJsonObject tree{{"root", QJsonObject{{"role", "frame"}, {"name", "Terminal"}, {"rect", QJsonArray{0, 0, 800, 600}},
                                                   {"children", QJsonArray{QJsonObject{{"role", "label"}, {"rect", QJsonArray{10, 10, 200, 20}},
                                                                                       {"text", "~ $ ls"}, {"index", 0}}}}}}};
        app.desktop->trees.insert(4242, tree);
        auto hovered = [&]() -> QJsonValue {
            app.call(QStringLiteral("deselect"));
            app.desktop->pointer = QPoint(300, 300);
            app.design().mode().poll();
            return app.status()["bar"].toObject()["target"];
        };
        auto lifted = [&](const QJsonObject &params) {
            QString error;
            const QJsonObject result = app.call(QStringLiteral("lift"), params, &error);
            if (!error.isEmpty())
                return error;
            QElapsedTimer clock;
            clock.start();
            while (app.design().liftJob() && clock.elapsed() < 20'000)
                QTest::qWait(20);
            return app.design().liftJob() ? QStringLiteral("still lifting") : QString();
        };

        // Onto the overlay, in place: the window's own coordinates, one step named for it.
        QCOMPARE(lifted({{"target", hovered()}}), QString());
        const auto art = app.design().overlays().art(QStringLiteral("window:foot"));
        QCOMPARE(art.size(), size_t(1));
        EditorSession &overlay = app.design().overlays().session();
        QCOMPARE(overlay.undoName(), QStringLiteral("Lift foot"));
        QCOMPARE(overlay.document()->find(art.front())->name, QStringLiteral("foot"));
        QVERIFY(app.status()["message"].toString().startsWith(QLatin1String("Lifted foot")));
        QCOMPARE(app.status()["bar"].toObject()["kind"].toString(), QStringLiteral("art:group"));
        app.call(QStringLiteral("undo"));
        QVERIFY(app.design().overlays().art(QStringLiteral("window:foot")).empty());

        // To the Desk, remembered for this surface.
        QCOMPARE(lifted({{"target", hovered()}, {"to", "desk"}}), QString());
        ProjectTab *desk = app.design().deskTab();
        QVERIFY(desk);
        QCOMPARE(Desk::frames(*desk->session.document()).size(), size_t(1));
        QCOMPARE(desk->session.undoName(), QStringLiteral("Lift foot"));
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("window:foot")), QStringLiteral("desk"));
        QVERIFY(app.design().overlays().art(QStringLiteral("window:foot")).empty());
        // The next lift of it goes there by itself.
        QCOMPARE(lifted({{"target", hovered()}}), QString());
        QCOMPARE(Desk::frames(*desk->session.document()).size(), size_t(2));

        // To a new document of the art's size.
        const size_t tabs = app.workspace.tabs().size();
        QCOMPARE(lifted({{"target", hovered()}, {"to", "document"}}), QString());
        QCOMPARE(app.workspace.tabs().size(), tabs + 1);
        const ProjectTab &document = *app.workspace.tabs().back();
        QCOMPARE(document.session.undoName(), QStringLiteral("Lift foot"));
        QCOMPARE(document.session.document()->size, QSizeF(800, 600));

        // No tree: traced, and the bar offers the agent's clean-up.
        app.desktop->trees.clear();
        QCOMPARE(lifted({{"target", hovered()}, {"to", "overlay"}}), QString());
        QVERIFY(app.status()["message"].toString().contains(QLatin1String("traced")));
        QCOMPARE(ids(app.status()["bar"].toObject()["actions"].toArray()).first(), QStringLiteral("cleanUp"));

        // A second lift waits for the first; cancelling drops it.
        QString error;
        app.call(QStringLiteral("lift"), {{"target", hovered()}}, &error);
        QCOMPARE(error, QString());
        // Its progress is on the bar while it runs.
        QCOMPARE(app.status()["lift"].toObject()["label"].toString(), QStringLiteral("foot"));
        app.call(QStringLiteral("lift"), {{"target", hovered()}}, &error);
        QVERIFY(error.startsWith(QLatin1String("Already lifting")));
        app.call(QStringLiteral("lift"), {{"cancel", true}}, &error);
        QCOMPARE(error, QString());
        QTRY_VERIFY(!app.design().liftJob());
        QCOMPARE(app.status()["message"].toString(), QStringLiteral("Lift cancelled."));
    }

    void inspectingAPageGivesItsCssAndTheBarOffersPageActions()
    {
        App app;
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("onboarding"), {{"finish", true}});
        // A page stands in for Omastrator's browser: the web inspector answers for the terminal's pid.
        app.design().mode().webPage = [](const Hyprland::Window &, QPoint) -> std::optional<QJsonObject> {
            return QJsonObject{{"inner", QJsonArray{800, 600}},
                               {"scroll", QJsonArray{0, 0}},
                               {"url", "https://example.com/pricing"},
                               {"element", QJsonObject{{"tag", "button"},
                                                       {"selector", "button.buy"},
                                                       {"rect", QJsonArray{10, 20, 120, 40}},
                                                       {"color", "#ffffff"},
                                                       {"background", "#3355ff"},
                                                       {"fontFamily", "Inter"},
                                                       {"fontSize", 16},
                                                       {"fontWeight", "600"},
                                                       {"styles", QJsonObject{{"padding", "8px 16px"}, {"borderRadius", "8px"}}}}}};
        };
        app.desktop->pointer = QPoint(150, 90);
        app.design().mode().poll();
        const QJsonObject bar = app.status()["bar"].toObject();
        QCOMPARE(bar["kind"].toString(), QStringLiteral("web"));
        QCOMPARE(bar["surface"].toString(), QStringLiteral("web:https://example.com/pricing"));
        QCOMPARE(ids(bar["actions"].toArray()), (QStringList{"inspect", "lift", "mockup", "measure", "extractSystem", "handToAgent"}));
        // Inspect: the card's details, and Copy CSS puts real CSS on the clipboard.
        const QJsonObject inspected = app.call(QStringLiteral("action"), {{"id", "inspect"}, {"target", bar["target"]}});
        QVERIFY(inspected["css"].toString().contains(QLatin1String("border-radius: 8px;")));
        QCOMPARE(app.status()["detail"].toObject()["name"].toString(), QStringLiteral("button.buy"));
        app.call(QStringLiteral("action"), {{"id", "copyCss"}});
        QTRY_VERIFY(read(m_directory.filePath(QStringLiteral("wl-copy.out"))).contains(QLatin1String("button.buy {")));
        QVERIFY(read(m_directory.filePath(QStringLiteral("wl-copy.out"))).contains(QLatin1String("background: #3355ff;")));
        // Lift needs the page itself: with no browser running, it says so plainly.
        QString error;
        app.call(QStringLiteral("action"), {{"id", "lift"}, {"target", bar["target"]}}, &error);
        QVERIFY2(error.contains(QLatin1String("Omastrator's browser")), qPrintable(error));
        // Mock Up: the rectangle tool, pinned to the element.
        app.call(QStringLiteral("action"), {{"id", "mockup"}, {"target", bar["target"]}});
        QCOMPARE(app.status()["tool"].toString(), QStringLiteral("rectangle"));
        QCOMPARE(app.status()["selected"].toObject()["name"].toString(), QStringLiteral("button.buy"));
        // Art on a page is kept in the page's coordinates.
        app.call(QStringLiteral("draw"), {{"tool", "rectangle"}, {"points", QJsonArray{QJsonArray{110, 70}, QJsonArray{230, 110}}}});
        const auto art = app.design().overlays().art(QStringLiteral("web:https://example.com/pricing"));
        QCOMPARE(art.size(), size_t(1));
        QCOMPARE(app.design().overlays().session().document()->bounds(art.front()).topLeft(), QPointF(10, 20));
    }

    void onboardingAnswersChangeTheSuggestions()
    {
        App app;
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.desktop->pointer = QPoint(300, 300);
        app.design().mode().poll();
        auto first = [&] { return app.status()["bar"].toObject()["suggestions"].toArray().first().toObject()["id"].toString(); };
        // Unanswered: capturing is the likeliest thing to do with a window.
        QCOMPARE(first(), QStringLiteral("capture"));
        app.call(QStringLiteral("onboarding"), {{"question", "makes"}, {"values", QJsonArray{"rice"}}});
        QCOMPARE(first(), QStringLiteral("palette"));
        app.call(QStringLiteral("onboarding"), {{"question", "ai"}, {"values", QJsonArray{"quiet"}}});
        // Kept quiet: one chip, and not an AI one.
        const QJsonArray quiet = app.status()["bar"].toObject()["suggestions"].toArray();
        QCOMPARE(quiet.size(), 1);
        QVERIFY(!quiet.first().toObject()["ai"].toBool());
        QString error;
        app.call(QStringLiteral("onboarding"), {{"question", "makes"}, {"values", QJsonArray{"sculpture"}}}, &error);
        QVERIFY(!error.isEmpty());
        app.call(QStringLiteral("onboarding"), {{"finish", true}});
        QVERIFY(!AnywhereSettings::needsOnboarding());
        // It doesn't open by itself again; the island's help opens it.
        app.call(QStringLiteral("off"));
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        QVERIFY(!app.status()["onboarding"].toObject()["open"].toBool());
        app.call(QStringLiteral("onboarding"), {{"open", true}});
        QVERIFY(app.status()["onboarding"].toObject()["open"].toBool());
        QCOMPARE(app.status()["onboarding"].toObject()["questions"].toArray().size(), 3);
    }

    void askRunsTheAgentOnTheSurfaceAsAPreviewToKeep()
    {
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "overlay");
        App app;
        QCOMPARE(app.bridge().startServer(m_directory.filePath(QStringLiteral("ask.sock"))), QString());
        app.call(QStringLiteral("on"));
        // Hover only inspects with Inspect chosen; design mode starts on Point.
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.desktop->pointer = QPoint(300, 300);
        app.design().mode().poll();
        const int target = app.status()["bar"].toObject()["target"].toInt();
        QVERIFY(target > 0);
        QString error;
        app.call(QStringLiteral("ask"), {{"prompt", ""}, {"target", target}}, &error);
        QCOMPARE(error, QStringLiteral("Say what you'd like first."));
        const QJsonObject asked = app.call(QStringLiteral("ask"), {{"prompt", "mock up a tighter prompt"}, {"target", target}});
        QVERIFY(!asked["requestId"].toString().isEmpty());
        // The agent was told about the surface and given its screenshot, kept on this machine.
        QTRY_VERIFY(!FakeAgents::arguments(m_directory.path(), QStringLiteral("claude")).isEmpty());
        const QString prompt = FakeAgents::arguments(m_directory.path(), QStringLiteral("claude")).join(QLatin1Char('\n'));
        QVERIFY(prompt.contains(QLatin1String("mock up a tighter prompt")));
        QVERIFY(prompt.contains(QLatin1String("Surface: foot")));
        QVERIFY(prompt.contains(m_directory.filePath(QStringLiteral("data/omastrator/captures/design-"))));
        EditorSession &overlay = app.design().overlays().session();
        // Its answer is a proposal on the overlay: shown, not yet the user's.
        QTRY_VERIFY_WITH_TIMEOUT(app.bridge().hasProposalIn(overlay), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(app.status().contains("proposal"), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(app.status()["proposal"].toObject()["title"].toString(), QStringLiteral("AI: Mock-up"), 20000);
        QCOMPARE(app.bridge().designTarget(), &overlay);
        // Nothing else draws on the overlay until it's kept or discarded.
        app.call(QStringLiteral("draw"), {{"tool", "line"}, {"points", QJsonArray{QJsonArray{110, 60}, QJsonArray{120, 70}}}}, &error);
        QVERIFY(error.contains(QLatin1String("proposal")));
        // Kept: one undo step on the overlay, on the terminal's layer; the front document is the agent's again.
        app.call(QStringLiteral("keep"));
        QVERIFY(!app.bridge().hasProposalIn(overlay));
        QCOMPARE(overlay.undoName(), QStringLiteral("AI: Mock-up"));
        QCOMPARE(app.design().overlays().art(QStringLiteral("window:foot")).size(), size_t(1));
        QVERIFY(!app.bridge().designTarget());
        app.call(QStringLiteral("keep"), {}, &error);
        QCOMPARE(error, QStringLiteral("There's no preview on the overlay."));
        QTRY_VERIFY(!app.bridge().run());
        qputenv("FAKE_MODE", "quiet");
    }

    // The background app: `omastrator --daemon` owns the socket and the overlays, with no window until asked.
    // Omastrator's own windows are read in-process: over AT-SPI they'd wait on this very thread.
    void inspectingOmastratorsOwnWindowReadsItsWidgets()
    {
        QWidget window;
        window.setWindowTitle(QStringLiteral("Own window"));
        auto *layout = new QVBoxLayout(&window);
        layout->setContentsMargins(20, 30, 20, 20);
        auto *button = new QPushButton(QStringLiteral("Export"), &window);
        button->setFixedSize(100, 40);
        layout->addWidget(button);
        layout->addStretch();
        window.resize(300, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        Hyprland::Window own;
        own.pid = QCoreApplication::applicationPid();
        own.title = QStringLiteral("Own window");
        SystemSource source;
        const auto answer = source.accessible(own, button->geometry().center());
        QVERIFY(answer);
        QCOMPARE(answer->value("role").toString(), QStringLiteral("button"));
        QCOMPARE(answer->value("name").toString(), QStringLiteral("Export"));
        const QJsonArray rect = answer->value("rect").toArray();
        QCOMPARE(QRect(rect.at(0).toInt(), rect.at(1).toInt(), rect.at(2).toInt(), rect.at(3).toInt()), button->geometry());
        own.title = QStringLiteral("Someone else's");
        QVERIFY(!source.accessible(own, QPoint(5, 5)));
    }

    void theDaemonStartsWithoutAWindow()
    {
        const QString binary = QStringLiteral(OMASTRATOR_BINARY);
        QVERIFY(QFileInfo::exists(binary));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("OMASTRATOR_SOCKET"), m_directory.filePath(QStringLiteral("daemon.sock")));
        environment.remove(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));
        QProcess daemon;
        daemon.setProcessEnvironment(environment);
        daemon.start(binary, {QStringLiteral("--daemon")});
        QVERIFY(daemon.waitForStarted());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("daemon.sock")).toUtf8());
        QTRY_VERIFY_WITH_TIMEOUT(Island::appIsRunning(), 20000);
        auto call = [](const QString &method, const QJsonObject &params = {}) {
            AgentClient::Connection connection;
            return connection.call(method, params, 10000);
        };
        QJsonObject status = call(QStringLiteral("status_get"));
        QCOMPARE(status["window"].toBool(true), false);
        QVERIFY(status["design"].toObject().contains("on"));
        // A second `--daemon` sees it running and leaves quietly.
        QProcess second;
        second.setProcessEnvironment(environment);
        second.start(binary, {QStringLiteral("--daemon")});
        QVERIFY(second.waitForFinished(20000));
        QCOMPARE(second.exitCode(), 0);
        // Asked for, the window opens; the daemon was asked by a plain `omastrator` the same way.
        QProcess plain;
        plain.setProcessEnvironment(environment);
        plain.start(binary, {});
        QVERIFY(plain.waitForFinished(20000));
        QCOMPARE(plain.exitCode(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(call(QStringLiteral("status_get"))["window"].toBool(), 10000);
        // Design mode works through it.
        call(QStringLiteral("design"), {{"action", "on"}});
        QVERIFY(call(QStringLiteral("status_get"))["design"].toObject()["on"].toBool());
        call(QStringLiteral("design"), {{"action", "off"}});
        // Quit ends it, background and all.
        call(QStringLiteral("quit_app"));
        QVERIFY(daemon.waitForFinished(20000));
        QCOMPARE(daemon.exitStatus(), QProcess::NormalExit);
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("app.sock")).toUtf8());
    }
};

// Run as `DesignModeUiTests agent …`, this binary is the Omastrator CLI the fake agent calls.
int main(int argc, char **argv)
{
    if (argc > 1 && Cli::handles(argv[1])) {
        QCoreApplication app(argc, argv);
        return Cli::run(QCoreApplication::arguments().mid(1));
    }
    QApplication app(argc, argv);
    DesignModeUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}
#include "DesignModeUiTests.moc"
