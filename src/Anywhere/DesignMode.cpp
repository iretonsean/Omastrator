#include "Anywhere/DesignMode.h"
#include "Agent/Island.h"
#include "Agent/Setup.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace {
// Browsers whose pages Omastrator can't read unless they're opened in its own.
bool isBrowser(const QString &className)
{
    static const QRegularExpression browsers(QStringLiteral("chrom|firefox|brave|vivaldi|zen|edge|librewolf|epiphany|qutebrowser"),
                                             QRegularExpression::CaseInsensitiveOption);
    return browsers.match(className).hasMatch();
}

bool sameThing(const Inspection &a, const Inspection &b)
{
    return a.surface.key == b.surface.key && a.bounds == b.bounds && a.source == b.source && a.name == b.name && a.role == b.role;
}
}

DesignMode::DesignMode(DesktopSource &source, QObject *parent) : QObject(parent), m_source(source)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &DesignMode::poll);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &DesignMode::syncIsland);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &DesignMode::syncIsland);
}

const QStringList &DesignMode::tools()
{
    static const QStringList all{QStringLiteral("point"), QStringLiteral("inspect"), QStringLiteral("pen"),  QStringLiteral("rectangle"), QStringLiteral("ellipse"),
                                 QStringLiteral("line"),    QStringLiteral("arrow"), QStringLiteral("text"),     QStringLiteral("note")};
    return all;
}

void DesignMode::followIsland()
{
    // Design mode left on by an app that didn't end cleanly doesn't come back on by itself at the next start.
    const QFileInfo state(Island::statePath());
    if (Island::read().mode == QLatin1String("design") && state.exists() && state.lastModified().secsTo(QDateTime::currentDateTime()) > 15) {
        Island::State normal = Island::read();
        normal.mode = QStringLiteral("normal");
        normal.expanded = false;
        Island::write(normal);
        Island::resetKeys();
    }
    const QString folder = QFileInfo(Island::statePath()).absolutePath();
    QDir().mkpath(folder);
    m_watcher.addPath(folder);
    syncIsland();
}

void DesignMode::syncIsland()
{
    const bool on = Island::read().mode == QLatin1String("design");
    if (on != m_on)
        setOn(on);
}

void DesignMode::setOn(bool on, const QString &monitor)
{
    if (on == m_on && (monitor.isEmpty() || monitor == m_monitor))
        return;
    m_on = on;
    m_hover.reset();
    m_anchor.reset();
    m_selected.reset();
    m_alt = false;
    m_measuring = false;
    m_cursor.reset();
    if (on) {
        refresh();
        const auto focused = monitor.isEmpty() ? Hyprland::focusedMonitor(m_monitors) : Hyprland::monitorNamed(monitor, m_monitors);
        m_monitor = focused ? focused->name : monitor;
        m_timer.start();
        Island::holdDesignKeys();
        m_source.wantAccessibility(true);
    } else {
        m_timer.stop();
        m_source.wantAccessibility(false);
        m_tool = QStringLiteral("point");
        Island::resetKeys();
    }
    // The island's mode is the switch the hotkey, the island and Esc all flip.
    Island::State state = Island::read();
    if ((state.mode == QLatin1String("design")) != on) {
        state.mode = on ? QStringLiteral("design") : QStringLiteral("normal");
        state.expanded = on;
        Island::write(state);
    }
    emit toggled(on);
    emit changed();
}

QString DesignMode::setTool(const QString &tool)
{
    if (!tools().contains(tool))
        return QStringLiteral("Choose a design tool: %1.").arg(tools().join(QStringLiteral(", ")));
    if (tool == m_tool)
        return {};
    m_tool = tool;
    // The next poll reads what's under a resting pointer at once, instead of waiting for it to move.
    m_cursor.reset();
    emit changed();
    return {};
}

namespace {
// What the bar's stickiness reads: which windows exist and are shown, where, and which one has focus.
QString desktopSignature(const std::vector<Hyprland::Window> &windows, const std::vector<Hyprland::Monitor> &monitors)
{
    QString signature;
    for (const Hyprland::Window &window : windows)
        signature += QStringLiteral("%1 %2,%3,%4,%5 %6 %7%8 %9;").arg(window.address).arg(window.rect.x()).arg(window.rect.y()).arg(window.rect.width())
                         .arg(window.rect.height()).arg(window.workspace).arg(window.mapped).arg(window.hidden).arg(window.focusHistory);
    for (const Hyprland::Monitor &monitor : monitors)
        signature += QStringLiteral("%1 %2 %3;").arg(monitor.name, QString::number(monitor.activeWorkspace), QString::number(monitor.specialWorkspace));
    return signature;
}
}

