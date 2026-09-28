#pragma once
#include "Agent/Hyprland.h"
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <optional>
#include <vector>

// Where design mode learns about the screen. The system one asks Hyprland,
// AT-SPI and grim; tests give a fake one, so they never read the real desktop.
class DesktopSource {
public:
    virtual ~DesktopSource() = default;
    virtual std::optional<QPoint> cursor() = 0;
    virtual std::vector<Hyprland::Window> windows() = 0;
    virtual std::vector<Hyprland::Monitor> monitors() = 0;
    // The accessible object at a point inside `window` (window coordinates), as Inspect::accessibleAt answers.
    virtual std::optional<QJsonObject> accessible(const Hyprland::Window &window, QPoint windowPoint) = 0;
    // The screen's colour at a point.
    virtual std::optional<QColor> pixel(QPoint point) = 0;
    // The shell's layer surfaces (the bar, the background), for telling the bar from the desktop.
    virtual std::vector<Hyprland::Layer> layers() { return {}; }
    // Asks apps to publish their accessibility trees while Inspect runs (Qt apps only do
    // when the a11y bus says so), and puts the bus back as it was after.
    virtual void wantAccessibility(bool on) { Q_UNUSED(on) }
    // A screenshot of `rect`, for Capture to Desk; null with `error` set when it can't.
    virtual QImage grab(const QRect &rect, QString *error) = 0;
    // The window's whole accessibility tree (Lift::accessibleTree's answer); nullopt with `error` when it has none.
    virtual std::optional<QJsonObject> accessibleTree(const Hyprland::Window &window, int maxNodes, QString *error)
    {
        Q_UNUSED(window)
        Q_UNUSED(maxNodes)
        if (error)
            *error = QStringLiteral("This app has no accessibility tree.");
        return std::nullopt;
    }
};

class SystemSource final : public DesktopSource {
public:
    std::optional<QPoint> cursor() override;
    std::vector<Hyprland::Window> windows() override;
    std::vector<Hyprland::Monitor> monitors() override;
    std::optional<QJsonObject> accessible(const Hyprland::Window &window, QPoint windowPoint) override;
    std::optional<QColor> pixel(QPoint point) override;
    std::vector<Hyprland::Layer> layers() override;
    QImage grab(const QRect &rect, QString *error) override;
    std::optional<QJsonObject> accessibleTree(const Hyprland::Window &window, int maxNodes, QString *error) override;
    void wantAccessibility(bool on) override;
    ~SystemSource() override { wantAccessibility(false); }

private:
    // Whether the bus's IsEnabled was turned on here, so turning off leaves others' setting alone.
    bool m_enabledAccessibility = false;
};
