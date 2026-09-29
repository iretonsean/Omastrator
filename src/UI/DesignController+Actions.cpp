#include "System/SiteExtract.h"
#include "Agent/AgentLauncher.h"
#include "Agent/Capture.h"
#include "Agent/Island.h"
#include "UI/DesktopLookPanel.h"
#include "Anywhere/AnywhereSettings.h"
#include "Document/PathOperations.h"
#include "UI/AgentBridge.h"
#include "UI/DesignController.h"
#include "UI/PageWorkspaces.h"
#include "UI/ProjectWorkspace.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>

// The `design` method's actions (docs/ANYWHERE.md): drawing on the overlay,
// the floating bar's actions, Ask, where work goes, onboarding and the Desk.

namespace {
QString rectText(const QRect &rect)
{
    return QStringLiteral("x %1, y %2, %3 × %4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

// The inspected element's styles as CSS, for Copy CSS.
QString cssOf(const Inspection &inspection)
{
    QStringList lines;
    auto add = [&](const QString &property, const QString &value) {
        if (!value.isEmpty() && value != QLatin1String("normal") && value != QLatin1String("0px") && value != QLatin1String("none"))
            lines << QStringLiteral("  %1: %2;").arg(property, value);
    };
    add(QStringLiteral("width"), QStringLiteral("%1px").arg(inspection.bounds.width()));
    add(QStringLiteral("height"), QStringLiteral("%1px").arg(inspection.bounds.height()));
    if (inspection.color.isValid())
        add(QStringLiteral("color"), inspection.color.name());
    if (inspection.background.isValid() && inspection.background.alpha() > 0)
        add(QStringLiteral("background"), inspection.background.name());
    if (!inspection.fontFamily.isEmpty())
        add(QStringLiteral("font-family"), QStringLiteral("\"%1\"").arg(inspection.fontFamily));
    if (inspection.fontSize > 0)
        add(QStringLiteral("font-size"), QStringLiteral("%1px").arg(inspection.fontSize));
    add(QStringLiteral("font-weight"), inspection.fontWeight);
    static const QList<std::pair<const char *, const char *>> styles{{"lineHeight", "line-height"},    {"letterSpacing", "letter-spacing"},
                                                                     {"padding", "padding"},          {"margin", "margin"},
                                                                     {"borderRadius", "border-radius"}, {"border", "border"},
                                                                     {"gap", "gap"}};
    for (const auto &[key, property] : styles)
        add(QLatin1String(property), inspection.styles[QLatin1String(key)].toString());
    const QString selector = inspection.name.isEmpty() ? QStringLiteral(".element") : inspection.name;
    return QStringLiteral("%1 {\n%2\n}\n").arg(selector, lines.join(QLatin1Char('\n')));
}

QString copyToClipboard(const QString &text)
{
    const QString program = qEnvironmentVariable("OMASTRATOR_WL_COPY", QStringLiteral("wl-copy"));
    QProcess process;
    process.start(program, {});
    if (!process.waitForStarted(3000))
        return Capture::missing(QStringLiteral("wl-copy")).replace(QStringLiteral("-S wl-copy"), QStringLiteral("-S wl-clipboard"));
    process.write(text.toUtf8());
    process.closeWriteChannel();
    process.waitForFinished(3000);
    return {};
}
}

QString DesignController::run(const QString &action, const QJsonObject &params, QJsonObject &result)
{
    if (!m_started)
        start();
    m_message.clear();
    EditorSession &overlay = m_overlays.session();
    if (action == QLatin1String("status")) {
        result = status();
        return {};
    }
    if (action == QLatin1String("on") || (action == QLatin1String("toggle") && !m_mode->isOn())) {
        m_mode->setOn(true, params["monitor"].toString());
        result["on"] = true;
        return {};
    }
    if (action == QLatin1String("off") || action == QLatin1String("toggle")) {
        m_mode->setOn(false);
        result["on"] = false;
        return {};
    }
    if (action == QLatin1String("tool")) {
        if (const QString failure = m_mode->setTool(params["tool"].toString()); !failure.isEmpty())
            return failure;
        // A drawing tool is design mode's; choosing one starts it.
        if (!m_mode->isOn())
            m_mode->setOn(true);
        result["tool"] = m_mode->tool();
        return {};
    }
    if (action == QLatin1String("reset")) {
        // The escape hatch: everything Omastrator put on the screen or holds goes, whatever state it was in.
        // The agent's work for the overlay stops, so nothing arrives after the screen is clear.
        if (m_bridge.designTarget() == &overlay && m_bridge.waiting())
            m_bridge.stopWaiting();
        if (m_bridge.hasProposalIn(overlay))
            m_bridge.discardProposal();
        endDesignSession();
        if (m_lookPanel)
            m_lookPanel->close();
        m_onboardingOpen = false;
        const QStringList surfaces = m_overlays.surfaces();
        for (const QString &key : surfaces)
            m_overlays.clear(key);
        m_overlays.save();
        m_mode->setOn(false);
        // The pages' workspaces go back to the desktop, and stay given back until View turns them on again.
        const bool hadPages = PageWorkspaces::isTurnedOn();
        PageWorkspaces::setTurnedOn(false);
        Island::resetKeys();
        // Live in the user's own tab takes that page's clicks: the tab goes back to them. Omastrator's own browser stays.
        if (m_bridge.liveSession().inUserBrowser())
            m_bridge.liveSession().stop();
        if (hadPages) {
            say(QStringLiteral("Reset. Pages as Workspaces is off. Turn it on again from View."));
            return {};
        }
        say(surfaces.isEmpty() ? QStringLiteral("Reset. Design mode is off and nothing is left on the screen.")
                               : QStringLiteral("Reset. Design mode is off and the drawings are cleared; Undo on the island brings them back."));
        return {};
    }
    if (action == QLatin1String("barFollowsFocus")) {
        const bool follows = params["on"].toBool(true);
        if (const QString failure = AnywhereSettings::setBarFollowsFocus(follows); !failure.isEmpty())
            return failure;
        m_settings = AnywhereSettings::read();
        result["barFollowsFocus"] = follows;
        emit changed();
        return {};
    }
    if (action == QLatin1String("home")) {
        // Explicitly moves the bar to the window pointed at (or `target`'s), where it then sticks.
        QString error;
        const std::optional<Target> chosen = target(params, &error);
        if (!chosen)
            return error;
        setHome(surfaceOf(*chosen));
        m_barHover = chosen->inspection;
        emit changed();
        return {};
    }
    if (action == QLatin1String("alt")) {
        m_mode->setAlt(params["on"].toBool(true));
        return {};
    }
    if (action == QLatin1String("select")) {
        if (!m_mode->select(params["target"].toInt()))
            return QStringLiteral("That's no longer under the pointer. Point at it again.");
        if (!overlay.isInteracting())
            overlay.deselectAll();
        return {};
    }
    if (action == QLatin1String("selectArt")) {
        std::vector<QUuid> ids;
        for (const QJsonValue &id : params["ids"].toArray())
            ids.push_back(QUuid(id.toString()));
        if (overlay.isInteracting())
            return QStringLiteral("Keep or discard the preview on the overlay first.");
        if (!m_overlays.selectArt(params["surface"].toString(), ids))
            return QStringLiteral("There's no art on that surface.");
        m_mode->select(0);
        return {};
    }
    if (action == QLatin1String("deselect")) {
        if (!overlay.isInteracting())
            overlay.deselectAll();
        m_mode->select(0);
        m_detail.reset();
        emit changed();
        return {};
    }
    if (action == QLatin1String("measure")) {
        m_mode->setMeasuring(params["target"].toInt(), params["on"].toBool(true));
        return {};
    }
    if (action == QLatin1String("draw"))
        return draw(params, result);
    if (action == QLatin1String("keep") || action == QLatin1String("discard")) {
        if (!m_bridge.hasProposalIn(overlay))
            return QStringLiteral("There's no preview on the overlay.");
        if (action == QLatin1String("keep"))
            m_bridge.keepProposal();
        else
            m_bridge.discardProposal();
        m_overlays.save();
        return {};
    }
    if (action == QLatin1String("undo") || action == QLatin1String("redo")) {
        if (overlay.isInteracting())
            return QStringLiteral("Keep or discard the preview on the overlay first.");
        const bool undo = action == QLatin1String("undo");
        if (undo ? !overlay.canUndo() : !overlay.canRedo())
            return undo ? QStringLiteral("There's nothing on the overlay to undo.") : QStringLiteral("There's nothing on the overlay to redo.");
        result["step"] = undo ? overlay.undoName() : overlay.redoName();
        undo ? overlay.undo() : overlay.redo();
        return {};
    }
    if (action == QLatin1String("clear")) {
        if (overlay.isInteracting())
            return QStringLiteral("Keep or discard the preview on the overlay first.");
        // "all" takes every surface's art away: the one step back to a clean screen.
        const QString surface = params["surface"].toString();
        const QStringList keys = surface == QLatin1String("all") ? m_overlays.surfaces() : QStringList{surface};
        // A lift still running would put its result back after the screen was cleared.
        if (surface == QLatin1String("all") && m_lift && m_lift->isRunning())
            m_lift->cancel();
        for (const QString &key : keys)
            m_overlays.clear(key);
        return {};
    }
    if (action == QLatin1String("onboarding"))
        return onboarding(params, result);
    if (action == QLatin1String("desk"))
        return desk(params["how"].toString(QStringLiteral("show")));
    if (action == QLatin1String("lift"))
        return startLift(params, result);
    if (action == QLatin1String("look"))
        return look(params, result);
    if (action == QLatin1String("restyle"))
        return restyle(params, result);

    QString error;
    const std::optional<Target> chosen = target(params, &error);
    if (!chosen)
        return error;
    if (action == QLatin1String("action"))
        return this->action(params["id"].toString(), *chosen, result);
    if (action == QLatin1String("ask"))
        return ask(params["prompt"].toString().trimmed(), *chosen, result);
    if (action == QLatin1String("send"))
        return send(params["destination"].toString(), *chosen, params["prompt"].toString().trimmed(), result);
    if (action == QLatin1String("handoff"))
        return handOff(*chosen, params, result);
    return QStringLiteral("There is no design action “%1”.").arg(action);
}

QString DesignController::draw(const QJsonObject &params, QJsonObject &result)
{
    const QString tool = params["tool"].toString(m_mode->tool());
    if (tool == QLatin1String("inspect") || tool == QLatin1String("point") || !DesignMode::tools().contains(tool))
        return QStringLiteral("Draw with pen, rectangle, ellipse, line, arrow, text or note.");
    OverlayStore::Stroke stroke;
    stroke.tool = tool;
    stroke.text = params["text"].toString();
    for (const QJsonValue &value : params["points"].toArray()) {
        const QJsonArray pair = value.toArray();
        if (pair.size() != 2)
            return QStringLiteral("Points are [x, y] pairs on screen.");
        stroke.points.emplace_back(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    if (stroke.points.empty())
        return QStringLiteral("Nothing was drawn.");
    const Surface surface = m_mode->surfaceAt(stroke.points.front().toPoint());
    QString error;
    const QUuid id = m_overlays.draw(surface, stroke, &error);
    if (id.isNull())
        return error;
    m_mode->select(0);
    result = {{"id", id.toString(QUuid::WithoutBraces)}, {"surface", surface.key}, {"label", surface.label()},
              {"step", m_overlays.session().undoName()}};
    updatePlacements();
    return {};
}

QString DesignController::action(const QString &id, const Target &target, QJsonObject &result)
{
    if (id == QLatin1String("handToAgent"))
        return handOff(target, {}, result);
    if (!target.artSurface.isEmpty())
        return artAction(id, target.artSurface);
    const Inspection &inspection = *target.inspection;
    if (id == QLatin1String("inspect")) {
        m_detail = inspection;
        result["detail"] = inspection.toJson();
        result["css"] = cssOf(inspection);
        emit changed();
        return {};
    }
    if (id == QLatin1String("copyCss")) {
        if (const QString failure = copyToClipboard(cssOf(m_detail ? *m_detail : inspection)); !failure.isEmpty())
            return failure;
        say(QStringLiteral("CSS copied."));
        return {};
    }
    if (id == QLatin1String("closeDetail")) {
        m_detail.reset();
        emit changed();
        return {};
    }
    if (id == QLatin1String("lift"))
        return startLift(QJsonObject{{"target", inspection.id}}, result);
    if (id == QLatin1String("mockup")) {
        m_mode->setTool(QStringLiteral("rectangle"));
        m_mode->select(inspection.id);
        say(QStringLiteral("Draw the mock-up over it. Inspect gives the pointer back."));
        return {};
    }
    if (id == QLatin1String("measure")) {
        const bool on = !(m_mode->isMeasuring() && m_mode->anchor() && m_mode->anchor()->id == inspection.id);
        m_mode->setMeasuring(inspection.id, on);
        if (on)
            say(QStringLiteral("Point at something else to measure the distance."));
        return {};
    }
    if (id == QLatin1String("capture"))
        return captureToDesk(target, result);
    // Changing the real thing: Omarchy's look, and the app's toolkit.
    if (id == QLatin1String("desktopLook") || id == QLatin1String("gapsAndBorders") || id == QLatin1String("barLook")
        || id == QLatin1String("restyleApp")) {
        static const QHash<QString, QString> sections{{"desktopLook", "windows"}, {"gapsAndBorders", "windows"}, {"barLook", "bar"}, {"restyleApp", "app"}};
        openLookPanel(sections.value(id), inspection);
        return {};
    }
    if (id == QLatin1String("sendDesk"))
        return send(QStringLiteral("desk"), target, QString(), result);
    if (id == QLatin1String("openInBrowser"))
        return m_bridge.live(QStringLiteral("start"), {}, result);
    if (id == QLatin1String("extractSystem")) {
        LiveSession &live = m_bridge.liveSession();
        if (live.state() != LiveSession::State::running || live.pageSession().isEmpty())
            return QStringLiteral("Open the page in Omastrator's browser to extract its design system.");
        QString error;
        const QJsonObject scan = SiteExtract::scan(live.browser(), live.pageSession(), &error);
        if (!error.isEmpty())
            return error;
        m_bridge.showWindow({}, true);
        // The proposal waits for confirmation in the Design System panel.
        emit m_bridge.designSystemRequested(nullptr, scan, inspection.surface.url.toString());
        return {};
    }
    return QStringLiteral("There is no bar action “%1”.").arg(id);
}

QString DesignController::artAction(const QString &id, const QString &surface)
{
    EditorSession &overlay = m_overlays.session();
    if (overlay.isInteracting())
        return QStringLiteral("Keep or discard the preview on the overlay first.");
    // Acts on the selected art when it's this surface's, else on all of it.
    if (m_overlays.selectedSurface() != surface)
        m_overlays.selectArt(surface);
    if (id == QLatin1String("group")) {
        if (!overlay.canGroup())
            return QStringLiteral("Select two or more shapes to group.");
        overlay.groupSelection();
    } else if (id == QLatin1String("ungroup")) {
        if (!overlay.canUngroup())
            return QStringLiteral("Select a group to ungroup.");
        overlay.ungroupSelection();
    } else if (id == QLatin1String("unite")) {
        if (!overlay.canCombine())
            return QStringLiteral("Select two or more paths to unite.");
        overlay.combineSelection(BooleanOperation::unite);
    } else if (id == QLatin1String("createOutlines")) {
        overlay.convertTextToPaths();
    } else if (id == QLatin1String("imageTrace")) {
        overlay.traceSelectedImage();
    } else if (id == QLatin1String("releaseClippingMask")) {
        overlay.releaseClippingMask();
    } else if (id == QLatin1String("duplicate")) {
        overlay.duplicateSelection(QPointF(16, 16));
    } else if (id == QLatin1String("delete")) {
        overlay.deleteSelection();
    } else if (id == QLatin1String("undo")) {
        if (!overlay.canUndo())
            return QStringLiteral("There's nothing on the overlay to undo.");
        overlay.undo();
    } else if (id == QLatin1String("cleanUp")) {
        // A traced lift is rough: the agent redraws it as a preview to keep or discard.
        QJsonObject ignored;
        return ask(QStringLiteral("This selected group was traced from a screenshot of an app, so its shapes are rough. Redraw it as clean "
                                  "UI vectors in the same place: straight edges, true rectangles with consistent corner radii, real text "
                                  "objects for any words you can read, and flat colours matching the originals. Replace the traced group."),
                   Target{std::nullopt, surface}, ignored);
    } else if (id == QLatin1String("makeComponent")) {
        if (!overlay.makeComponent())
            return QStringLiteral("Select the art to make a component of.");
    } else if (id == QLatin1String("designSystem")) {
        m_bridge.showWindow({}, true);
        emit m_bridge.designSystemRequested(&overlay, {}, {});
    } else if (id == QLatin1String("sendDesk")) {
        QJsonObject ignored;
        return send(QStringLiteral("desk"), Target{std::nullopt, surface}, QString(), ignored);
    } else {
        return QStringLiteral("There is no action “%1” for art.").arg(id);
    }
    return {};
}

QString DesignController::keepScreenshot(const QRect &rect, QImage *image)
{
    if (rect.isEmpty())
        return {};
    QString error;
    const QImage grabbed = m_source->grab(rect, &error);
    if (grabbed.isNull())
        return {};
    // Captures stay on this machine, in Omastrator's own folder.
    const QString folder = Capture::capturesDirectory();
    QDir().mkpath(folder);
    const QString path = QDir(folder).filePath(QStringLiteral("design-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
    if (!grabbed.save(path, "PNG"))
        return {};
    if (image)
        *image = grabbed;
    return path;
}

QString DesignController::ask(const QString &prompt, const Target &target, QJsonObject &result)
{
    if (prompt.isEmpty())
        return QStringLiteral("Say what you'd like first.");
    EditorSession &overlay = m_overlays.session();
    if (overlay.isInteracting())
        return QStringLiteral("Keep or discard the preview on the overlay first.");
    const Surface surface = surfaceOf(target);
    QString context = QStringLiteral("Surface: %1, %2").arg(surface.label(), Surface::kindName(surface.kind));
    if (surface.kind == Surface::Kind::web)
        context += QStringLiteral(" page %1").arg(surface.url.toString());
    else if (surface.kind == Surface::Kind::window)
        context += QStringLiteral(" (class %1, title “%2”)").arg(surface.app, surface.title);
    context += QStringLiteral(".\n");
    QRect shot;
    const bool art = !target.artSurface.isEmpty();
    if (target.inspection) {
        const Inspection &inspection = *target.inspection;
        const QRect local = inspection.bounds.translated(-surface.origin().toPoint());
        context += QStringLiteral("Pointed at: %1 (%2), in the overlay's coordinates at %3.\n")
                       .arg(inspection.summary(), inspection.source, rectText(local));
        if (inspection.color.isValid())
            context += QStringLiteral("Text colour %1. ").arg(inspection.color.name());
        if (inspection.background.isValid())
            context += QStringLiteral("Background %1. ").arg(inspection.background.name());
        if (!inspection.fontFamily.isEmpty())
            context += QStringLiteral("Font %1 %2 at %3 px. ").arg(inspection.fontFamily, inspection.fontWeight).arg(inspection.fontSize);
        if (!inspection.text.isEmpty())
            context += QStringLiteral("Its text: “%1”.").arg(inspection.text);
        shot = inspection.bounds;
    } else {
        shot = surface.rect;
        context += QStringLiteral("The user selected %1 object(s) of art drawn on this surface.").arg(overlay.selection().size());
    }
    // The agent's art lands on this surface's layer.
    const QUuid layer = m_overlays.ensureLayer(surface);
    if (art) {
        if (m_overlays.selectedSurface() != surface.key)
            m_overlays.selectArt(surface.key);
    } else {
        overlay.deselectAll();
    }
    overlay.setActiveLayer(layer);
    const QString screenshot = keepScreenshot(shot.intersected(surface.rect.isEmpty() ? shot : surface.rect));
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // The art stays in view as it was while the agent works, instead of each half-made edit showing (strokes vanishing
    // before their replacements arrive). A copy, since the live picture's file is replaced as the overlay changes.
    m_askSurface = surface.key;
    m_askFrozen = {};
    for (const QJsonValue &placed : m_placements) {
        QJsonObject object = placed.toObject();
        if (object["key"].toString() != surface.key)
            continue;
        const QString copy = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("overlays/asking-%1.png").arg(requestId.left(8)));
        QDir().mkpath(QFileInfo(copy).absolutePath());
        if (QFile::copy(object["png"].toString(), copy)) {
            object["png"] = copy;
            m_askFrozen = object;
        }
    }
    if (const QString failure = m_bridge.askOnOverlay(overlay, requestId, AgentLauncher::surfacePrompt(requestId, prompt, context, screenshot, art));
        !failure.isEmpty()) {
        m_askSurface.clear();
        m_askFrozen = {};
        return failure;
    }
    result["requestId"] = requestId;
    say(QStringLiteral("Asked. It stays as it is until the answer is ready to keep or discard."));
    return {};
}

Desk::Frame DesignController::frameFor(const Target &target, bool screenshot, QString *error)
{
    const QString key = surfaceKeyOf(target);
    Surface surface = surfaceOf(target);
    m_mode->refresh();
    const std::optional<Surface> now = locate(key);
    if (now)
        surface = *now;
    Desk::Frame frame;
    frame.source = surface.label();
    // The part of the surface the frame shows: a page's viewport, a window, a monitor.
    QRect region = surface.rect;
    if (surface.kind == Surface::Kind::web && now)
        region = QRect(surface.viewport, surface.rect.bottomRight());
    std::vector<QUuid> ids;
    if (!target.artSurface.isEmpty() && m_overlays.selectedSurface() == key)
        ids = m_overlays.session().selection();
    VectorDocument art = m_overlays.extract(key, ids);
    if (now && !region.isEmpty()) {
        const QPointF offset = surface.origin() - QPointF(region.topLeft());
        if (!offset.isNull() && !art.layers().empty()) {
            for (const QUuid &root : art.children(art.layers().front()))
                art.transform(root, QTransform::fromTranslate(offset.x(), offset.y()), false, false);
        }
        if (screenshot) {
            QImage image;
            keepScreenshot(region, &image);
            if (!image.isNull()) {
                frame.screenshot = image;
                frame.size = region.size();
            } else if (error) {
                *error = QStringLiteral("The screen couldn't be captured, so only the art was sent.");
            }
        }
    }
    frame.art = art;
    return frame;
}

QString DesignController::captureToDesk(const Target &target, QJsonObject &result)
{
    QString warning;
    Desk::Frame frame = frameFor(target, true, &warning);
    if (frame.screenshot.isNull()) {
        // Capture without a picture is only worth it with art.
        if (frame.art.layers().empty() || frame.art.children(frame.art.layers().front()).empty())
            return warning.isEmpty() ? QStringLiteral("The screen couldn't be captured. Is grim installed? sudo pacman -S grim") : warning;
    }
    ProjectTab *tab = deskTab();
    if (!tab)
        return QStringLiteral("The Desk couldn't be opened.");
    QString error;
    const QUuid id = Desk::addFrame(tab->session, frame, &error);
    if (id.isNull())
        return error;
    result["frame"] = id.toString(QUuid::WithoutBraces);
    result["label"] = Desk::label(frame.source, frame.time);
    say(QStringLiteral("Sent to the Desk: %1").arg(result["label"].toString()));
    return {};
}

QString DesignController::send(const QString &destination, const Target &target, const QString &prompt, QJsonObject &result)
{
    if (!AnywhereSettings::destinations().contains(destination))
        return QStringLiteral("Send work to one of: %1.").arg(AnywhereSettings::destinations().join(QStringLiteral(", ")));
    const QString key = surfaceKeyOf(target);
    const Surface surface = surfaceOf(target);
    // The choice is remembered as this surface's default.
    if (const QString failure = AnywhereSettings::setDestination(key, destination); !failure.isEmpty())
        return failure;
    m_settings = AnywhereSettings::read();
    result["destination"] = destination;
    if (destination == QLatin1String("overlay")) {
        say(QStringLiteral("Kept on the overlay."));
        emit changed();
        return {};
    }
    if (destination == QLatin1String("agent")) {
        if (prompt.isEmpty()) {
            result["needsPrompt"] = true;
            say(QStringLiteral("Say what the agent should do with it."));
            return {};
        }
        return ask(prompt, target, result);
    }
    if (destination == QLatin1String("desk")) {
        QString warning;
        Desk::Frame frame = frameFor(target, true, &warning);
        ProjectTab *tab = deskTab();
        if (!tab)
            return QStringLiteral("The Desk couldn't be opened.");
        QString error;
        const QUuid id = Desk::addFrame(tab->session, frame, &error);
        if (id.isNull())
            return error;
        result["frame"] = id.toString(QUuid::WithoutBraces);
        result["label"] = Desk::label(frame.source, frame.time);
        say(warning.isEmpty() ? QStringLiteral("Sent to the Desk: %1").arg(result["label"].toString()) : warning);
        return {};
    }
    if (destination == QLatin1String("document")) {
        if (m_workspace.isManaging())
            return QStringLiteral("Omastrator is showing a dialog. Try again when it's answered.");
        Desk::Frame frame = frameFor(target, true, nullptr);
        QSizeF size = frame.size;
        const std::vector<QUuid> roots = frame.art.layers().empty() ? std::vector<QUuid>{} : frame.art.children(frame.art.layers().front());
        if (size.isEmpty() && !roots.empty()) {
            const QRectF bounds = frame.art.bounds(roots, true);
            size = QSizeF(std::max(100.0, bounds.right() + 20), std::max(100.0, bounds.bottom() + 20));
        }
        if (size.isEmpty())
            return QStringLiteral("There's nothing on this surface to put in a document yet.");
        VectorDocument document = VectorDocument::blank(size);
        const QUuid layer = document.layers().front();
        document.find(layer)->name = surface.label();
        if (!frame.screenshot.isNull()) {
            VectorObject shot;
            shot.kind = ObjectKind::image;
            shot.name = QStringLiteral("Screenshot");
            shot.image = frame.screenshot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            shot.transform = QTransform::fromScale(size.width() / frame.screenshot.width(), size.height() / frame.screenshot.height());
            document.insert(shot, layer);
        }
        for (const QUuid &root : roots) {
            std::vector<VectorObject> copies = frame.art.copySubtree(root);
            if (copies.empty())
                continue;
            copies.front().parentID = layer;
            for (VectorObject &copy : copies)
                document.objects.push_back(std::move(copy));
        }
        ProjectTab &tab = m_workspace.addTab(false, surface.label());
        tab.session.loadDocument(document);
        tab.session.markUnsaved();
        m_bridge.bringForward();
        result["document"] = tab.title();
        say(QStringLiteral("Opened as a new document."));
        return {};
    }
    // Source: only a page Live has the code for can take it.
    LiveSession &live = m_bridge.liveSession();
    const bool ownsPage = surface.kind == Surface::Kind::web && live.state() == LiveSession::State::running && !live.project().isEmpty()
                          && Surface::keyFor(Surface::Kind::web, QString(), live.url()) == key;
    if (!ownsPage)
        return QStringLiteral("Only pages open in Omastrator's browser with their code on this machine take changes. Send it to the Desk, "
                              "a document or the agent instead.");
    // Lifted art that was changed goes back as page edits, then into the code like any Live edit.
    const std::vector<QUuid> roots = !target.artSurface.isEmpty() && m_overlays.selectedSurface() == key ? m_overlays.session().selection()
                                                                                                        : m_overlays.art(key);
    bool applied = false;
    if (const QString failure = applyLifted(key, roots, &applied, result); !failure.isEmpty() || applied)
        return failure;
    const auto picture = m_overlays.picture(key, 2);
    QString instruction = prompt.isEmpty() ? QStringLiteral("Make the page match the mock-up the user drew over it.") : prompt;
    if (picture)
        instruction += QStringLiteral(" The mock-up is %1, drawn over the page at %2 in page coordinates (CSS pixels).")
                           .arg(picture->path, rectText(picture->bounds.toAlignedRect()));
    QString requestId;
    if (const QString failure = m_bridge.liveAsk(instruction, {}, &requestId); !failure.isEmpty())
        return failure;
    result["requestId"] = requestId;
    say(QStringLiteral("The agent is changing the page's code. Review changes shows what it did."));
    return {};
}

QString DesignController::onboarding(const QJsonObject &params, QJsonObject &result)
{
    if (params.contains("question")) {
        QStringList values;
        for (const QJsonValue &value : params["values"].toArray())
            values << value.toString();
        if (const QString failure = AnywhereSettings::setAnswer(params["question"].toString(), values); !failure.isEmpty())
            return failure;
    }
    if (params.contains("finish")) {
        if (const QString failure = AnywhereSettings::finish(!params["finish"].toBool()); !failure.isEmpty())
            return failure;
        m_onboardingOpen = false;
    }
    if (params.contains("open"))
        m_onboardingOpen = params["open"].toBool();
    m_settings = AnywhereSettings::read();
    result["answers"] = AnywhereSettings::answers().toJson();
    result["open"] = m_onboardingOpen;
    emit changed();
    return {};
}