void DesignMode::refresh()
{
    m_windows = m_source.windows();
    m_monitors = m_source.monitors();
    m_sinceRefresh.start();
}

void DesignMode::watchDesktop()
{
    if (m_sinceRefresh.isValid() && m_sinceRefresh.elapsed() <= 500)
        return;
    refresh();
    // A window that moved, closed or went to another workspace matters even while the pointer rests.
    const QString now = desktopSignature(m_windows, m_monitors);
    if (now != m_signature) {
        m_signature = now;
        emit changed();
    }
}

void DesignMode::poll()
{
    // Reading a page waits in a local event loop, where the timer could fire again.
    if (!m_on || m_polling)
        return;
    m_polling = true;
    pollOnce();
    m_polling = false;
}

void DesignMode::pollOnce()
{
    watchDesktop();
    const std::optional<QPoint> cursor = m_source.cursor();
    if (!cursor)
        return;
    const bool moved = cursor != m_cursor;
    m_cursor = cursor;
    if (moved) {
        m_rest = 0;
        m_deepDone = false;
    } else if (++m_rest < 2 || m_deepDone) {
        return;
    }
    watchDesktop();
    const auto monitor = Hyprland::monitorNamed(m_monitor, m_monitors);
    if (monitor && !monitor->rect.contains(*cursor)) {
        // Design mode covers its own monitor only.
        if (m_hover) {
            m_hover.reset();
            emit changed();
        }
        return;
    }
    // Point, design mode's resting tool, inspects nothing until Alt measures: no outlines, no bar.
    if (m_tool == QLatin1String("point") && !m_alt) {
        if (m_hover) {
            m_hover.reset();
            emit changed();
        }
        return;
    }
    const bool deep = !moved;
    Inspection next = inspect(*cursor, deep);
    if (deep)
        m_deepDone = true;
    if (m_hover && sameThing(*m_hover, next) && !deep)
        return;
    if (m_hover && sameThing(*m_hover, next)) {
        // The rest only added colour or type: same thing, same id.
        next.id = m_hover->id;
        m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(), [&](const Inspection &each) { return each.id == next.id; }), m_recent.end());
        m_recent.push_back(next);
    } else {
        remember(next);
    }
    m_hover = next;
    if (m_alt && !m_anchor)
        m_anchor = m_hover;
    emit changed();
}

void DesignMode::remember(Inspection &inspection)
{
    inspection.id = m_nextId++;
    m_recent.push_back(inspection);
    while (m_recent.size() > 48)
        m_recent.pop_front();
}

std::optional<Inspection> DesignMode::target(int id) const
{
    for (auto it = m_recent.rbegin(); it != m_recent.rend(); ++it) {
        if (it->id == id)
            return *it;
    }
    if (m_selected && m_selected->id == id)
        return m_selected;
    return std::nullopt;
}

Surface DesignMode::surfaceFor(const std::optional<Hyprland::Window> &window, QPoint point, std::optional<QJsonObject> *page)
{
    Surface surface;
    const auto monitor = Hyprland::monitorAt(point, m_monitors);
    surface.monitor = monitor ? monitor->name : m_monitor;
    if (!window) {
        surface.kind = Surface::Kind::desktop;
        surface.app = surface.monitor;
        surface.rect = monitor ? monitor->rect : QRect();
        surface.key = Surface::keyFor(surface.kind, surface.app, {});
        return surface;
    }
    surface.kind = Surface::Kind::window;
    surface.app = window->className;
    surface.title = window->title;
    surface.pid = window->pid;
    surface.address = window->address;
    surface.rect = window->rect;
    if (webPage) {
        if (std::optional<QJsonObject> answer = webPage(*window, point - window->rect.topLeft())) {
            const auto web = Inspect::fromWeb(*answer, surface);
            surface.kind = Surface::Kind::web;
            surface.url = QUrl(answer->value("url").toString());
            if (web)
                surface = web->surface;
            else {
                const QJsonArray inner = answer->value("inner").toArray(), scroll = answer->value("scroll").toArray();
                surface.viewport = Inspect::viewportOrigin(surface.rect, QSizeF(inner.at(0).toDouble(), inner.at(1).toDouble()));
                surface.scroll = QPointF(scroll.at(0).toDouble(), scroll.at(1).toDouble());
            }
            if (page)
                *page = answer;
        }
    }
    if (surface.kind == Surface::Kind::window)
        surface.otherBrowser = isBrowser(surface.app);
    surface.key = Surface::keyFor(surface.kind, surface.app, surface.url);
    return surface;
}

