#include "UI/DesignController.h"
#include "Agent/Island.h"
#include "Anywhere/AnywhereSettings.h"
#include "Anywhere/Bar.h"
#include "Anywhere/Desk.h"
#include "IO/ProjectStore.h"
#include "UI/AgentBridge.h"
#include "UI/ProjectWorkspace.h"
#include "UI/TaskBarActions.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

// Design mode's state, the overlays' places on screen, and the Desk
// (docs/ANYWHERE.md). The actions are in DesignController+Actions.cpp.

namespace {
// A window of the browser Live runs in. In the user's own Chromium, only the window whose title is the Live tab's:
// its other windows show other tabs.
bool showsLivePage(const LiveSession &live, const Hyprland::Window &window)
{
    const qint64 browser = live.browserProcessId();
    if (browser <= 0 || window.pid != browser)
        return false;
    return !live.inUserBrowser() || live.pageTitle().isEmpty() || window.title.startsWith(live.pageTitle());
}

QJsonArray rectArray(const QRectF &rect)
{
    const QRect whole = rect.toAlignedRect();
    return {whole.x(), whole.y(), whole.width(), whole.height()};
}

QString monitorName(int id, const std::vector<Hyprland::Monitor> &monitors)
{
    for (const Hyprland::Monitor &monitor : monitors) {
        if (monitor.id == id)
            return monitor.name;
    }
    return {};
}
}

DesignController::DesignController(AgentBridge &bridge, ProjectWorkspace &workspace, QWidget &window)
    : m_bridge(bridge), m_workspace(workspace), m_window(window)
{
    setSource(std::make_unique<SystemSource>());
    recoverLookPreview();
    m_placementTimer.setInterval(500);
    connect(&m_placementTimer, &QTimer::timeout, this, [this] { updatePlacements(); });
    connect(&m_overlays, &OverlayStore::changed, this, [this] {
        updatePlacements();
        emit changed();
    });
    // The overlay's selection and proposals change the bar.
    connect(&m_overlays.session(), &EditorSession::changed, this, &DesignController::changed);
    m_deskSave.setSingleShot(true);
    m_deskSave.setInterval(500);
    connect(&m_deskSave, &QTimer::timeout, this, [this] { autosaveDesk(); });
    // When the agent stops, the surface shows its answer (or its art again) in one step.
    connect(&m_bridge, &AgentBridge::waitingChanged, this, [this] {
        if (!m_bridge.waiting()) {
            if (!m_askFrozen.isEmpty())
                QFile::remove(m_askFrozen["png"].toString());
            m_askSurface.clear();
            m_askFrozen = {};
        }
        updatePlacements();
    });
    // The waiting line's seconds count up on the bar while the agent works on the overlay.
    connect(&m_bridge, &AgentBridge::waitingTick, this, [this] {
        if (m_bridge.designTarget() == &m_overlays.session())
            emit changed();
    });
}

DesignController::~DesignController()
{
    // The window may be going too: save what's pending without telling anyone.
    blockSignals(true);
    m_overlays.session().blockSignals(true);
    m_overlays.save();
    if (m_deskSave.isActive())
        autosaveDesk(false);
    // The app is going: no preview may stay on the desktop.
    endDesignSession();
}

void DesignController::endDesignSession()
{
    if (m_lift && m_lift->isRunning())
        m_lift->cancel();
    m_gapHandles = false;
    QJsonObject ignored;
    look({{"op", "discard"}}, ignored);
    restyle({{"op", "discard"}}, ignored);
}

