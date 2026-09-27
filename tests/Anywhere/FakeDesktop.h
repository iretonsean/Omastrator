#pragma once
#include "Anywhere/DesktopSource.h"
#include <QHash>
#include <QPainter>

// A desktop for design mode's tests: two monitors, windows placed by hand, a
// pointer the test moves, an accessibility tree per app and a screen that
// grabs as flat colour. Nothing here reads the real Hyprland.
class FakeDesktop final : public DesktopSource {
public:
    FakeDesktop()
    {
        Hyprland::Monitor left;
        left.id = 0;
        left.name = QStringLiteral("DP-1");
        left.rect = QRect(0, 0, 1920, 1080);
        left.focused = true;
        left.activeWorkspace = 1;
        Hyprland::Monitor right;
        right.id = 1;
        right.name = QStringLiteral("HDMI-A-1");
        right.rect = QRect(1920, 0, 1920, 1080);
        right.activeWorkspace = 2;
        screens = {left, right};
    }

    Hyprland::Window &addWindow(const QString &className, const QRect &rect, qint64 pid, int workspace = 1, bool floating = false)
    {
        Hyprland::Window window;
        window.address = QStringLiteral("0x%1").arg(clients.size() + 1);
        window.className = className;
        window.title = className + QStringLiteral(" window");
        window.rect = rect;
        window.pid = pid;
        window.workspace = workspace;
        window.monitor = workspace == 2 ? 1 : 0;
        window.floating = floating;
        window.focusHistory = int(clients.size());
        clients.push_back(window);
        return clients.back();
    }

    std::optional<QPoint> pointer;
    std::vector<Hyprland::Window> clients;
    std::vector<Hyprland::Monitor> screens;
    // By pid: what the accessibility helper answers, in window coordinates.
    QHash<qint64, QJsonObject> accessibility;
    QColor screenColor = QColor(0x20, 0x40, 0x60);
    int accessibleCalls = 0;
    int pixelCalls = 0;
    int grabs = 0;
    bool grabFails = false;

    std::optional<QPoint> cursor() override { return pointer; }
    std::vector<Hyprland::Window> windows() override { return clients; }
    std::vector<Hyprland::Monitor> monitors() override { return screens; }
    std::optional<QJsonObject> accessible(const Hyprland::Window &window, QPoint) override
    {
        ++accessibleCalls;
        if (!accessibility.contains(window.pid))
            return std::nullopt;
        return accessibility.value(window.pid);
    }
    std::optional<QColor> pixel(QPoint) override
    {
        ++pixelCalls;
        return screenColor;
    }
    QImage grab(const QRect &rect, QString *error) override
    {
        ++grabs;
        if (grabFails) {
            if (error)
                *error = QStringLiteral("grim could not capture the screen.");
            return {};
        }
        QImage image(rect.size(), QImage::Format_ARGB32);
        image.fill(screenColor);
        return image;
    }
};
