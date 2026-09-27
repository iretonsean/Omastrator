#include "DesktopFixtures.h"
#include "System/AppStyle.h"
#include "System/ConfigBackup.h"
#include "System/DesktopLook.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// Phase 4 of docs/ANYWHERE.md, below the UI: reading Omarchy's look, the
// plans that write it (files, diffs, commands, backups), the Hyprland Lua and
// hyprlang blocks, the bar's files, GTK CSS, Qt stylesheets and qt6ct files,
// and previews that never touch the real config. All in a temporary HOME
// with fake hyprctl, omarchy and omarchy-shell.

using namespace DesktopFixtures;

namespace {
QString lua()
{
    return QStandardPaths::findExecutable(QStringLiteral("lua"));
}

// Runs `code` after a stub `hl` whose config() records the tables it's given; prints what `probe` returns.
QString runLua(const QString &code, const QString &probe)
{
    const QString program = lua();
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("snippet.lua")), code.toUtf8());
    const QString stub = QStringLiteral("seen = {} hl = { env = function() end, config = function(t) for k, v in pairs(t) do seen[k] = v end end } "
                                        "dofile('%1') io.write(tostring(%2))")
                             .arg(dir.filePath(QStringLiteral("snippet.lua")), probe);
    QProcess process;
    process.start(program, {QStringLiteral("-e"), stub});
    process.waitForFinished(5000);
    return QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError());
}

const FileWrite *writeTo(const SyncPlan &plan, const QString &path)
{
    for (const FileWrite &write : plan.writes) {
        if (write.path == path)
            return &write;
    }
    return nullptr;
}

bool pythonGtk(int version)
{
    QProcess process;
    process.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import gi; gi.require_version('Gtk', '%1.0'); from gi.repository import Gtk").arg(version)});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}

// GTK's own parser on the CSS: prints every parsing error.
QString gtkErrors(const QByteArray &css, int version)
{
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("style.css")), css);
    const QString program =
        version == 4 ? QStringLiteral("import gi, sys\ngi.require_version('Gtk', '4.0')\nfrom gi.repository import Gtk\np = Gtk.CssProvider()\n"
                                      "p.connect('parsing-error', lambda p, s, e: print(e.message))\np.load_from_path(sys.argv[1])\n")
                     : QStringLiteral("import gi, sys\ngi.require_version('Gtk', '3.0')\nfrom gi.repository import Gtk\np = Gtk.CssProvider()\n"
                                      "p.connect('parsing-error', lambda p, s, e: print(e.message))\ntry:\n    p.load_from_path(sys.argv[1])\n"
                                      "except Exception as e:\n    print(e)\n");
    QProcess process;
    process.start(QStringLiteral("python3"), {QStringLiteral("-c"), program, dir.filePath(QStringLiteral("style.css"))});
    process.waitForFinished(15000);
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
}

class DesktopLookTests : public QObject {
    Q_OBJECT

private slots:
    void theLookIsReadFromTheFilesWithoutHyprland()
    {
        Desktop desktop;
        desktop.use();
        qunsetenv("OMASTRATOR_HYPRCTL");
        const DesktopLook::Look look = DesktopLook::read(DesktopLook::Paths::current());
        // The theme's values win over Omarchy's defaults, and a group bar's gaps_out = 0 isn't taken for the windows'.
        QCOMPARE(look.gapsIn, 4);
        QCOMPARE(look.gapsOut, 8);
        QCOMPARE(look.borderSize, 1);
        QCOMPARE(look.rounding, 12);
        QCOMPARE(look.activeBorder.name(QColor::HexArgb), QStringLiteral("#b30a84ff"));
        QCOMPARE(look.barPosition, QStringLiteral("top"));
        QVERIFY(look.barTransparent);
        QCOMPARE(look.barHeight, 26);
        QCOMPARE(look.barBackground.name(), QStringLiteral("#1a1a1c"));
        QCOMPARE(look.textSize, 12);
        QCOMPARE(look.wallpaper, desktop.wallpaper());
        QCOMPARE(look.theme, QStringLiteral("graphite"));
        QCOMPARE(look.themeDirectory, desktop.theme());
        QCOMPARE(look.colors.front().first, QStringLiteral("accent"));
        QCOMPARE(look.barLayout["right"].toArray().size(), 2);
    }

