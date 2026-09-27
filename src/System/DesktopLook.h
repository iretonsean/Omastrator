#pragma once
#include "System/SyncPlan.h"
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// Omarchy's own look, edited visually in design mode (docs/ANYWHERE.md,
// phase 4): window gaps and borders, the bar, the font, the wallpaper and the
// theme's colours. An edit is a JSON object of the keys below. It previews
// live where Omarchy allows (hyprctl, the shell's applyTheme, the background
// plugin), and is written only through a confirmed plan that backs up every
// file it touches first.
//
// Keys: gapsIn, gapsOut, borderSize, rounding (numbers); activeBorder,
// inactiveBorder ("#rrggbbaa" or "#rrggbb"); barPosition (top, bottom, left,
// right), barTransparent (bool), barHeight, barBackground, barText, barLayout
// ({left, center, right}: widget ids in order); font (a family), textSize;
// wallpaper (an image's path); colors ({colors.toml key: "#rrggbb"}).
namespace DesktopLook {
struct Paths {
    QString home;
    // $XDG_CONFIG_HOME, else ~/.config.
    QString config;
    // $XDG_DATA_HOME, else ~/.local/share.
    QString data;
    // Omarchy's current theme and background: ~/.local/state/omarchy/current.
    QString state;
    // $OMARCHY_PATH, else /usr/share/omarchy.
    QString omarchy;

    static Paths current();
    // Omarchy 4's Lua config: hypr/hyprland.lua exists.
    bool lua() const;
    // hypr/looknfeel.lua (Lua), else hypr/looknfeel.conf, else hypr/hyprland.conf.
    QString looknfeel() const;
    QString shellJson() const;
    // The machine-level override the shell watches: ~/.config/omarchy/shell.toml.
    QString userShellToml() const;
    QString themeDirectory() const;
    QString themeName() const;
    QString background() const;
    QString fontsConf() const;
    // Where wallpapers made from artboards go: $XDG_DATA_HOME/omastrator/wallpapers.
    QString wallpapers() const;
};

// $OMASTRATOR_HYPRCTL, else hyprctl; $OMASTRATOR_OMARCHY_SHELL, else omarchy-shell.
QString hyprctl();
QString omarchyShell();

struct Look {
    int gapsIn = 5;
    int gapsOut = 10;
    int borderSize = 2;
    int rounding = 0;
    QColor activeBorder;
    QColor inactiveBorder;
    QString barPosition = QStringLiteral("top");
    bool barTransparent = false;
    int barHeight = 26;
    QColor barBackground;
    QColor barText;
    QJsonObject barLayout;
    QString font;
    int textSize = 12;
    QString wallpaper;
    QString theme;
    QString themeDirectory;
    // colors.toml's colours by key, in the file's order.
    std::vector<std::pair<QString, QColor>> colors;

    QJsonObject toJson() const;
};

// The desktop as it is: Hyprland's live values when it answers (`askHyprland`), else the config files.
Look read(const Paths &paths, bool askHyprland = true);
// The monospace families fontconfig knows (fc-list), sorted; empty without fc-list.
QStringList fontFamilies();

// Checks an edit; returns why it can't be used, or empty.
QString validate(const QJsonObject &edits);
// "rgba(0a84ffb3)", as Hyprland and Omarchy's themes write colours.
QString hyprColor(const QColor &color);
// The first colour of Hyprland's "rgba(…)", "rgb(…)", "0xAARRGGBB", a gradient or getoption's "b30a84ff 0deg".
std::optional<QColor> parseHyprColor(const QString &text);
QColor parseColor(const QString &text);

// The Omastrator block in hypr/looknfeel.lua (or .conf): every Hyprland key given, merged with the block
// already there, so earlier edits stay.
QByteArray luaBlock(const QJsonObject &values);
QByteArray confBlock(const QJsonObject &values);
// The Hyprland values the file's Omastrator block holds now.
QJsonObject blockValues(const QByteArray &file);
// `file` with the block replaced, or added at the end.
QByteArray withBlock(const QByteArray &file, const QByteArray &block, bool lua);
// One Lua statement setting the Hyprland keys live, for `hyprctl eval`.
QString luaEval(const QJsonObject &values);

// A TOML file with `section.key` set to `value` (already TOML: "\"#fff\"" or "26"), comments and order kept.
QByteArray setTomlKey(const QByteArray &toml, const QString &section, const QString &key, const QString &value);
// The TOML value of `section.key`, unquoted, or empty.
QString tomlValue(const QByteArray &toml, const QString &section, const QString &key);
// shell.json's bar layout with widgets moved to `order` ({left, center, right} of ids); options stay with each widget.
QJsonObject reorderLayout(const QJsonObject &layout, const QJsonObject &order);

// What `omarchy font set` will write, predicted from its script, for the dialog and the backup.
std::vector<FileWrite> fontWrites(const Paths &paths, const QString &family);

// The commands that show an edit on the desktop now without writing a file, and those that take it back.
std::vector<QStringList> previewCommands(const QJsonObject &edits, const Look &now, const Paths &paths);
std::vector<QStringList> discardCommands(const QJsonObject &edits, const Look &now, const Paths &paths);
// What of the edit shows live, and what only once it's saved, in plain words.
QString previewNote(const QJsonObject &edits, const Paths &paths);

// The confirmed write: files, their diffs, commands, a backup and what Revert runs.
SyncPlan savePlan(const QJsonObject &edits, const Look &now, const Paths &paths);
}