void DesignController::setSource(std::unique_ptr<DesktopSource> source)
{
    m_mode.reset();
    m_source = std::move(source);
    m_mode = std::make_unique<DesignMode>(*m_source);
    connect(m_mode.get(), &DesignMode::changed, this, &DesignController::changed);
    connect(m_mode.get(), &DesignMode::toggled, this, [this](bool on) {
        m_detail.reset();
        if (on) {
            // The first run of design mode asks about the designer's work, once.
            m_onboardingOpen = AnywhereSettings::needsOnboarding();
            m_placementTimer.start();
        } else {
            m_onboardingOpen = false;
            if (!m_overlays.session().isInteracting())
                m_overlays.session().deselectAll();
            endDesignSession();
        }
        updatePlacements();
    });
    // Pages in Omastrator's browser are read through the DevTools Protocol.
    m_mode->webPage = [this](const Hyprland::Window &window, QPoint windowPoint) -> std::optional<QJsonObject> {
        LiveSession &live = m_bridge.liveSession();
        if (!showsLivePage(live, window))
            return std::nullopt;
        QString error;
        const QJsonValue answer = live.evaluate(Inspect::webScript(windowPoint, window.rect.size()), &error);
        if (!error.isEmpty() || !answer.isObject())
            return std::nullopt;
        return answer.toObject();
    };
}

void DesignController::start()
{
    if (m_started)
        return;
    m_started = true;
    m_settings = AnywhereSettings::read();
    m_overlays.load();
    m_mode->followIsland();
    m_placementTimer.start();
    updatePlacements();
}

void DesignController::say(const QString &line)
{
    m_message = line;
    emit changed();
}

std::optional<Surface> DesignController::locate(const QString &key)
{
    const QString kind = key.section(QLatin1Char(':'), 0, 0);
    const QString rest = key.section(QLatin1Char(':'), 1);
    const auto &windows = m_mode->windows();
    const auto &monitors = m_mode->monitors();
    if (kind == QLatin1String("desktop")) {
        const auto monitor = Hyprland::monitorNamed(rest, monitors);
        if (!monitor)
            return std::nullopt;
        Surface surface;
        surface.kind = Surface::Kind::desktop;
        surface.app = rest;
        surface.monitor = rest;
        surface.rect = monitor->rect;
        surface.key = key;
        return surface;
    }
    const Hyprland::Window *best = nullptr;
    for (const Hyprland::Window &window : windows) {
        if (!Hyprland::isShown(window, monitors))
            continue;
        const bool matches = kind == QLatin1String("window") ? window.className == rest : showsLivePage(m_bridge.liveSession(), window);
        if (matches && (!best || window.focusHistory < best->focusHistory))
            best = &window;
    }
    if (!best)
        return std::nullopt;
    Surface surface;
    surface.kind = kind == QLatin1String("web") ? Surface::Kind::web : Surface::Kind::window;
    surface.app = best->className;
    surface.title = best->title;
    surface.pid = best->pid;
    surface.address = best->address;
    surface.rect = best->rect;
    surface.monitor = monitorName(best->monitor, monitors);
    surface.key = key;
    if (surface.kind == Surface::Kind::web) {
        // The page must still be the one the art was drawn on; its scroll moves the art.
        const std::optional<QJsonObject> page = m_mode->webPage ? m_mode->webPage(*best, QPoint(0, 0)) : std::nullopt;
        if (!page)
            return std::nullopt;
        surface.url = QUrl(page->value("url").toString());
        if (Surface::keyFor(Surface::Kind::web, QString(), surface.url) != key)
            return std::nullopt;
        const QJsonArray inner = page->value("inner").toArray(), scroll = page->value("scroll").toArray();
        surface.viewport = Inspect::viewportOrigin(surface.rect, QSizeF(inner.at(0).toDouble(), inner.at(1).toDouble()));
        surface.scroll = QPointF(scroll.at(0).toDouble(), scroll.at(1).toDouble());
    }
    return surface;
}

