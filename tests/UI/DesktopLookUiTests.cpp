#include "Agent/Island.h"
#include "System/AppStyle.h"
#include "System/ConfigBackup.h"
#include "UI/DesignController.h"
#include "UI/DesktopLookPanel.h"
#include "UI/NumberField.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SyncConfirmDialog.h"
#include "../Anywhere/FakeDesktop.h"
#include "../System/DesktopFixtures.h"
#include <QApplication>
#include <QDirIterator>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QtTest>

// Changing the real thing on the desktop (docs/ANYWHERE.md, phase 4), in the
// app: previews go to the fake hyprctl and omarchy-shell, Save goes through
// the confirmation dialog (Cancel writes nothing), and Revert puts every
// file back byte for byte. All in a temporary HOME; the real desktop is
// never read or written.

using namespace DesktopFixtures;

namespace {
// Every file under HOME as bytes (links as where they point), less the logs, the fakes and the backups.
QMap<QString, QByteArray> snapshot(const QString &home)
{
    QMap<QString, QByteArray> files;
    QDirIterator walk(home, QDir::Files | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        const QString relative = QDir(home).relativeFilePath(path);
        if (relative.startsWith(QLatin1String("bin/")) || relative.endsWith(QLatin1String(".log")) || relative.contains(QLatin1String("omastrator/backups"))
            || relative.startsWith(QLatin1String("runtime/")) || relative.contains(QLatin1String("omastrator/overlays")))
            continue;
        const QFileInfo info(path);
        files.insert(relative, info.isSymLink() ? "-> " + info.symLinkTarget().toUtf8() : readAll(path));
    }
    return files;
}

// omarchy as it behaves: `theme bg set` moves the link, `font set` writes fontconfig. It logs everything.
void mimicOmarchy(const Desktop &desktop)
{
    script(desktop.home() + QStringLiteral("/bin/omarchy"),
           QStringLiteral("printf 'omarchy %s\\n' \"$*\" >> '%1'\n"
                          "if [ \"$1 $2 $3\" = 'theme bg set' ]; then ln -nsf \"$4\" \"$HOME/.local/state/omarchy/current/background\"; fi\n"
                          "if [ \"$1 $2\" = 'font set' ]; then mkdir -p \"$HOME/.config/fontconfig\"; printf '<fontconfig>%s</fontconfig>\\n' \"$3\" > \"$HOME/.config/fontconfig/fonts.conf\"; fi\n")
               .arg(desktop.log()));
}

QStringList ids(const QJsonArray &list)
{
    QStringList result;
    for (const QJsonValue &each : list)
        result << each.toObject()["id"].toString();
    return result;
}
}

class DesktopLookUiTests : public QObject {
    Q_OBJECT

