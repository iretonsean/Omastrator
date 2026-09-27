#include "Anywhere/AnywhereSettings.h"
#include "UI/AgentBridge.h"
#include "UI/DesignController.h"
#include "UI/ProjectWorkspace.h"
#include <QJsonArray>

// Lift into vectors from the floating bar (docs/ANYWHERE.md): the job runs in
// the background with its progress on the bar, then lands on the overlay, the
// Desk or a new document as one undo step, "Lift <thing>".

namespace {
// Lifted art lands on the overlay, the Desk or a document; the other destinations apply work, so the overlay stands in.
QString liftDestination(const QString &chosen)
{
    return chosen == QLatin1String("desk") || chosen == QLatin1String("document") ? chosen : QStringLiteral("overlay");
}

// The art moved so its top-left is 0,0, for a frame or a document of its own.
VectorDocument normalized(const Lift::Result &result, QSizeF *size)
{
    VectorDocument art = result.art;
    const QRectF bounds = art.bounds(result.root, true);
    art.transform(result.root, QTransform::fromTranslate(-bounds.left(), -bounds.top()), false, false);
    art.size = bounds.size().expandedTo(QSizeF(1, 1));
    if (size)
        *size = art.size;
    return art;
}
}

QString DesignController::startLift(const QJsonObject &params, QJsonObject &result)
{
    if (params["cancel"].toBool()) {
        if (!m_lift || !m_lift->isRunning())
            return QStringLiteral("Nothing is being lifted.");
        m_lift->cancel();
        return {};
    }
    if (m_lift && m_lift->isRunning())
        return QStringLiteral("Already lifting %1. Wait for it, or cancel it from the bar.").arg(m_lift->label());
    if (m_overlays.session().isInteracting())
        return QStringLiteral("Keep or discard the preview on the overlay first.");

    Surface surface;
    std::optional<Inspection> inspection;
    QRect region;
    const QJsonArray regionArray = params["region"].toArray();
    if (regionArray.size() == 4) {
        region = QRect(regionArray.at(0).toInt(), regionArray.at(1).toInt(), regionArray.at(2).toInt(), regionArray.at(3).toInt()).normalized();
        if (region.width() < 4 || region.height() < 4)
            return QStringLiteral("Drag out a larger region to lift.");
        m_mode->refresh();
        surface = m_mode->surfaceAt(region.center());
    } else {
        QString error;
        const std::optional<Target> chosen = target(params, &error);
        if (!chosen)
            return error;
        if (!chosen->inspection)
            return QStringLiteral("Lift works on what's on screen. Point at a page element or a window.");
        inspection = chosen->inspection;
        surface = inspection->surface;
    }

    // Where it lands: asked for now, else this surface's remembered choice.
    const QString asked = params["to"].toString();
    if (!asked.isEmpty()) {
        if (!QStringList{"overlay", "desk", "document"}.contains(asked))
            return QStringLiteral("Lifted art goes to the overlay, the Desk or a document.");
        if (const QString failure = AnywhereSettings::setDestination(surface.key, asked); !failure.isEmpty())
            return failure;
        m_settings = AnywhereSettings::read();
    }
    m_liftDestination = liftDestination(asked.isEmpty() ? AnywhereSettings::destination(surface.key) : asked);
    m_liftSurface = surface;

    if (surface.kind == Surface::Kind::web) {
        LiveSession &live = m_bridge.liveSession();
        if (live.state() != LiveSession::State::running)
            return QStringLiteral("This page isn't open in Omastrator's browser any more.");
        const bool element = inspection && inspection->source == QLatin1String("dom");
        // The page's viewport coordinates.
        const QRect box = element ? inspection->bounds : region;
        const QRectF viewportRect = box.isEmpty() ? QRectF() : QRectF(box.translated(-surface.viewport));
        const QString label = element ? inspection->name : surface.label();
        m_lift = LiftJob::web(live, element, viewportRect, label);
    } else if (surface.kind == Surface::Kind::window) {
        const auto &windows = m_mode->windows();
        const auto found = std::find_if(windows.begin(), windows.end(), [&](const Hyprland::Window &window) {
            return (!surface.address.isEmpty() && window.address == surface.address) || (surface.address.isEmpty() && window.className == surface.app);
        });
        if (found == windows.end())
            return QStringLiteral("That window is gone.");
        // An accessible widget lifts just that widget; a window, all of it.
        QRect local;
        if (!region.isEmpty())
            local = region.intersected(found->rect).translated(-found->rect.topLeft());
        else if (inspection && inspection->source == QLatin1String("accessibility"))
            local = inspection->bounds.translated(-found->rect.topLeft());
        const QString label = inspection && inspection->source == QLatin1String("accessibility") && !inspection->name.isEmpty()
                                  ? inspection->name
                                  : surface.label();
        m_lift = LiftJob::screen(*m_source, *found, local, label);
    } else {
        const QRect rect = region.isEmpty() ? surface.rect : region;
        m_lift = LiftJob::trace(*m_source, rect, surface.origin(), surface.label());
    }
    connect(m_lift.get(), &LiftJob::progressed, this, &DesignController::changed);
    connect(m_lift.get(), &LiftJob::finished, this, [this] { QMetaObject::invokeMethod(this, &DesignController::landLift, Qt::QueuedConnection); });
    m_lift->start();
    result["lifting"] = m_lift->label();
    result["destination"] = m_liftDestination;
    say(QStringLiteral("Lifting %1…").arg(m_lift->label()));
    return {};
}