void DesignController::updatePlacements()
{
    if (!m_started)
        return;
    const QStringList keys = m_overlays.surfaces();
    QJsonArray list;
    // While the agent works on a surface, it shows as it was, marked as being worked on.
    const bool asking = !m_askSurface.isEmpty() && m_bridge.waiting() && m_bridge.designTarget() == &m_overlays.session();
    if (asking && !m_askFrozen.isEmpty()) {
        QJsonObject frozen = m_askFrozen;
        frozen["working"] = true;
        list.append(frozen);
    }
    if (!keys.isEmpty()) {
        // While design mode is on its poll keeps the windows fresh.
        if (!m_mode->isOn())
            m_mode->refresh();
        const QString selected = m_overlays.selectedSurface();
        for (const QString &key : keys) {
            if (asking && key == m_askSurface)
                continue;
            const std::optional<Surface> surface = locate(key);
            if (!surface)
                continue;
            double scale = 1;
            if (const auto monitor = Hyprland::monitorNamed(surface->monitor, m_mode->monitors()))
                scale = monitor->scale;
            const auto picture = m_overlays.picture(key, scale);
            if (!picture)
                continue;
            const QRectF placed = picture->bounds.translated(surface->origin());
            list.append(QJsonObject{{"key", key},
                                    {"label", surface->label()},
                                    {"png", picture->path},
                                    {"version", picture->version},
                                    {"rect", rectArray(placed)},
                                    {"monitor", surface->monitor},
                                    {"selected", key == selected}});
        }
    }
    if (list != m_placements) {
        m_placements = list;
        emit changed();
    }
}

std::optional<DesignController::Target> DesignController::target(const QJsonObject &params, QString *error)
{
    auto failed = [&](const QString &message) -> std::optional<Target> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const int id = params["target"].toInt();
    if (id > 0) {
        if (const auto found = m_mode->target(id))
            return Target{found, QString()};
        return failed(QStringLiteral("That's no longer under the pointer. Point at it again."));
    }
    const QString surface = params["surface"].toString();
    if (!surface.isEmpty()) {
        if (m_overlays.art(surface).empty())
            return failed(QStringLiteral("There's no art on that surface."));
        return Target{std::nullopt, surface};
    }
    if (const QString art = m_overlays.selectedSurface(); !art.isEmpty())
        return Target{std::nullopt, art};
    if (m_mode->selected())
        return Target{m_mode->selected(), QString()};
    if (m_mode->hover())
        return Target{m_mode->hover(), QString()};
    return failed(QStringLiteral("Point at something first."));
}

QString DesignController::kindOf(const Target &target) const
{
    if (!target.artSurface.isEmpty()) {
        const QString art = TaskBarActions::kind(m_overlays.session());
        return QStringLiteral("art:") + (art.isEmpty() ? QStringLiteral("objects") : art);
    }
    // Omarchy's bar is a layer over the desktop, with its own actions.
    if (target.inspection->surface.kind == Surface::Kind::desktop && target.inspection->role == QLatin1String("bar"))
        return QStringLiteral("shellBar");
    return Bar::kindOf(Surface::kindName(target.inspection->surface.kind), target.inspection->surface.otherBrowser);
}

QString DesignController::surfaceKeyOf(const Target &target) const
{
    return target.artSurface.isEmpty() ? target.inspection->surface.key : target.artSurface;
}

Surface DesignController::surfaceOf(const Target &target) const
{
    if (target.inspection)
        return target.inspection->surface;
    if (const auto found = const_cast<DesignController *>(this)->locate(target.artSurface))
        return *found;
    // Not on screen now: known by its key alone.
    Surface surface;
    const QString kind = target.artSurface.section(QLatin1Char(':'), 0, 0);
    surface.kind = kind == QLatin1String("web") ? Surface::Kind::web : kind == QLatin1String("window") ? Surface::Kind::window : Surface::Kind::desktop;
    surface.app = target.artSurface.section(QLatin1Char(':'), 1);
    surface.url = QUrl(surface.app);
    surface.key = target.artSurface;
    return surface;
}

