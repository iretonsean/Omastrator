#pragma once
#include "System/DesktopLook.h"
#include "System/SyncPlan.h"
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QString>
#include <QStringList>

// Restyling GTK and Qt apps through their toolkit (docs/ANYWHERE.md, phase 4):
// a user CSS block in ~/.config/gtk-3.0 or gtk-4.0/gtk.css for GTK, and for
// Qt either qt6ct/qt5ct's palette, font and stylesheet (when the app runs
// with that platform theme) or the app's own stylesheet, passed by its
// launcher entry. A preview relaunches the app with the change from a
// temporary folder; nothing real is written until the plan is confirmed.
namespace AppStyle {
enum class Toolkit { unknown, gtk3, gtk4, qt5, qt6 };

// From /proc/<pid>/maps: the toolkit's libraries it has loaded.
Toolkit fromMaps(const QByteArray &maps, bool *widgets = nullptr);
QString name(Toolkit toolkit);
bool isQt(Toolkit toolkit);

struct App {
    qint64 pid = 0;
    // Hyprland's class for the window.
    QString className;
    // How it was started, from /proc/<pid>/cmdline.
    QStringList command;
    Toolkit toolkit = Toolkit::unknown;
    // Qt Widgets (which stylesheets restyle) rather than only Qt Quick.
    bool widgets = true;
    // The QT_QPA_PLATFORMTHEME it runs with: "qt6ct" makes qt6ct's files count.
    QString platformTheme;
    // Its launcher entry, when one matches.
    QString desktopFile;
    // Where /proc is: tests give a folder of their own.
    static App describe(qint64 pid, const QString &className, const QString &proc = QStringLiteral("/proc"));
    // qt6ct/qt5ct decides this app's palette.
    bool usesQtct() const;
};

struct Style {
    QColor accent;
    QColor background;
    QColor foreground;
    QString font;
    double fontSize = 0;
    // -1: leave the corners alone.
    int radius = -1;
    // The kind of widget pointed at ("push button", from AT-SPI); empty: the whole window.
    QString role;

    static Style fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
    bool isEmpty() const;
};
// Why a style can't be used, or empty.
QString validate(const Style &style);

// The CSS node for an AT-SPI role ("push button" → "button"), and Qt's class ("QPushButton").
QString gtkSelector(const QString &role);
QString qtSelector(const QString &role);

// Omastrator's rules for GTK, merged into the block already in `css` (a rule per selector, the last edit
// winning); everything outside the block stays.
QByteArray withGtkStyle(const QByteArray &css, const Style &style, Toolkit toolkit);
// A Qt stylesheet for the style.
QByteArray qss(const Style &style);
// qt5ct/qt6ct's colour scheme: active, inactive and disabled colours for every palette role, from `base` with the style's colours.
QByteArray qtctScheme(const Style &style, const QPalette &base, int roles);
// qt5ct.conf or qt6ct.conf pointing at the scheme and stylesheet, with the font set; other keys stay.
QByteArray qtctConf(const QByteArray &conf, const QString &schemePath, const QString &qssPath, const Style &style);
// A launcher entry whose Exec lines pass `-stylesheet qssPath`.
QByteArray withStylesheet(const QByteArray &desktopEntry, const QString &qssPath);

// Where things go.
QString gtkCss(const DesktopLook::Paths &paths, Toolkit toolkit);
QString qtctFolder(const DesktopLook::Paths &paths, Toolkit toolkit);
// $XDG_CONFIG_HOME/omastrator/styles/<app>.qss.
QString appQss(const DesktopLook::Paths &paths, const App &app);
// The user's copy of the app's launcher entry, in $XDG_DATA_HOME/applications.
QString userDesktopFile(const DesktopLook::Paths &paths, const App &app);

// The confirmed write, with a backup and Revert.
SyncPlan plan(const App &app, const Style &style, const QPalette &base, const DesktopLook::Paths &paths);

// A second copy of the app, started with the change from `folder` (a temporary folder this makes).
struct Preview {
    QStringList command;
    // NAME=value pairs added to the app's environment.
    QStringList environment;
    QString folder;
    QString note;
};
Preview preview(const App &app, const Style &style, const QPalette &base, const DesktopLook::Paths &paths, const QString &folder, QString *error);
// What can and can't change live for this app, in plain words.
QString honesty(const App &app);
}
