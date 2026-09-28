#pragma once
#include <QJsonValue>
#include <QPoint>
#include <QRect>
#include <QString>
#include <optional>
#include <vector>

// Hyprland's view of the desktop (docs/ANYWHERE.md): windows, monitors and the
// pointer, read from its socket, or from $OMASTRATOR_HYPRCTL in tests.
// Every rectangle is in Hyprland's global layout coordinates, logical pixels.
namespace Hyprland {
struct Window {
    QString address;
    qint64 pid = 0;
    QString className;
    QString title;
    QRect rect;
    int workspace = 0;
    QString workspaceName;
    int monitor = -1;
    bool floating = false;
    bool mapped = true;
    bool hidden = false;
    bool fullscreen = false;
    // 0 is the window with focus; higher went longer without it.
    int focusHistory = 0;
};

struct Monitor {
    int id = 0;
    QString name;
    QRect rect;
    double scale = 1;
    bool focused = false;
    // Space layer surfaces such as the bar keep at the top, in layout pixels.
    int reservedTop = 0;
    int activeWorkspace = 0;
    // A special workspace shown over the monitor, or 0.
    int specialWorkspace = 0;
    QString specialName;
};

// A layer surface: the bar, the background, the island.
struct Layer {
    QString name;
    QRect rect;
    QString monitor;
};

std::vector<Window> parseClients(const QJsonValue &json);
// `hyprctl layers -j`: every monitor's levels.
std::vector<Layer> parseLayers(const QJsonValue &json);
std::vector<Monitor> parseMonitors(const QJsonValue &json);
std::optional<QPoint> parseCursor(const QJsonValue &json);

// One query ("clients", "monitors", "cursorpos", "activewindow") as JSON; null with `error` set when Hyprland doesn't answer.
QJsonValue query(const QString &what, QString *error = nullptr);
// Runs a dispatcher: `lua` on Omarchy 4's Lua config (through `hyprctl eval`), else `legacy` (`hyprctl dispatch …`).
// Returns why it failed, or empty.
QString dispatch(const QString &lua, const QString &legacy);
// True when ~/.config/hypr/hyprland.lua exists, as setup decides.
bool usesLua();

// Shown now: on a monitor's active or special workspace, mapped and not hidden.
bool isShown(const Window &window, const std::vector<Monitor> &monitors);
// The topmost shown window under `point`: a special workspace over the rest, then floating over tiled, then focus order.
std::optional<Window> windowAt(QPoint point, const std::vector<Window> &windows, const std::vector<Monitor> &monitors);
std::optional<Monitor> monitorAt(QPoint point, const std::vector<Monitor> &monitors);
std::optional<Monitor> monitorNamed(const QString &name, const std::vector<Monitor> &monitors);
std::optional<Monitor> focusedMonitor(const std::vector<Monitor> &monitors);
}