    void hyprlandsLiveValuesWinWhenItAnswers()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Look look = DesktopLook::read(DesktopLook::Paths::current());
        QCOMPARE(look.gapsIn, 6);
        QCOMPARE(look.gapsOut, 14);
        QCOMPARE(look.borderSize, 3);
        QCOMPARE(look.rounding, 9);
        QCOMPARE(look.activeBorder.name(QColor::HexArgb), QStringLiteral("#b3ff375f"));
        QVERIFY(desktop.logged().contains(QStringLiteral("hyprctl -j getoption general:gaps_in")));
    }

    void gapsAndBordersMakeALuaBlockInLooknfeel()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const DesktopLook::Look now = DesktopLook::read(paths);
        const QString looknfeel = desktop.config() + QStringLiteral("/hypr/looknfeel.lua");
        const SyncPlan plan = DesktopLook::savePlan({{"gapsIn", 12}, {"activeBorder", "#ff375fcc"}, {"rounding", 6}}, now, paths);
        QVERIFY2(plan.problem.isEmpty(), qPrintable(plan.problem));
        QCOMPARE(plan.writes.size(), size_t(1));
        const FileWrite &write = plan.writes.front();
        QCOMPARE(write.path, looknfeel);
        QCOMPARE(write.before.value(), userLooknfeel);
        // The user's file is kept as it was, with the block after it.
        QVERIFY(write.after.startsWith(userLooknfeel));
        QVERIFY(write.after.contains("gaps_in = 12,"));
        QVERIFY(write.after.contains("active_border = \"rgba(ff375fcc)\","));
        QVERIFY(write.after.contains("border_active = \"rgba(ff375fcc)\","));
        QVERIFY(write.after.contains("rounding = 6"));
        QVERIFY(write.diff().contains(QStringLiteral("+ gaps_in = 12,")));
        QCOMPARE(write.summary().left(1), QStringLiteral("+"));
        QCOMPARE(plan.allCommands().size(), size_t(1));
        QCOMPARE(plan.allCommands().front(), (QStringList{desktop.home() + QStringLiteral("/bin/hyprctl"), "reload", "config-only"}));
        QVERIFY(plan.backupFolder.startsWith(desktop.home() + QStringLiteral("/.local/share/omastrator/backups/")));
        QCOMPARE(plan.revertCommands.size(), size_t(1));
        // Building the plan wrote nothing and ran nothing but queries.
        QCOMPARE(readAll(looknfeel), userLooknfeel);
        QVERIFY(!QFileInfo::exists(plan.backupFolder));
        QVERIFY(!desktop.logged().contains(QStringLiteral("reload")));
    }

    void theLuaBlockPassesLuasOwnSyntaxCheckAndSetsTheRightTable()
    {
        if (lua().isEmpty())
            QSKIP("lua isn't installed");
        const QJsonObject values{{"gapsIn", 12}, {"gapsOut", 20}, {"borderSize", 2}, {"rounding", 8}, {"activeBorder", "#ff375fcc"}, {"inactiveBorder", "#ffffff14"}};
        const QByteArray file = DesktopLook::withBlock(userLooknfeel, DesktopLook::luaBlock(values), true);
        QProcess check;
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("looknfeel.lua")), file);
        const QString luac = QStandardPaths::findExecutable(QStringLiteral("luac"));
        if (!luac.isEmpty()) {
            check.start(luac, {QStringLiteral("-p"), dir.filePath(QStringLiteral("looknfeel.lua"))});
            QVERIFY(check.waitForFinished(5000));
            QCOMPARE(check.exitCode(), 0);
        }
        const QString code = QString::fromUtf8(file);
        QCOMPARE(runLua(code, QStringLiteral("seen.general.gaps_in")), QStringLiteral("12"));
        QCOMPARE(runLua(code, QStringLiteral("seen.general.gaps_out")), QStringLiteral("20"));
        QCOMPARE(runLua(code, QStringLiteral("seen.general.col.active_border")), QStringLiteral("rgba(ff375fcc)"));
        QCOMPARE(runLua(code, QStringLiteral("seen.general.col.inactive_border")), QStringLiteral("rgba(ffffff14)"));
        QCOMPARE(runLua(code, QStringLiteral("seen.group.col.border_active")), QStringLiteral("rgba(ff375fcc)"));
        QCOMPARE(runLua(code, QStringLiteral("seen.decoration.rounding")), QStringLiteral("8"));
        // The live preview's one line, as `hyprctl eval` runs it.
        const QString eval = DesktopLook::luaEval({{"gapsOut", 30}});
        QVERIFY(!eval.contains(QLatin1Char('\n')));
        QCOMPARE(runLua(eval, QStringLiteral("seen.general.gaps_out")), QStringLiteral("30"));
    }

    void aSecondEditMergesIntoTheBlockAndKeepsTheFirst()
    {
        const QByteArray once = DesktopLook::withBlock(userLooknfeel, DesktopLook::luaBlock({{"gapsIn", 12}}), true);
        QJsonObject merged = DesktopLook::blockValues(once);
        QCOMPARE(merged["gapsIn"].toInt(), 12);
        merged["borderSize"] = 3;
        const QByteArray twice = DesktopLook::withBlock(once, DesktopLook::luaBlock(merged), true);
        QCOMPARE(twice.count("BEGIN Omastrator: desktop look"), 1);
        QVERIFY(twice.contains("gaps_in = 12,"));
        QVERIFY(twice.contains("border_size = 3,"));
        QVERIFY(twice.startsWith(userLooknfeel));
    }

    void hyprlangGetsAConfBlockAndKeywordPreviews()
    {
        Desktop desktop;
        desktop.use();
        QFile::remove(desktop.config() + QStringLiteral("/hypr/hyprland.lua"));
        write(desktop.config() + QStringLiteral("/hypr/looknfeel.conf"), "general {\n    gaps_in = 5\n}\n");
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        QVERIFY(!paths.lua());
        const DesktopLook::Look now = DesktopLook::read(paths);
        const SyncPlan plan = DesktopLook::savePlan({{"gapsOut", 18}, {"inactiveBorder", "#101010"}}, now, paths);
        QCOMPARE(plan.writes.front().path, desktop.config() + QStringLiteral("/hypr/looknfeel.conf"));
        const QByteArray after = plan.writes.front().after;
        QVERIFY(after.startsWith("general {\n    gaps_in = 5\n}\n"));
        QVERIFY(after.contains("# BEGIN Omastrator: desktop look"));
        QVERIFY(after.contains("    gaps_out = 18\n"));
        QVERIFY(after.contains("    col.inactive_border = rgba(101010ff)\n"));
        const auto preview = DesktopLook::previewCommands({{"gapsOut", 18}}, now, paths);
        QCOMPARE(preview.size(), size_t(1));
        QCOMPARE(preview.front().mid(1), (QStringList{"keyword", "general:gaps_out", "18"}));
    }

    void previewsUseHyprlandAndTheShellsIpcAndWriteNothing()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const DesktopLook::Look now = DesktopLook::read(paths);
        const QJsonObject edits{{"gapsIn", 9}, {"colors", QJsonObject{{"accent", "#ff375f"}}}, {"barHeight", 32}, {"wallpaper", desktop.wallpaper()}};
        const auto commands = DesktopLook::previewCommands(edits, now, paths);
        QCOMPARE(commands.size(), size_t(3));
        QCOMPARE(commands[0].mid(1, 1), QStringList{"eval"});
        QVERIFY(commands[0][2].contains(QStringLiteral("gaps_in = 9")));
        QCOMPARE(commands[1].mid(1, 2), (QStringList{"shell", "applyTheme"}));
        const QByteArray colors = QByteArray::fromBase64(commands[1][3].toLatin1());
        const QByteArray shell = QByteArray::fromBase64(commands[1][4].toLatin1());
        QVERIFY(colors.contains("accent = \"#ff375f\""));
        QVERIFY(colors.contains("background = \"#1a1a1c\""));
        QVERIFY(shell.contains("size-horizontal = 32"));
        QCOMPARE(commands[2].mid(1), (QStringList{"-q", "background", "set", desktop.wallpaper()}));
        const auto discard = DesktopLook::discardCommands(edits, now, paths);
        QCOMPARE(discard[0].mid(1), (QStringList{"reload", "config-only"}));
        QCOMPARE(QByteArray::fromBase64(discard[1][3].toLatin1()), colorsToml);
        // The user's shell.toml pins the text size, so it says that shows on saving.
        const QString note = DesktopLook::previewNote({{"textSize", 14}, {"gapsIn", 9}, {"barLayout", QJsonObject()}}, paths);
        QVERIFY(note.contains(QStringLiteral("Showing now: gaps, borders and corners.")));
        QVERIFY(note.contains(QStringLiteral("the text size (~/.config/omarchy/shell.toml sets them")));
        QVERIFY(note.contains(QStringLiteral("the bar's position and widget order")));
        QCOMPARE(readAll(desktop.state() + QStringLiteral("/theme/colors.toml")), colorsToml);
    }

    void theBarsLayoutIsWrittenWithJqKeepingItsOrderAndOptions()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("jq")).isEmpty())
            QSKIP("jq isn't installed");
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const DesktopLook::Look now = DesktopLook::read(paths);
        const QJsonObject order{{"left", QJsonArray{"omarchy.workspaces", "omarchy.clock"}}, {"center", QJsonArray{}}, {"right", QJsonArray{"omarchy.power", "omarchy.tray"}}};
        const SyncPlan plan = DesktopLook::savePlan({{"barPosition", "bottom"}, {"barLayout", order}, {"barHeight", 30}, {"barBackground", "#20202080"}}, now, paths);
        QVERIFY2(plan.problem.isEmpty(), qPrintable(plan.problem));
        const FileWrite *json = writeTo(plan, desktop.config() + QStringLiteral("/omarchy/shell.json"));
        QVERIFY(json);
        const QJsonObject bar = QJsonDocument::fromJson(json->after).object()["bar"].toObject();
        QCOMPARE(bar["position"].toString(), QStringLiteral("bottom"));
        const QJsonArray left = bar["layout"].toObject()["left"].toArray();
        QCOMPARE(left.size(), 2);
        // The clock moved with its options.
        QCOMPARE(left[1].toObject()["format"].toString(), QStringLiteral("h:mm AP"));
        QCOMPARE(bar["layout"].toObject()["right"].toArray()[0].toObject()["id"].toString(), QStringLiteral("omarchy.power"));
        // Keys keep their order and the escaped dash stays escaped.
        QVERIFY(json->after.indexOf("\"version\"") < json->after.indexOf("\"bar\""));
        QVERIFY(json->after.contains("\\u2014"));
        const FileWrite *toml = writeTo(plan, desktop.config() + QStringLiteral("/omarchy/shell.toml"));
        QVERIFY(toml);
        QCOMPARE(toml->after, QByteArray("[font]\nbase-size = 12\n\n[bar]\nsize-horizontal = 30\nbackground = \"#202020\"\nbackground-alpha = 0.502\n"));
        bool reload = false;
        for (const QStringList &command : plan.allCommands())
            reload = reload || command.mid(1) == QStringList{"shell", "reloadConfig"};
        QVERIFY(reload);
    }

    void tomlKeysAreSetInPlaceWithCommentsKept()
    {
        const QByteArray toml = "# Mine.\n[bar]\n# Height.\nsize-horizontal = 26 # comment\n\n[font]\nbase-size = 12\n";
        QCOMPARE(DesktopLook::setTomlKey(toml, "bar", "size-horizontal", "32"),
                 QByteArray("# Mine.\n[bar]\n# Height.\nsize-horizontal = 32 # comment\n\n[font]\nbase-size = 12\n"));
        QCOMPARE(DesktopLook::setTomlKey(toml, "bar", "text", "\"#fff\""),
                 QByteArray("# Mine.\n[bar]\n# Height.\nsize-horizontal = 26 # comment\ntext = \"#fff\"\n\n[font]\nbase-size = 12\n"));
        QCOMPARE(DesktopLook::tomlValue(toml, "font", "base-size"), QStringLiteral("12"));
        QCOMPARE(DesktopLook::setTomlKey({}, "font", "base-size", "14"), QByteArray("[font]\nbase-size = 14\n"));
    }

    void theFontGoesThroughOmarchysCommandWithItsFilesShown()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const SyncPlan plan = DesktopLook::savePlan({{"font", "Berkeley Mono"}}, DesktopLook::read(paths), paths);
        QCOMPARE(plan.allCommands().front(), (QStringList{desktop.home() + QStringLiteral("/bin/omarchy"), "font", "set", "Berkeley Mono"}));
        const FileWrite *fonts = writeTo(plan, desktop.config() + QStringLiteral("/fontconfig/fonts.conf"));
        QVERIFY(fonts && fonts->byCommand && !fonts->before);
        QVERIFY(fonts->after.contains("<string>Berkeley Mono</string>"));
        const FileWrite *alacritty = writeTo(plan, desktop.config() + QStringLiteral("/alacritty/alacritty.toml"));
        QVERIFY(alacritty && alacritty->byCommand);
        QVERIFY(alacritty->after.contains("family = \"Berkeley Mono\""));
        const FileWrite *foot = writeTo(plan, desktop.config() + QStringLiteral("/foot/foot.ini"));
        QVERIFY(foot && foot->after.contains("font=Berkeley Mono:size=9"));
        QVERIFY(fonts->summary().endsWith(QStringLiteral("(by the command)")));
        QCOMPARE(plan.revertCommands.front().mid(1), (QStringList{"restart", "shell"}));
    }

    void aWallpaperFromAnArtboardIsKeptThenLinkedByOmarchy()
    {
        Desktop desktop;
        desktop.use();
        QTemporaryDir temporary;
        const QString made = temporary.filePath(QStringLiteral("artboard-1.png"));
        write(made, QByteArray("\x89PNG\r\n\x1a\n\0made", 13));
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const SyncPlan plan = DesktopLook::savePlan({{"wallpaper", made}}, DesktopLook::read(paths), paths);
        const QString kept = desktop.home() + QStringLiteral("/.local/share/omastrator/wallpapers/artboard-1.png");
        const FileWrite *copy = writeTo(plan, kept);
        QVERIFY(copy);
        QCOMPARE(copy->copyFrom, made);
        const FileWrite *link = writeTo(plan, desktop.state() + QStringLiteral("/background"));
        QVERIFY(link && link->byCommand);
        QCOMPARE(link->linkTo, kept);
        QCOMPARE(link->linkBefore, desktop.wallpaper());
        QVERIFY(link->diff().contains(QStringLiteral("− → ") + desktop.wallpaper()));
        QCOMPARE(plan.allCommands().front().mid(1), (QStringList{"theme", "bg", "set", kept}));
        QCOMPARE(plan.revertCommands.front().mid(1), (QStringList{"-q", "background", "set", desktop.wallpaper()}));
    }

    void theThemesColoursAreChangedInTheUsersThemeAndApplied()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const SyncPlan plan = DesktopLook::savePlan({{"colors", QJsonObject{{"accent", "#ff375f"}}}}, DesktopLook::read(paths), paths);
        QVERIFY2(plan.problem.isEmpty(), qPrintable(plan.problem));
        const FileWrite *colors = writeTo(plan, desktop.theme() + QStringLiteral("/colors.toml"));
        QVERIFY(colors);
        QCOMPARE(colors->before.value(), colorsToml);
        QVERIFY(colors->after.contains("accent = \"#ff375f\""));
        QVERIFY(colors->after.contains("# Graphite."));
        QCOMPARE(plan.allCommands().front().mid(1), (QStringList{"theme", "set", "graphite"}));
        QCOMPARE(plan.revertCommands.front().mid(1), (QStringList{"theme", "set", "graphite"}));
    }

    void editsAreChecked()
    {
        QVERIFY(!DesktopLook::validate({}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"gapsIn", 900}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"activeBorder", "chartreuse-ish"}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"barPosition", "middle"}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"font", "Bad\"Font"}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"wallpaper", "/nowhere/at/all.png"}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"colors", QJsonObject{{"accent; rm", "#fff"}}}}).isEmpty());
        QVERIFY(!DesktopLook::validate({{"sudo", 1}}).isEmpty());
        QVERIFY(DesktopLook::validate({{"gapsIn", 4}, {"activeBorder", "rgba(0a84ffb3)"}}).isEmpty());
    }

    void backupsSaveBytesLinksAndAbsences()
    {
        Desktop desktop;
        desktop.use();
        const QString folder = ConfigBackup::newFolder(QStringLiteral("Test"));
        const QString missing = desktop.config() + QStringLiteral("/nothing-here.conf");
        const QString looknfeel = desktop.config() + QStringLiteral("/hypr/looknfeel.lua");
        QVERIFY(ConfigBackup::save(folder, QStringLiteral("Test"), {looknfeel, missing, desktop.state() + QStringLiteral("/background")},
                                   {{"hyprctl", "reload"}})
                    .isEmpty());
        const std::vector<ConfigBackup::Backup> all = ConfigBackup::list();
        QCOMPARE(all.size(), size_t(1));
        const ConfigBackup::Backup &backup = all.front();
        QCOMPARE(backup.entries.size(), size_t(3));
        QCOMPARE(readAll(backup.entries[0].copy), userLooknfeel);
        QVERIFY(!backup.entries[1].existed);
        QCOMPARE(backup.entries[2].linkTarget, desktop.wallpaper());
        QCOMPARE(backup.revertCommands.front(), (QStringList{"hyprctl", "reload"}));
        // The revert plan puts each back: bytes, a deletion and the link.
        write(looknfeel, "changed\n");
        write(missing, "made\n");
        QFile::remove(desktop.state() + QStringLiteral("/background"));
        QFile::link(desktop.home() + QStringLiteral("/other.png"), desktop.state() + QStringLiteral("/background"));
        const SyncPlan revert = ConfigBackup::revertPlan(backup);
        QVERIFY(revert.problem.isEmpty());
        QCOMPARE(revert.writes[0].after, userLooknfeel);
        QCOMPARE(revert.writes[0].before.value(), QByteArray("changed\n"));
        QVERIFY(revert.writes[1].remove);
        QCOMPARE(revert.writes[1].state(), QStringLiteral("removed"));
        QCOMPARE(revert.writes[2].linkTo, desktop.wallpaper());
        QCOMPARE(revert.confirmLabel, QStringLiteral("Revert"));
        QVERIFY(!revert.backupFolder.isEmpty());
    }

    void toolkitsAreKnownByTheirLibraries()
    {
        bool widgets = false;
        QCOMPARE(AppStyle::fromMaps("7f00 r-xp /usr/lib/libgtk-4.so.1.1600.0\n"), AppStyle::Toolkit::gtk4);
        QCOMPARE(AppStyle::fromMaps("7f00 r-xp /usr/lib/libgtk-3.so.0\n"), AppStyle::Toolkit::gtk3);
        QCOMPARE(AppStyle::fromMaps("/usr/lib/libQt6Core.so.6\n/usr/lib/libQt6Widgets.so.6\n", &widgets), AppStyle::Toolkit::qt6);
        QVERIFY(widgets);
        QCOMPARE(AppStyle::fromMaps("/usr/lib/libQt6Gui.so.6\n/usr/lib/libQt6Quick.so.6\n", &widgets), AppStyle::Toolkit::qt6);
        QVERIFY(!widgets);
        QCOMPARE(AppStyle::fromMaps("/usr/lib/libQt5Core.so.5\n"), AppStyle::Toolkit::qt5);
        QCOMPARE(AppStyle::fromMaps("/usr/lib/libc.so.6\n"), AppStyle::Toolkit::unknown);
    }

    void anAppIsDescribedFromProcAndItsLauncherEntry()
    {
        Desktop desktop;
        desktop.use();
        const QString proc = desktop.home() + QStringLiteral("/proc");
        write(proc + QStringLiteral("/42/cmdline"), QByteArray("/usr/bin/kcalc\0--one\0", 21));
        write(proc + QStringLiteral("/42/environ"), QByteArray("PATH=/usr/bin\0QT_QPA_PLATFORMTHEME=qt6ct\0", 41));
        write(proc + QStringLiteral("/42/maps"), "/usr/lib/libQt6Widgets.so.6\n");
        write(desktop.home() + QStringLiteral("/system/applications/org.kde.kcalc.desktop"), "[Desktop Entry]\nName=KCalc\nExec=kcalc %U\n");
        const AppStyle::App app = AppStyle::App::describe(42, QStringLiteral("org.kde.kcalc"), proc);
        QCOMPARE(app.command, (QStringList{"/usr/bin/kcalc", "--one"}));
        QCOMPARE(app.platformTheme, QStringLiteral("qt6ct"));
        QCOMPARE(app.toolkit, AppStyle::Toolkit::qt6);
        QVERIFY(app.usesQtct());
        QCOMPARE(app.desktopFile, desktop.home() + QStringLiteral("/system/applications/org.kde.kcalc.desktop"));
    }

    void gtkCssIsABlockThatGtkParsesAndLaterEditsMergeInto()
    {
        AppStyle::Style style;
        style.accent = QColor("#ff375f");
        style.role = QStringLiteral("push button");
        style.radius = 10;
        style.font = QStringLiteral("Inter");
        style.fontSize = 11;
        const QByteArray existing = "@import url(\"../../theme.css\");\n\nwindow.nautilus-window {\n  font-family: \"SF Pro Text\";\n}\n";
        const QByteArray once = AppStyle::withGtkStyle(existing, style, AppStyle::Toolkit::gtk4);
        QVERIFY(once.startsWith(existing));
        QVERIFY(once.contains("@define-color accent_bg_color #ff375f;"));
        QVERIFY(once.contains("--accent-bg-color: #ff375f;"));
        QVERIFY(once.contains("button {\n  font-family: \"Inter\";\n  font-size: 11pt;\n  border-radius: 10px;\n}"));
        AppStyle::Style second;
        second.role = QStringLiteral("push button");
        second.radius = 4;
        second.background = QColor("#222224");
        const QByteArray twice = AppStyle::withGtkStyle(once, second, AppStyle::Toolkit::gtk4);
        QCOMPARE(twice.count("BEGIN Omastrator: restyle"), 1);
        QVERIFY(twice.contains("border-radius: 4px;"));
        QVERIFY(!twice.contains("border-radius: 10px;"));
        QVERIFY(twice.contains("font-family: \"Inter\";"));
        QVERIFY(twice.contains("background-color: #222224;"));
        // GTK's own parser finds nothing wrong (the import's file doesn't exist, so it's left out).
        const QByteArray ours = twice.mid(twice.indexOf("/* BEGIN Omastrator"));
        if (pythonGtk(4))
            QCOMPARE(gtkErrors(ours, 4), QString());
        if (pythonGtk(3)) {
            AppStyle::Style three = style;
            three.foreground = QColor("#e5e5e7");
            three.role.clear();
            QCOMPARE(gtkErrors(AppStyle::withGtkStyle({}, three, AppStyle::Toolkit::gtk3), 3), QString());
        }
    }

    void qt6ctFilesAreValidAndKeepTheOtherKeys()
    {
        AppStyle::Style style;
        style.accent = QColor("#ff375f");
        style.background = QColor("#1a1a1c");
        style.foreground = QColor("#e5e5e7");
        style.font = QStringLiteral("Inter");
        style.fontSize = 10.5;
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("scheme.conf")), AppStyle::qtctScheme(style, QPalette(), int(QPalette::NColorRoles)));
        QSettings scheme(dir.filePath(QStringLiteral("scheme.conf")), QSettings::IniFormat);
        for (const char *key : {"ColorScheme/active_colors", "ColorScheme/inactive_colors", "ColorScheme/disabled_colors"}) {
            const QStringList colours = scheme.value(QLatin1String(key)).toStringList();
            QCOMPARE(colours.size(), int(QPalette::NColorRoles));
            for (const QString &colour : colours)
                QVERIFY2(QColor::isValidColorName(colour.trimmed()), qPrintable(colour));
        }
        const QStringList active = scheme.value(QStringLiteral("ColorScheme/active_colors")).toStringList();
        QCOMPARE(QColor(active[QPalette::Highlight].trimmed()), QColor("#ff375f"));
        QCOMPARE(QColor(active[QPalette::Window].trimmed()), QColor("#1a1a1c"));
        QCOMPARE(QColor(active[QPalette::WindowText].trimmed()), QColor("#e5e5e7"));
        // qt5ct's 21 roles.
        QCOMPARE(QString::fromUtf8(AppStyle::qtctScheme(style, QPalette(), 21)).section(QLatin1Char('\n'), 1, 1).count(QLatin1Char('#')), 21);

        const QByteArray conf = AppStyle::qtctConf("[Appearance]\nicon_theme=Papirus\nstyle=Fusion\n", QStringLiteral("/s/colors/Omastrator.conf"),
                                                   QStringLiteral("/s/qss/omastrator.qss"), style);
        write(dir.filePath(QStringLiteral("qt6ct.conf")), conf);
        QSettings settings(dir.filePath(QStringLiteral("qt6ct.conf")), QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("Appearance/icon_theme")).toString(), QStringLiteral("Papirus"));
        QCOMPARE(settings.value(QStringLiteral("Appearance/custom_palette")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("Appearance/color_scheme_path")).toString(), QStringLiteral("/s/colors/Omastrator.conf"));
        QCOMPARE(settings.value(QStringLiteral("Interface/stylesheets")).toString(), QStringLiteral("/s/qss/omastrator.qss"));
        QVERIFY(settings.value(QStringLiteral("Fonts/general")).toString().startsWith(QStringLiteral("Inter,10.5")));
    }

    void aQtAppsLauncherEntryPassesItsStylesheet()
    {
        const QByteArray entry = "[Desktop Entry]\nName=KCalc\nExec=kcalc %U\nTryExec=kcalc\n\n[Desktop Action New]\nExec=\"/opt/k calc\" --new\n";
        const QByteArray once = AppStyle::withStylesheet(entry, QStringLiteral("/home/u/.config/omastrator/styles/kcalc.qss"));
        QVERIFY(once.contains("Exec=kcalc -stylesheet /home/u/.config/omastrator/styles/kcalc.qss %U\n"));
        QVERIFY(once.contains("Exec=\"/opt/k calc\" -stylesheet /home/u/.config/omastrator/styles/kcalc.qss --new"));
        QVERIFY(once.contains("TryExec=kcalc\n"));
        // Again: the old argument is replaced, not repeated.
        const QByteArray twice = AppStyle::withStylesheet(once, QStringLiteral("/tmp/other.qss"));
        QCOMPARE(twice.count("-stylesheet"), 2);
        QVERIFY(twice.contains("Exec=kcalc -stylesheet /tmp/other.qss %U"));
    }

    void restylePlansNameEveryFile()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        AppStyle::Style style;
        style.accent = QColor("#ff375f");
        AppStyle::App gtk;
        gtk.className = QStringLiteral("org.gnome.Nautilus");
        gtk.toolkit = AppStyle::Toolkit::gtk4;
        gtk.command = {QStringLiteral("nautilus")};
        SyncPlan plan = AppStyle::plan(gtk, style, QPalette(), paths);
        QCOMPARE(plan.writes.size(), size_t(1));
        QCOMPARE(plan.writes.front().path, desktop.config() + QStringLiteral("/gtk-4.0/gtk.css"));
        QVERIFY(plan.note.contains(QStringLiteral("every GTK 4 app")));
        QVERIFY(!plan.backupFolder.isEmpty());

        AppStyle::App qt;
        qt.className = QStringLiteral("org.kde.kcalc");
        qt.toolkit = AppStyle::Toolkit::qt6;
        qt.command = {QStringLiteral("kcalc")};
        qt.platformTheme = QStringLiteral("gtk3");
        qt.desktopFile = desktop.home() + QStringLiteral("/system/applications/org.kde.kcalc.desktop");
        write(qt.desktopFile, "[Desktop Entry]\nExec=kcalc %U\n");
        plan = AppStyle::plan(qt, style, QPalette(), paths);
        QCOMPARE(plan.writes.size(), size_t(2));
        QCOMPARE(plan.writes[0].path, desktop.config() + QStringLiteral("/omastrator/styles/org.kde.kcalc.qss"));
        QCOMPARE(plan.writes[1].path, desktop.home() + QStringLiteral("/.local/share/applications/org.kde.kcalc.desktop"));
        QVERIFY(!plan.writes[1].before);
        QVERIFY(plan.writes[1].after.contains("Exec=kcalc -stylesheet " + plan.writes[0].path.toUtf8() + " %U"));

        qt.platformTheme = QStringLiteral("qt6ct");
        plan = AppStyle::plan(qt, style, QPalette(), paths);
        QCOMPARE(plan.writes.size(), size_t(3));
        QCOMPARE(plan.writes[2].path, desktop.config() + QStringLiteral("/qt6ct/qt6ct.conf"));

        qt.platformTheme = QStringLiteral("gtk3");
        qt.widgets = false;
        plan = AppStyle::plan(qt, style, QPalette(), paths);
        QVERIFY(plan.problem.contains(QStringLiteral("Qt Quick")));
        AppStyle::App other;
        other.className = QStringLiteral("foot");
        QVERIFY(AppStyle::plan(other, style, QPalette(), paths).problem.contains(QStringLiteral("isn't a GTK or Qt app")));
    }

    void aGtkPreviewUsesACopyOfTheConfigAndLeavesTheRealOneAlone()
    {
        Desktop desktop;
        desktop.use();
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        const QByteArray realCss = readAll(desktop.config() + QStringLiteral("/gtk-4.0/gtk.css"));
        AppStyle::App app;
        app.className = QStringLiteral("org.gnome.Nautilus");
        app.toolkit = AppStyle::Toolkit::gtk4;
        app.command = {desktop.home() + QStringLiteral("/bin/app"), QStringLiteral("--new-window")};
        AppStyle::Style style;
        style.accent = QColor("#ff375f");
        QTemporaryDir runtime;
        const QString folder = runtime.filePath(QStringLiteral("preview"));
        QString error;
        const AppStyle::Preview preview = AppStyle::preview(app, style, QPalette(), paths, folder, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(preview.command, app.command);
        QCOMPARE(preview.environment, QStringList{QStringLiteral("XDG_CONFIG_HOME=") + folder + QStringLiteral("/config")});
        const QString copy = folder + QStringLiteral("/config/gtk-4.0/gtk.css");
        QVERIFY(!QFileInfo(copy).isSymLink());
        QVERIFY(readAll(copy).contains("--accent-bg-color: #ff375f;"));
        // Its relative import still reaches the same file.
        QVERIFY(readAll(copy).contains(QUrl::fromLocalFile(desktop.home() + QStringLiteral("/theme.css")).toString().toUtf8()));
        QVERIFY(QFileInfo(folder + QStringLiteral("/config/gtk-4.0/settings.ini")).isSymLink());
        QVERIFY(QFileInfo(folder + QStringLiteral("/config/hypr")).isSymLink());
        QCOMPARE(readAll(desktop.config() + QStringLiteral("/gtk-4.0/gtk.css")), realCss);
        // A second preview clears the first without following its links into the real config.
        AppStyle::preview(app, style, QPalette(), paths, folder, &error);
        QDir(folder).removeRecursively();
        QCOMPARE(readAll(desktop.config() + QStringLiteral("/gtk-4.0/gtk.css")), realCss);
        QVERIFY(QFileInfo::exists(desktop.config() + QStringLiteral("/gtk-4.0/settings.ini")));
        QVERIFY(QFileInfo::exists(desktop.config() + QStringLiteral("/hypr/looknfeel.lua")));
    }

    void aQtPreviewPassesAStylesheetAndQtctGetsItsOwnFolder()
    {
        Desktop desktop;
        desktop.use();
        write(desktop.config() + QStringLiteral("/qt6ct/qt6ct.conf"), "[Appearance]\nstyle=Fusion\n");
        write(desktop.config() + QStringLiteral("/qt6ct/colors/Mine.conf"), "[ColorScheme]\n");
        const DesktopLook::Paths paths = DesktopLook::Paths::current();
        AppStyle::App app;
        app.className = QStringLiteral("org.kde.kcalc");
        app.toolkit = AppStyle::Toolkit::qt6;
        app.command = {QStringLiteral("kcalc")};
        AppStyle::Style style;
        style.radius = 6;
        QTemporaryDir runtime;
        QString error;
        AppStyle::Preview preview = AppStyle::preview(app, style, QPalette(), paths, runtime.filePath(QStringLiteral("a")), &error);
        QCOMPARE(preview.command.mid(0, 2), (QStringList{"kcalc", "-stylesheet"}));
        QVERIFY(readAll(preview.command[2]).contains("border-radius: 6px"));
        app.platformTheme = QStringLiteral("qt6ct");
        preview = AppStyle::preview(app, style, QPalette(), paths, runtime.filePath(QStringLiteral("b")), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(preview.environment.contains(QStringLiteral("QT_QPA_PLATFORMTHEME=qt6ct")));
        const QString inner = runtime.filePath(QStringLiteral("b/config/qt6ct"));
        QVERIFY(!QFileInfo(inner + QStringLiteral("/colors")).isSymLink());
        QVERIFY(readAll(inner + QStringLiteral("/qt6ct.conf")).contains("style=Fusion"));
        QVERIFY(readAll(inner + QStringLiteral("/qt6ct.conf")).contains(inner.toUtf8() + "/colors/Omastrator.conf"));
        // The real qt6ct folder is as it was.
        QCOMPARE(readAll(desktop.config() + QStringLiteral("/qt6ct/qt6ct.conf")), QByteArray("[Appearance]\nstyle=Fusion\n"));
        QVERIFY(!QFileInfo::exists(desktop.config() + QStringLiteral("/qt6ct/colors/Omastrator.conf")));
    }
};

QTEST_MAIN(DesktopLookTests)
#include "DesktopLookTests.moc"
