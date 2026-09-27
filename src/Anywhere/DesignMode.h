#pragma once
#include "Anywhere/DesktopSource.h"
#include "Anywhere/Inspect.h"
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <deque>
#include <functional>
#include <optional>

// Design mode on any surface (docs/ANYWHERE.md): while it's on, the pointer
// is inspected where it rests. It follows the island's mode ("design"), which
// the hotkey, the island and Esc set; it covers the monitor that had focus
// when it started. Nothing here takes clicks: the overlay stays click-through.
class DesignMode : public QObject {
    Q_OBJECT
public:
    explicit DesignMode(DesktopSource &source, QObject *parent = nullptr);

    // The page under a point of `window`, when that window is Omastrator's browser: webScript's answer, else nullopt.
    std::function<std::optional<QJsonObject>(const Hyprland::Window &window, QPoint windowPoint)> webPage;

    // Watches the island's state file and turns on and off with its mode.
    void followIsland();
    bool isOn() const { return m_on; }
    // `monitor` empty: the focused one.
    void setOn(bool on, const QString &monitor = QString());
    QString monitor() const { return m_monitor; }

    // "inspect" (hover only, click-through) or a drawing tool the overlay takes the pointer for.
    static const QStringList &tools();
    QString tool() const { return m_tool; }
    // Returns why it can't, or empty.
    QString setTool(const QString &tool);

    // One look at the pointer. The timer calls it while on; tests call it after moving the fake pointer.
    void poll();
    const std::optional<Inspection> &hover() const { return m_hover; }
    // A recent inspection by id, so the bar acts on what it showed even after the pointer moved on.
    std::optional<Inspection> target(int id) const;

    // Alt: what is hovered (or selected) becomes the anchor, and distances run to whatever is hovered next.
    void setAlt(bool held);
    // The bar's Measure: the same, until turned off.
    void setMeasuring(int targetId, bool on);
    bool isMeasuring() const { return m_measuring; }
    const std::optional<Inspection> &anchor() const { return m_anchor; }
    std::vector<Inspect::Measure> distances() const;

    // Pins the bar to a recent inspection; 0 clears. Returns false for an unknown id.
    bool select(int id);
    const std::optional<Inspection> &selected() const { return m_selected; }

    // The surface under a point, for drawing: the window (its page, when it's Omastrator's browser) or the desktop.
    Surface surfaceAt(QPoint point);
    // The windows and monitors as last read.
    const std::vector<Hyprland::Window> &windows() const { return m_windows; }
    const std::vector<Hyprland::Monitor> &monitors() const { return m_monitors; }
    // Reads windows and monitors again now.
    void refresh();
    DesktopSource &source() { return m_source; }

    QJsonObject status() const;

signals:
    void changed();
    // Turned on or off.
    void toggled(bool on);

private:
    void pollOnce();
    Inspection inspect(QPoint point, bool deep);
    Surface surfaceFor(const std::optional<Hyprland::Window> &window, QPoint point, std::optional<QJsonObject> *page);
    void remember(Inspection &inspection);
    void syncIsland();

    DesktopSource &m_source;
    bool m_on = false;
    QString m_monitor;
    QString m_tool = QStringLiteral("inspect");
    QTimer m_timer;
    QFileSystemWatcher m_watcher;
    std::vector<Hyprland::Window> m_windows;
    std::vector<Hyprland::Monitor> m_monitors;
    QElapsedTimer m_sinceRefresh;
    std::optional<QPoint> m_cursor;
    // Polls the pointer has rested; the slower accessibility and colour reads wait for a rest.
    int m_rest = 0;
    bool m_deepDone = false;
    bool m_polling = false;
    std::optional<Inspection> m_hover;
    std::optional<Inspection> m_anchor;
    std::optional<Inspection> m_selected;
    bool m_alt = false;
    bool m_measuring = false;
    std::deque<Inspection> m_recent;
    int m_nextId = 1;
};