QJsonObject DesignController::status()
{
    QJsonObject status = m_mode->status();
    status["overlays"] = m_placements;
    // Where each monitor sits in Hyprland's layout, so the overlay places art and the bar on the right one.
    QJsonArray monitors;
    for (const Hyprland::Monitor &monitor : m_mode->monitors())
        monitors.append(QJsonObject{{"name", monitor.name},
                                    {"x", monitor.rect.x()},
                                    {"y", monitor.rect.y()},
                                    {"width", monitor.rect.width()},
                                    {"height", monitor.rect.height()},
                                    {"reservedTop", monitor.reservedTop}});
    status["monitors"] = monitors;
    status["message"] = m_message;
    const AnywhereSettings::Answers answers = AnywhereSettings::Answers::fromJson(m_settings["onboarding"].toObject());
    QJsonObject onboarding{{"open", m_onboardingOpen}, {"needed", !answers.done}, {"answers", answers.toJson()}};
    if (m_onboardingOpen) {
        QJsonArray questions;
        for (const AnywhereSettings::Question &question : AnywhereSettings::questions())
            questions.append(QJsonObject{{"id", question.id}, {"text", question.text}, {"multiple", question.multiple}, {"options", question.options}});
        onboarding["questions"] = questions;
        onboarding["note"] = AnywhereSettings::privacyNote();
    }
    status["onboarding"] = onboarding;
    if (m_detail)
        status["detail"] = m_detail->toJson();
    EditorSession &overlay = m_overlays.session();
    if (m_bridge.hasProposalIn(overlay))
        status["proposal"] = QJsonObject{{"title", m_bridge.proposalTitle()}, {"summary", m_bridge.proposalSummary()}};
    if (m_bridge.designTarget() == &overlay && m_bridge.waiting())
        status["waiting"] = m_bridge.waitingText();
    status["canUndo"] = overlay.canUndo();
    status["look"] = lookStatus();
    if (m_lift && m_lift->isRunning())
        status["lift"] = m_lift->status();

    // The floating bar: next to the selected art, else what's pinned, else what's hovered.
    std::optional<Target> shown;
    const QString art = m_overlays.selectedSurface();
    if (!art.isEmpty() && m_mode->isOn())
        shown = Target{std::nullopt, art};
    else if (m_mode->selected())
        shown = Target{m_mode->selected(), QString()};
    else if (m_mode->hover() && m_mode->tool() == QLatin1String("inspect"))
        shown = Target{m_mode->hover(), QString()};
    if (shown && m_mode->isOn()) {
        const QString kind = kindOf(*shown);
        const QString key = surfaceKeyOf(*shown);
        QRectF bounds;
        QString label;
        if (shown->inspection) {
            bounds = shown->inspection->bounds;
            label = shown->inspection->surface.label();
        } else {
            const Surface surface = surfaceOf(*shown);
            label = surface.label();
            bounds = overlay.selectionBounds(true).translated(surface.origin());
        }
        const QString chosen = m_settings["destinations"].toObject()[key].toString();
        QJsonArray destinations;
        const bool web = key.startsWith(QLatin1String("web:"));
        const bool source = web && !m_bridge.liveSession().project().isEmpty();
        static const QHash<QString, QString> names{{"overlay", "Keep on Overlay"}, {"desk", "Desk"}, {"document", "New Document"},
                                                   {"source", "Apply to Source"}, {"agent", "Hand to Agent"}};
        for (const QString &destination : AnywhereSettings::destinations()) {
            QJsonObject entry{{"id", destination}, {"label", names.value(destination)}};
            if (destination == QLatin1String("source") && !source) {
                entry["enabled"] = false;
                entry["tip"] = QStringLiteral("Only pages open in Omastrator's browser with their code on this machine take changes.");
            }
            destinations.append(entry);
        }
        QJsonArray actions = Bar::actions(kind);
        // A traced lift can be handed to the agent to redraw cleanly.
        if (!shown->inspection && overlay.selection().size() == 1) {
            const VectorObject *picked = overlay.document()->find(overlay.selection().front());
            if (picked && picked->liftedFrom == QLatin1String("trace"))
                actions.insert(0, QJsonObject{{"id", "cleanUp"}, {"label", "Ask Agent to Clean Up"},
                                              {"tip", "The agent redraws the traced shapes as clean vectors, as a preview to keep or discard"}});
        }
        status["bar"] = QJsonObject{{"kind", kind},
                                    {"target", shown->inspection ? shown->inspection->id : 0},
                                    {"surface", key},
                                    {"label", label},
                                    {"bounds", rectArray(bounds)},
                                    {"actions", actions},
                                    {"suggestions", Bar::suggestions(kind, answers)},
                                    {"placeholder", Bar::askPlaceholder(kind)},
                                    {"destination", AnywhereSettings::destinations().contains(chosen) ? chosen : QStringLiteral("overlay")},
                                    {"destinations", destinations},
                                    {"art", int(m_overlays.art(key).size())}};
    }
    return status;
}