void DesignController::landLift()
{
    if (!m_lift || m_lift->isRunning())
        return;
    std::unique_ptr<LiftJob> job = std::move(m_lift);
    if (!job->result()) {
        say(job->wasCancelled() ? QStringLiteral("Lift cancelled.") : job->error());
        return;
    }
    const Lift::Result &lifted = *job->result();
    const QString step = QStringLiteral("Lift %1").arg(lifted.name);
    QString placedWhere;
    if (m_liftDestination == QLatin1String("desk")) {
        ProjectTab *tab = deskTab();
        if (!tab)
            return say(QStringLiteral("The Desk couldn't be opened."));
        Desk::Frame frame;
        frame.source = m_liftSurface.label();
        frame.art = normalized(lifted, &frame.size);
        frame.step = step;
        QString error;
        if (Desk::addFrame(tab->session, frame, &error).isNull())
            return say(error);
        placedWhere = QStringLiteral(" on the Desk");
    } else if (m_liftDestination == QLatin1String("document")) {
        if (m_workspace.isManaging())
            return say(QStringLiteral("Omastrator is showing a dialog, so the lift wasn't placed. Lift it again when it's answered."));
        QSizeF size;
        const VectorDocument art = normalized(lifted, &size);
        VectorDocument blank = VectorDocument::blank(size);
        ProjectTab &tab = m_workspace.addTab(false, lifted.name);
        tab.session.loadDocument(blank);
        VectorDocument next = blank;
        const QUuid layer = next.layers().front();
        std::vector<VectorObject> copies = art.copySubtree(lifted.root);
        const QUuid root = copies.front().id;
        copies.front().parentID = layer;
        for (VectorObject &copy : copies) {
            const QUuid parent = *copy.parentID;
            next.insert(std::move(copy), parent);
        }
        tab.session.beginInteraction(step);
        tab.session.previewDocument(next, {root});
        tab.session.commitInteraction();
        tab.session.markUnsaved();
        m_bridge.bringForward();
        placedWhere = QStringLiteral(" in a new document");
    } else {
        QString error;
        if (m_overlays.place(m_liftSurface, lifted.art, lifted.root, step, &error).isNull())
            return say(error);
        m_mode->select(0);
        updatePlacements();
    }
    QString line = QStringLiteral("Lifted %1%2: %3 objects.").arg(lifted.name, placedWhere).arg(lifted.objects);
    if (!lifted.notes.isEmpty())
        line += QLatin1Char(' ') + lifted.notes.join(QLatin1Char(' '));
    say(line);
}