    struct App {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window{workspace};
        FakeDesktop *desktop = nullptr;
        DesignController &design() { return window.agent()->designMode(); }
        App()
        {
            window.setProperty("background", true);
            auto fake = std::make_unique<FakeDesktop>();
            desktop = fake.get();
            desktop->addWindow(QStringLiteral("org.gnome.Nautilus"), QRect(100, 50, 800, 600), 777);
            desktop->shellLayers.push_back({QStringLiteral("omarchy-bar"), QRect(0, 0, 1920, 26), QStringLiteral("DP-1")});
            design().setSource(std::move(fake));
            design().start();
        }
        QJsonObject call(const QString &action, const QJsonObject &params, QString *error = nullptr)
        {
            QJsonObject result;
            const QString failure = design().run(action, params, result);
            if (error)
                *error = failure;
            else if (!failure.isEmpty())
                qWarning().noquote() << action << failure;
            return result;
        }
    };

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        SyncConfirmDialog::setResponder({});
        DesktopLookPanel::setColorResponder({});
    }

    void aPreviewShowsOnTheDesktopAndCancelWritesNothing()
    {
        Desktop desktop;
        desktop.use();
        qputenv("OMASTRATOR_RUNTIME_DIR", (desktop.home() + QStringLiteral("/runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", (desktop.home() + QStringLiteral("/runtime/app.sock")).toUtf8());
        App app;
        const QMap<QString, QByteArray> before = snapshot(desktop.home());
        QString error;
        app.call(QStringLiteral("look"), {{"op", "preview"}, {"edits", QJsonObject{{"gapsIn", 12}, {"activeBorder", "#ff375f"}}}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(desktop.logged().contains(QStringLiteral("hyprctl eval hl.config({")));
        QVERIFY(desktop.logged().contains(QStringLiteral("gaps_in = 12,")));
        QCOMPARE(app.design().lookEdits()["gapsIn"].toInt(), 12);
        // The dialog shows every file with its full path, the commands, the backup and what shows when; Cancel is the default.
        QString shown;
        bool cancelIsDefault = false;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            shown = dialog.text();
            cancelIsDefault = dialog.confirmButton() && !dialog.confirmButton()->isDefault() && !dialog.confirmButton()->autoDefault();
            return false;
        });
        const QJsonObject result = app.call(QStringLiteral("look"), {{"op", "save"}}, &error);
        QVERIFY(!result["confirmed"].toBool());
        QVERIFY(cancelIsDefault);
        QVERIFY(shown.contains(desktop.config() + QStringLiteral("/hypr/looknfeel.lua")));
        QVERIFY(shown.contains(QStringLiteral("Then runs: ") + desktop.home() + QStringLiteral("/bin/hyprctl reload config-only")));
        QVERIFY(shown.contains(QStringLiteral("Backs up: ")));
        QVERIFY(shown.contains(QStringLiteral("Note: Border colours set here stay through theme switches")));
        QCOMPARE(snapshot(desktop.home()), before);
        QVERIFY(!QFileInfo::exists(ConfigBackup::root()));
        QVERIFY(!desktop.logged().contains(QStringLiteral("reload")));
        // Discard asks Hyprland to read its unchanged files again.
        app.call(QStringLiteral("look"), {{"op", "discard"}});
        QVERIFY(desktop.logged().contains(QStringLiteral("hyprctl reload config-only")));
        QVERIFY(app.design().lookEdits().isEmpty());
        QCOMPARE(snapshot(desktop.home()), before);
    }

    void savingBacksUpFirstAndRevertPutsEveryByteBack()
    {
        Desktop desktop;
        desktop.use();
        mimicOmarchy(desktop);
        qputenv("OMASTRATOR_RUNTIME_DIR", (desktop.home() + QStringLiteral("/runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", (desktop.home() + QStringLiteral("/runtime/app.sock")).toUtf8());
        const QString second = desktop.home() + QStringLiteral("/Pictures/second.png");
        write(second, QByteArray("\x89PNG\r\n\x1a\n\0second", 15));
        App app;
        const QMap<QString, QByteArray> before = snapshot(desktop.home());
        QJsonObject edits{{"gapsOut", 20},
                          {"rounding", 4},
                          {"barHeight", 30},
                          {"textSize", 13},
                          {"font", "Berkeley Mono"},
                          {"wallpaper", second},
                          {"colors", QJsonObject{{"accent", "#ff375f"}}}};
        const bool jq = !QStandardPaths::findExecutable(QStringLiteral("jq")).isEmpty();
        if (jq)
            edits["barPosition"] = QStringLiteral("bottom");
        QString error;
        app.call(QStringLiteral("look"), {{"op", "preview"}, {"edits", edits}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // The wallpaper and the colours preview through the shell, nothing written.
        QVERIFY(desktop.logged().contains(QStringLiteral("omarchy-shell -q background set ") + second));
        QVERIFY(desktop.logged().contains(QStringLiteral("omarchy-shell shell applyTheme ")));
        QCOMPARE(snapshot(desktop.home()), before);

        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        const QJsonObject saved = app.call(QStringLiteral("look"), {{"op", "save"}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(saved["confirmed"].toBool());
        QVERIFY(readAll(desktop.config() + QStringLiteral("/hypr/looknfeel.lua")).contains("gaps_out = 20,"));
        QVERIFY(readAll(desktop.config() + QStringLiteral("/omarchy/shell.toml")).contains("base-size = 13"));
        QVERIFY(readAll(desktop.theme() + QStringLiteral("/colors.toml")).contains("accent = \"#ff375f\""));
        QCOMPARE(QFileInfo(desktop.state() + QStringLiteral("/background")).symLinkTarget(), second);
        QVERIFY(QFileInfo::exists(desktop.config() + QStringLiteral("/fontconfig/fonts.conf")));
        if (jq)
            QVERIFY(readAll(desktop.config() + QStringLiteral("/omarchy/shell.json")).contains("\"position\": \"bottom\""));
        const QString log = desktop.logged();
        for (const QString &command : {QStringLiteral("omarchy theme set graphite"), QStringLiteral("hyprctl reload config-only"),
                                       QStringLiteral("omarchy font set Berkeley Mono"), QStringLiteral("omarchy theme bg set ") + second})
            QVERIFY2(log.contains(command), qPrintable(command));
        QVERIFY(log.indexOf(QStringLiteral("theme set graphite")) < log.indexOf(QStringLiteral("theme bg set")));
        QVERIFY(app.design().lookEdits().isEmpty());

        // The backup holds what was there; Revert puts it all back, through the dialog.
        const QJsonArray history = app.call(QStringLiteral("look"), {{"op", "history"}})["history"].toArray();
        QCOMPARE(history.size(), 1);
        QCOMPARE(history[0].toObject()["id"].toString(), saved["backup"].toString());
        QString revertShown;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            revertShown = dialog.text();
            return true;
        });
        app.call(QStringLiteral("look"), {{"op", "revert"}, {"id", saved["backup"].toString()}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(revertShown.contains(desktop.config() + QStringLiteral("/fontconfig/fonts.conf (deleted)")));
        QCOMPARE(snapshot(desktop.home()), before);
        QVERIFY(!QFileInfo::exists(desktop.config() + QStringLiteral("/fontconfig/fonts.conf")));
        QCOMPARE(QFileInfo(desktop.state() + QStringLiteral("/background")).symLinkTarget(), desktop.wallpaper());
        const QString reverted = desktop.logged().mid(log.size());
        QVERIFY(reverted.contains(QStringLiteral("omarchy theme set graphite")));
        QVERIFY(reverted.contains(QStringLiteral("omarchy restart shell")));
        QVERIFY(reverted.contains(QStringLiteral("omarchy-shell -q background set ") + desktop.wallpaper()));
        QVERIFY(app.call(QStringLiteral("look"), {{"op", "history"}})["history"].toArray()[1].toObject()["reverted"].toBool());
    }

    void aFileChangedSinceThePreviewStopsTheSave()
    {
        Desktop desktop;
        desktop.use();
        qputenv("OMASTRATOR_RUNTIME_DIR", (desktop.home() + QStringLiteral("/runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", (desktop.home() + QStringLiteral("/runtime/app.sock")).toUtf8());
        App app;
        app.call(QStringLiteral("look"), {{"op", "preview"}, {"edits", QJsonObject{{"borderSize", 3}}}});
        const QString looknfeel = desktop.config() + QStringLiteral("/hypr/looknfeel.lua");
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &) {
            write(looknfeel, "-- edited meanwhile\n");
            return true;
        });
        QString error;
        app.call(QStringLiteral("look"), {{"op", "save"}}, &error);
        QVERIFY(error.contains(QStringLiteral("changed since the preview")));
        QCOMPARE(readAll(looknfeel), QByteArray("-- edited meanwhile\n"));
    }

    void theBarIsItsOwnKindAndThePanelPreviewsWhatItsControlsChange()
    {
        Desktop desktop;
        desktop.use();
        qputenv("OMASTRATOR_RUNTIME_DIR", (desktop.home() + QStringLiteral("/runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", (desktop.home() + QStringLiteral("/runtime/app.sock")).toUtf8());
        App app;
        app.call(QStringLiteral("on"), {});
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("onboarding"), {{"finish", false}});
        app.desktop->pointer = QPoint(40, 10);
        app.design().mode().poll();
        // The bar sticks to the window design mode started on until the Omarchy bar is chosen.
        app.call(QStringLiteral("home"), {{"target", app.design().mode().hover()->id}});
        QJsonObject bar = app.design().status()["bar"].toObject();
        QCOMPARE(bar["kind"].toString(), QStringLiteral("shellBar"));
        QCOMPARE(bar["label"].toString(), QStringLiteral("Desktop (DP-1)"));
        QCOMPARE(ids(bar["actions"].toArray()).first(), QStringLiteral("barLook"));
        app.call(QStringLiteral("action"), {{"id", "barLook"}, {"target", bar["target"]}});
        DesktopLookPanel *panel = app.design().lookPanel();
        QVERIFY(panel);
        QCOMPARE(panel->tabs()->currentIndex(), 1);
        QCOMPARE(panel->barList(2)->count(), 2);
        QCOMPARE(panel->field(QStringLiteral("gapsOut"))->value(), 14.0);

        // A number typed (or scrubbed) previews at once.
        NumberField *gaps = panel->field(QStringLiteral("gapsOut"));
        gaps->field->setText(QStringLiteral("24"));
        gaps->commit();
        QVERIFY(desktop.logged().contains(QStringLiteral("gaps_out = 24,")));
        // A colour picked previews at once too.
        DesktopLookPanel::setColorResponder([](const QString &, const QColor &) { return QColor(255, 0, 0); });
        panel->pickColor(QStringLiteral("activeBorder"));
        QVERIFY(desktop.logged().contains(QStringLiteral("active_border = \"rgba(ff0000ff)\"")));
        panel->pickColor(QStringLiteral("colors.accent"));
        QCOMPARE(app.design().lookEdits()["colors"].toObject()["accent"].toString(), QStringLiteral("#ff0000"));
        // Dragging a widget to another section is an edit of the layout.
        QListWidget *right = panel->barList(2);
        QListWidget *left = panel->barList(0);
        left->addItem(right->takeItem(0));
        QTRY_VERIFY(app.design().lookEdits().contains(QStringLiteral("barLayout")));
        const QJsonObject order = app.design().lookEdits()["barLayout"].toObject();
        QCOMPARE(order["left"].toArray().last().toString(), QStringLiteral("omarchy.tray"));
        QCOMPARE(order["right"].toArray().size(), 1);
        // The overlay's gap handles follow the Windows tab and the edit.
        panel->tabs()->setCurrentIndex(0);
        const QJsonObject look = app.design().status()["look"].toObject();
        QVERIFY(look["handles"].toBool());
        QCOMPARE(look["gapsOut"].toInt(), 24);
        QVERIFY(look["pending"].toBool());
        // Reading the handles leaves the edit as it was, so the next preview still validates.
        QVERIFY(!app.design().lookEdits().contains(QStringLiteral("borderSize")));
        QString previewError;
        app.call(QStringLiteral("look"), {{"op", "preview"}, {"edits", QJsonObject{{"gapsIn", 9}}}}, &previewError);
        QCOMPARE(previewError, QString());
        // Nothing reached a file.
        QCOMPARE(readAll(desktop.config() + QStringLiteral("/hypr/looknfeel.lua")), userLooknfeel);
        panel->close();
    }

    void anAppIsRestyledThroughItsToolkitWithAPreviewFromACopy()
    {
        Desktop desktop;
        desktop.use();
        qputenv("OMASTRATOR_RUNTIME_DIR", (desktop.home() + QStringLiteral("/runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", (desktop.home() + QStringLiteral("/runtime/app.sock")).toUtf8());
        // The fake app says where its config came from.
        script(desktop.home() + QStringLiteral("/bin/app"), QStringLiteral("printf 'app %s config=%s\\n' \"$*\" \"$XDG_CONFIG_HOME\" >> '%1'\n").arg(desktop.log()));
        App app;
        app.design().describeApp = [&](qint64 pid, const QString &className) {
            AppStyle::App described;
            described.pid = pid;
            described.className = className;
            described.toolkit = AppStyle::Toolkit::gtk4;
            described.command = {desktop.home() + QStringLiteral("/bin/app"), QStringLiteral("--new-window")};
            return described;
        };
        app.call(QStringLiteral("on"), {});
        app.call(QStringLiteral("tool"), {{"tool", "inspect"}});
        app.call(QStringLiteral("onboarding"), {{"finish", false}});
        app.desktop->pointer = QPoint(300, 300);
        app.design().mode().poll();
        const QJsonObject bar = app.design().status()["bar"].toObject();
        QVERIFY(ids(bar["actions"].toArray()).contains(QStringLiteral("restyleApp")));
        QString error;
        const QJsonObject described = app.call(QStringLiteral("restyle"), {{"op", "get"}, {"target", bar["target"]}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(described["toolkit"].toString(), QStringLiteral("GTK 4"));
        QVERIFY(described["honesty"].toString().contains(QStringLiteral("only when they start")));
        const QMap<QString, QByteArray> before = snapshot(desktop.home());
        const QJsonObject preview = app.call(QStringLiteral("restyle"), {{"op", "preview"}, {"style", QJsonObject{{"accent", "#ff375f"}, {"radius", 8}}}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_VERIFY(desktop.logged().contains(QStringLiteral("app --new-window config=")));
        QVERIFY(desktop.logged().contains(desktop.home() + QStringLiteral("/runtime/restyle-preview/config")));
        QVERIFY(readAll(desktop.home() + QStringLiteral("/runtime/restyle-preview/config/gtk-4.0/gtk.css")).contains("--accent-bg-color: #ff375f;"));
        QCOMPARE(snapshot(desktop.home()), before);

        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        const QJsonObject saved = app.call(QStringLiteral("restyle"), {{"op", "save"}, {"style", QJsonObject{{"accent", "#ff375f"}}}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QByteArray css = readAll(desktop.config() + QStringLiteral("/gtk-4.0/gtk.css"));
        QVERIFY(css.startsWith(before.value(QStringLiteral(".config/gtk-4.0/gtk.css"))));
        QVERIFY(css.contains("@define-color accent_bg_color #ff375f;"));
        app.call(QStringLiteral("look"), {{"op", "revert"}, {"id", saved["backup"].toString()}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(readAll(desktop.config() + QStringLiteral("/gtk-4.0/gtk.css")), before.value(QStringLiteral(".config/gtk-4.0/gtk.css")));
        app.call(QStringLiteral("restyle"), {{"op", "discard"}});
    }

    void aQtStylesheetParsesInQt()
    {
        AppStyle::Style style;
        style.accent = QColor("#ff375f");
        style.background = QColor("#1a1a1c");
        style.foreground = QColor("#e5e5e7");
        style.font = QStringLiteral("Inter");
        style.fontSize = 10;
        style.radius = 6;
        static QStringList warnings;
        warnings.clear();
        const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) { warnings << message; });
        for (const QString &role : {QString(), QStringLiteral("push button"), QStringLiteral("page tab")}) {
            style.role = role;
            QWidget widget;
            auto *button = new QPushButton(QStringLiteral("OK"), &widget);
            widget.setStyleSheet(QString::fromUtf8(AppStyle::qss(style)));
            widget.ensurePolished();
            button->ensurePolished();
        }
        qInstallMessageHandler(previous);
        for (const QString &warning : warnings)
            QVERIFY2(!warning.contains(QStringLiteral("parse"), Qt::CaseInsensitive), qPrintable(warning));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    DesktopLookUiTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}
#include "DesktopLookUiTests.moc"