ProjectTab *DesignController::deskTab(QString *error)
{
    const QString path = Desk::defaultPath();
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        if (tab->path && QFileInfo(*tab->path) == QFileInfo(path))
            return tab.get();
    }
    VectorDocument document = Desk::blank();
    if (QFileInfo::exists(path)) {
        try {
            document = ProjectStore::read(path);
        } catch (const FileError &failure) {
            if (error)
                *error = QStringLiteral("The Desk couldn't be read: %1").arg(failure.message());
            return nullptr;
        }
    }
    // Opening the Desk behind an open window leaves the tab in front where it was.
    const QUuid previous = m_workspace.selectedID();
    const bool keepFront = m_window.isVisible() && m_workspace.current().session.hasDocument();
    ProjectTab &tab = m_workspace.addTab(false, QStringLiteral("Desk"));
    tab.session.loadDocument(document);
    tab.path = path;
    if (keepFront)
        m_workspace.select(previous);
    disconnect(m_deskWatch);
    m_deskWatch = connect(&tab.session, &EditorSession::documentChanged, &m_deskSave, qOverload<>(&QTimer::start));
    if (!QFileInfo::exists(path))
        autosaveDesk();
    return &tab;
}

void DesignController::autosaveDesk(bool mark)
{
    const QString path = Desk::defaultPath();
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        if (!tab->path || QFileInfo(*tab->path) != QFileInfo(path) || !tab->session.hasDocument() || tab->session.isInteracting())
            continue;
        // The Desk keeps itself: there is no Save to forget.
        try {
            QDir().mkpath(QFileInfo(path).absolutePath());
            ProjectStore::write(*tab->session.document(), path);
            if (mark)
                tab->session.markSaved();
        } catch (const FileError &failure) {
            say(QStringLiteral("The Desk couldn't be saved: %1").arg(failure.message()));
        }
    }
}

QString DesignController::desk(const QString &how)
{
    QString error;
    ProjectTab *tab = deskTab(&error);
    if (!tab)
        return error;
    const bool deskInFront = m_window.isVisible() && m_workspace.selectedID() == tab->id;
    const QString workspace = AnywhereSettings::deskWorkspace();
    const bool special = workspace.startsWith(QLatin1String("special:"));
    const QString name = special ? workspace.mid(8) : workspace;
    auto specialShown = [&] {
        m_mode->refresh();
        return std::any_of(m_mode->monitors().begin(), m_mode->monitors().end(),
                           [&](const Hyprland::Monitor &monitor) { return monitor.specialName == workspace; });
    };
    auto toggleSpecial = [&] {
        return Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.workspace.toggle_special(\"%1\"))").arg(name),
                                  QStringLiteral("togglespecialworkspace %1").arg(name));
    };
    if (how == QLatin1String("hide") || (how == QLatin1String("toggle") && deskInFront)) {
        if (special && specialShown())
            return toggleSpecial();
        if (deskInFront)
            m_window.hide();
        return {};
    }
    m_workspace.select(tab->id);
    if (how == QLatin1String("window")) {
        m_bridge.bringForward();
        return {};
    }
    // Its own workspace: go there, then map the window again so it opens there.
    QString failure;
    if (special) {
        if (!specialShown())
            failure = toggleSpecial();
    } else {
        failure = Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.focus({ workspace = \"%1\" }))").arg(name),
                                     QStringLiteral("workspace %1").arg(name));
    }
    if (m_window.isVisible())
        m_window.hide();
    m_bridge.bringForward();
    // Without Hyprland the Desk still opens, as a window where you are.
    Q_UNUSED(failure)
    return {};
}