Surface DesignMode::surfaceAt(QPoint point)
{
    refresh();
    return surfaceFor(Hyprland::windowAt(point, m_windows, m_monitors), point, nullptr);
}

Inspection DesignMode::inspect(QPoint point, bool deep)
{
    const std::optional<Hyprland::Window> window = Hyprland::windowAt(point, m_windows, m_monitors);
    std::optional<QJsonObject> page;
    const Surface surface = surfaceFor(window, point, &page);
    std::optional<Inspection> found;
    if (page)
        found = Inspect::fromWeb(*page, surface);
    if (!found && window && deep && anyPage) {
        if (const auto answer = anyPage(*window, point - window->rect.topLeft())) {
            found = Inspect::fromWeb(*answer, surface);
            // Only the Live tab's art is kept by the page; elsewhere it stays on the window.
            if (found)
                found->surface = surface;
        }
    }
    if (!found && window && deep) {
        if (const auto answer = m_source.accessible(*window, point - window->rect.topLeft()))
            found = Inspect::fromAccessible(*answer, surface);
    }
    if (!found) {
        Inspection plain;
        plain.surface = surface;
        plain.source = window ? QStringLiteral("window") : QStringLiteral("screen");
        plain.bounds = surface.rect;
        plain.role = window ? QStringLiteral("window") : QStringLiteral("desktop");
        plain.name = window ? window->title : surface.label();
        // Omarchy's bar is a layer over the desktop: it gets its own actions (docs/ANYWHERE.md, phase 4).
        if (!window) {
            for (const Hyprland::Layer &layer : m_source.layers()) {
                if (layer.name == QLatin1String("omarchy-bar") && layer.rect.contains(point)) {
                    plain.role = QStringLiteral("bar");
                    plain.name = QStringLiteral("The bar");
                    plain.bounds = layer.rect;
                }
            }
        }
        found = plain;
    }
    if (deep) {
        if (const auto color = m_source.pixel(point))
            found->pixel = *color;
    } else if (m_hover && sameThing(*m_hover, *found)) {
        found->pixel = m_hover->pixel;
    }
    return *found;
}

void DesignMode::setAlt(bool held)
{
    if (held == m_alt)
        return;
    m_alt = held;
    if (!m_measuring)
        m_anchor = held ? (m_selected ? m_selected : m_hover) : std::nullopt;
    emit changed();
}

void DesignMode::setMeasuring(int targetId, bool on)
{
    m_measuring = on;
    m_anchor = on ? target(targetId) : std::nullopt;
    if (on && !m_anchor)
        m_anchor = m_selected ? m_selected : m_hover;
    if (!on && m_alt)
        m_anchor = m_hover;
    emit changed();
}

std::vector<Inspect::Measure> DesignMode::distances() const
{
    if (!m_anchor || !m_hover || m_anchor->id == m_hover->id)
        return {};
    return Inspect::distances(m_anchor->bounds, m_hover->bounds);
}

bool DesignMode::select(int id)
{
    if (id == 0) {
        m_selected.reset();
        emit changed();
        return true;
    }
    const auto found = target(id);
    if (!found)
        return false;
    m_selected = found;
    emit changed();
    return true;
}

QJsonObject DesignMode::status() const
{
    QJsonArray lines;
    for (const Inspect::Measure &measure : distances())
        lines.append(measure.toJson());
    QJsonObject monitorRect;
    if (const auto monitor = Hyprland::monitorNamed(m_monitor, m_monitors))
        monitorRect = {{"x", monitor->rect.x()}, {"y", monitor->rect.y()}, {"width", monitor->rect.width()}, {"height", monitor->rect.height()}};
    return {{"on", m_on},
            {"monitor", m_monitor},
            {"monitorRect", monitorRect},
            {"tool", m_tool},
            // Without setup's Hyprland keys Esc never reaches design mode, so the island can't promise it.
            {"keysLoaded", Setup::designKeysLoaded(Setup::Environment::current())},
            {"hover", m_hover ? m_hover->toJson() : QJsonValue(QJsonValue::Null)},
            {"anchor", m_anchor ? m_anchor->toJson() : QJsonValue(QJsonValue::Null)},
            {"selected", m_selected ? m_selected->toJson() : QJsonValue(QJsonValue::Null)},
            {"measuring", m_measuring || m_alt},
            {"distances", lines}};
}
