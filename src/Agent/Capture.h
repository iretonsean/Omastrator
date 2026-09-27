#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <utility>
#include <vector>

// The island's Capture mode (docs/OS-SUITE.md): tools that act on the whole
// screen and hand the result to the running app. Each outside program can be
// replaced for tests: $OMASTRATOR_HYPRPICKER, $OMASTRATOR_SLURP,
// $OMASTRATOR_GRIM and $OMASTRATOR_WL_PASTE.
namespace Capture {
struct Run {
    bool started = false;
    int exitCode = -1;
    QByteArray out;
    QString error;
};
// Runs `program` (by its override variable) with `args`, waiting up to `timeoutMs`.
Run run(const QString &program, const QStringList &args, int timeoutMs = 120'000);
// "hyprpicker isn't installed. Install it with: sudo pacman -S hyprpicker"
QString missing(const QString &program);

// The Omarchy theme's colours in file order, named from their keys.
std::vector<std::pair<QString, QColor>> themeColors(const QString &colorsToml);
// $OMASTRATOR_THEME_DIR, else ~/.local/state/omarchy/current/theme.
QString themeDirectory();
// "Graphite", from theme.name beside the theme folder, else "Omarchy theme".
QString themeName(const QString &directory);
// Where screenshots are kept: $XDG_DATA_HOME/omastrator/captures.
QString capturesDirectory();
// SVG text from wl-paste's types, or empty with `error` set.
QString clipboardSvg(QString *error);

// `omastrator island capture <color [fill|stroke|swatch] | screenshot | window | paste-svg | theme-swatches>`.
// `window` captures the focused window through hyprctl ($OMASTRATOR_HYPRCTL in tests).
int runCli(const QStringList &args, QTextStream &out, QTextStream &err);
}
