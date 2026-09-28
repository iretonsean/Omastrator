#include "Canvas/EditorCanvasState.h"
#include <QLineF>
#include <array>
#include <cmath>
#include <numbers>

namespace {
// Handles clockwise from the top left, as unit points of the box.
constexpr std::array<QPointF, 8> handleUnits{QPointF(0, 0), QPointF(0.5, 0), QPointF(1, 0), QPointF(1, 0.5),
                                             QPointF(1, 1), QPointF(0.5, 1), QPointF(0, 1), QPointF(0, 0.5)};
constexpr double handleReach = 6;
constexpr double rotateReach = 20;

QTransform around(QPointF center, const QTransform &transform)
{
    return QTransform::fromTranslate(-center.x(), -center.y()) * transform * QTransform::fromTranslate(center.x(), center.y());
}

double degreesBetween(QPointF center, QPointF from, QPointF to)
{
    const double start = std::atan2(from.y() - center.y(), from.x() - center.x());
    const double end = std::atan2(to.y() - center.y(), to.x() - center.x());
    return (end - start) * 180 / std::numbers::pi;
}
}

// Snapping ---------------------------------------------------------------------

SmartGuides EditorCanvas::State::guidesExcluding(const std::vector<QUuid> &excluded, const QUuid &excludedBoard) const
{
    if (!session.document())
        return {};
    SmartGuides guides = session.usesSmartGuides ? SmartGuides(*session.document(), excluded, excludedBoard) : SmartGuides();
    // Ruler guides pull whether or not smart guides are on, while they show.
    if (session.showsGuides)
        guides.addGuides(session.document()->guides);
    return guides;
}

QPointF EditorCanvas::State::snapPoint(const SmartGuides &guides, QPointF point, std::optional<QPointF> anchor, bool constrained)
{
    const SmartGuides::Result result = guides.point(point, scale(), anchor, constrained);
    guideLines = result.lines;
    guideGaps = result.gaps;
    QPointF snapped = result.delta;
    // The grid takes whichever axes the guides left alone; a 45° line keeps its angle.
    if (session.snapsToGrid && !constrained) {
        const QPointF grid = session.snapped(snapped);
        if (!result.snappedX)
            snapped.setX(grid.x());
        if (!result.snappedY)
            snapped.setY(grid.y());
    }
    // Snap to Pixel: whole points on whatever axis is still free.
    if (session.snapsToPixel && !constrained) {
        if (!result.snappedX)
            snapped.setX(std::round(snapped.x()));
        if (!result.snappedY)
            snapped.setY(std::round(snapped.y()));
    }
    return snapped;
}

QPointF EditorCanvas::State::snapMovement(const SmartGuides &guides, const QRectF &bounds, QPointF delta, bool constrained)
{
    const SmartGuides::Result result = guides.movement(bounds, delta, scale(), constrained);
    guideLines = result.lines;
    guideGaps = result.gaps;
    QPointF moved = result.delta;
    if (session.snapsToGrid && !constrained) {
        const QPointF corner = bounds.topLeft() + moved;
        const QPointF grid = session.snapped(corner);
        if (!result.snappedX)
            moved.rx() += grid.x() - corner.x();
        if (!result.snappedY)
            moved.ry() += grid.y() - corner.y();
    }
    // Snap to Pixel: the bounds' top left lands on whole points.
    if (session.snapsToPixel && !constrained) {
        const QPointF corner = bounds.topLeft() + moved;
        if (!result.snappedX)
            moved.rx() += std::round(corner.x()) - corner.x();
        if (!result.snappedY)
            moved.ry() += std::round(corner.y()) - corner.y();
    }
    return moved;
}

void EditorCanvas::State::clearGuides()
{
    guideLines.clear();
    guideGaps.clear();
}

// Hit testing ------------------------------------------------------------------

std::optional<QUuid> EditorCanvas::State::hitLeaf(QPointF document) const
{
    if (!session.document())
        return std::nullopt;
    // Isolated, only the group's own art answers; what's outside is dimmed and out of reach.
    if (const std::optional<QUuid> group = session.isolatedGroup()) {
        for (const QUuid &id : session.document()->hitTestAll(document, reach(3))) {
            if (session.document()->isAncestor(*group, id))
                return id;
        }
        return std::nullopt;
    }
    return session.document()->hitTest(document, reach(3));
}

std::optional<QUuid> EditorCanvas::State::selectableTarget(const QUuid &leaf) const
{
    const VectorDocument &document = *session.document();
    const std::optional<QUuid> group = session.isolatedGroup();
    if (group && document.isAncestor(*group, leaf)) {
        QUuid id = leaf;
        while (const VectorObject *object = document.find(id)) {
            if (object->parentID == group)
                return id;
            if (!object->parentID)
                break;
            id = *object->parentID;
        }
    }
    return document.selectableObject(leaf);
}

std::optional<QRectF> EditorCanvas::State::selectionBox() const
{
    // A selected artboard shows the handles an object would.
    if (session.tool() == Tool::select && session.artboardSelected() && !text && session.document())
        return activeArtboardBox();
    if (session.tool() != Tool::select || !session.hasSelection() || text || !session.document())
        return std::nullopt;
    // Locked objects show no handles: nothing would move.
    const VectorDocument &document = *session.document();
    if (std::all_of(session.selection().begin(), session.selection().end(), [&](const QUuid &id) { return document.isEffectivelyLocked(id); }))
        return std::nullopt;
    const QRectF bounds = session.selectionBounds();
    if (bounds.isNull() && bounds.topLeft().isNull())
        return std::nullopt;
    return bounds;
}

QPointF EditorCanvas::State::handlePoint(const QRectF &box, int index) const
{
    const QPointF unit = handleUnits[size_t(index)];
    return toView(QPointF(box.left() + unit.x() * box.width(), box.top() + unit.y() * box.height()));
}

bool EditorCanvas::State::handleShown(const QRectF &box, int index) const
{
    const QPointF unit = handleUnits[size_t(index)];
    return !((unit.x() == 0.5 && box.width() * scale() < 12) || (unit.y() == 0.5 && box.height() * scale() < 12));
}

std::optional<int> EditorCanvas::State::handleAt(QPointF view) const
{
    const std::optional<QRectF> box = selectionBox();
    if (!box)
        return std::nullopt;
    std::optional<int> best;
    double bestDistance = handleReach;
    for (int index = 0; index < 8; ++index) {
        if (!handleShown(*box, index))
            continue;
        const double distance = QLineF(view, handlePoint(*box, index)).length();
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

bool EditorCanvas::State::inRotateZone(QPointF view) const
{
    const std::optional<QRectF> box = selectionBox();
    // An artboard never turns.
    if (!box || session.artboardSelected())
        return false;
    const QRectF shown(toView(box->topLeft()), toView(box->bottomRight()));
    if (shown.adjusted(-2, -2, 2, 2).contains(view))
        return false;
    for (const int corner : {0, 2, 4, 6}) {
        if (QLineF(view, handlePoint(*box, corner)).length() <= rotateReach)
            return true;
    }
    return false;
}

void EditorCanvas::State::updateHover(QPointF view)
{
    std::optional<QUuid> next;
    if (session.hasDocument() && !spaceHeld && !text) {
        const Tool tool = session.tool();
        if (tool == Tool::select || tool == Tool::directSelect || tool == Tool::eyedropper) {
            if (const std::optional<QUuid> leaf = hitLeaf(toDocument(view)))
                next = tool == Tool::select ? selectableTarget(*leaf) : leaf;
        }
    }
    if (next != hovered) {
        hovered = next;
        canvas.update();
    }
    updateHoverGuides(view);
    updateBuilderHover(view);
    // The pen's rubber band follows the pointer.
    if (pen)
        canvas.update();
}

void EditorCanvas::State::updateHoverGuides(std::optional<QPointF> view)
{
    const Tool tool = session.tool();
    const bool drawing = tool == Tool::pen || tool == Tool::pencil || tool == Tool::text || isShapeTool(tool);
    if (view && drawing && !drag && !text && !spaceHeld && session.usesSmartGuides && session.hasDocument()) {
        if (!hoverGuides)
            hoverGuides = guidesExcluding(pen ? std::vector<QUuid>{pen->object} : std::vector<QUuid>{});
        const std::vector<QLineF> lines = guideLines, gaps = guideGaps;
        snapPoint(*hoverGuides, toDocument(*view));
        if (lines != guideLines || gaps != guideGaps)
            canvas.update();
    } else if (!drag && (!guideLines.empty() || !guideGaps.empty())) {
        clearGuides();
        canvas.update();
    }
}

// Selection tool ---------------------------------------------------------------

void EditorCanvas::State::selectPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    if (const std::optional<int> handle = handleAt(view)) {
        if (session.artboardSelected()) {
            beginArtboardResize(session.activeArtboard(), *handle, view);
            return;
        }
        beginDrag(DragKind::scale, view);
        drag->handle = *handle;
        drag->startBounds = *selectionBox();
        drag->guides = guidesExcluding(session.selection());
        return;
    }
    if (inRotateZone(view)) {
        beginDrag(DragKind::rotate, view);
        drag->center = selectionBox()->center();
        return;
    }
    // A frame's name on the canvas picks the frame itself.
    std::optional<QUuid> target = frameLabelAt(view);
    if (!target) {
        // So does an artboard's: a click selects it, and a drag moves it.
        if (const std::optional<int> board = artboardLabelAt(view)) {
            beginArtboardMove(*board, view, modifiers);
            return;
        }
        const std::optional<QUuid> leaf = hitLeaf(document);
        target = leaf ? selectableTarget(*leaf) : std::nullopt;
    }
    if (!target) {
        if (!modifiers.testFlag(Qt::ShiftModifier))
            session.deselectAll();
        beginDrag(DragKind::marquee, view);
        drag->additive = modifiers.testFlag(Qt::ShiftModifier);
        drag->selectionBefore = session.selection();
        return;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        session.toggleSelected(*target);
        if (!session.isSelected(*target))
            return;
    } else if (!session.isSelected(*target)) {
        session.select({*target});
    } else if (session.selection().size() > 1) {
        // Illustrator: a click on one of several selected objects makes it the key object.
        beginDrag(DragKind::move, view);
        drag->startBounds = session.selectionBounds();
        drag->keyCandidate = target;
        return;
    }
    beginDrag(DragKind::move, view);
    drag->startBounds = session.selectionBounds();
}

void EditorCanvas::State::transformToolPress(QPointF view)
{
    if (!session.hasSelection())
        return;
    const QRectF bounds = session.selectionBounds();
    beginDrag(session.tool() == Tool::rotate ? DragKind::rotate : DragKind::scaleTool, view);
    drag->center = bounds.center();
    drag->startBounds = bounds;
}

void EditorCanvas::State::dragMove(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        // Alt-drag leaves the originals and moves copies, all one undo step.
        drag->duplicate = modifiers.testFlag(Qt::AltModifier);
        session.beginInteraction(drag->duplicate ? QStringLiteral("Move Copy") : QStringLiteral("Move"));
        drag->interacting = true;
        if (drag->duplicate)
            session.previewDuplicateSelection();
        drag->guides = guidesExcluding(session.selection());
        drag->startBounds = session.selectionBounds();
    }
    const QPointF delta = snapMovement(drag->guides, drag->startBounds, toDocument(view) - drag->pressDocument,
                                       modifiers.testFlag(Qt::ShiftModifier));
    session.previewTransform(QTransform::fromTranslate(delta.x(), delta.y()));
}

QTransform EditorCanvas::State::handleScale(const QRectF &box, int handle, QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF unit = handleUnits[size_t(handle)];
    const QPointF grip(box.left() + unit.x() * box.width(), box.top() + unit.y() * box.height());
    const bool fromCenter = modifiers.testFlag(Qt::AltModifier);
    const QPointF opposite(box.left() + (1 - unit.x()) * box.width(), box.top() + (1 - unit.y()) * box.height());
    const QPointF fixed = fromCenter ? box.center() : opposite;
    const QPointF moved = snapPoint(drag->guides, grip + toDocument(view) - drag->pressDocument);
    const auto factor = [](double to, double from, double pivot) {
        const double span = from - pivot;
        return std::abs(span) < 1e-9 ? 1.0 : (to - pivot) / span;
    };
    double sx = unit.x() == 0.5 ? 1 : factor(moved.x(), grip.x(), fixed.x());
    double sy = unit.y() == 0.5 ? 1 : factor(moved.y(), grip.y(), fixed.y());
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        // Proportional: the axis pulled further leads; a side handle drags both.
        const double lead = unit.x() == 0.5 ? sy : unit.y() == 0.5 ? sx : (std::abs(sx) > std::abs(sy) ? sx : sy);
        sx = std::copysign(std::abs(lead), unit.x() == 0.5 ? 1.0 : sx);
        sy = std::copysign(std::abs(lead), unit.y() == 0.5 ? 1.0 : sy);
        guideLines.clear();
        guideGaps.clear();
    }
    // Never quite flat: a zero scale can't come back.
    const auto nonZero = [](double value) { return std::abs(value) < 1e-4 ? std::copysign(1e-4, value) : value; };
    return around(fixed, QTransform::fromScale(nonZero(sx), nonZero(sy)));
}

void EditorCanvas::State::dragScale(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Scale"));
        drag->interacting = true;
    }
    session.previewTransform(handleScale(drag->startBounds, drag->handle, view, modifiers), true);
}

void EditorCanvas::State::dragRotate(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Rotate"));
        drag->interacting = true;
    }
    double degrees = degreesBetween(drag->center, drag->pressDocument, toDocument(view));
    if (modifiers.testFlag(Qt::ShiftModifier))
        degrees = std::round(degrees / 45) * 45;
    QTransform rotation;
    rotation.rotate(degrees);
    session.previewTransform(around(drag->center, rotation));
}

void EditorCanvas::State::dragScaleTool(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Scale"));
        drag->interacting = true;
    }
    const QPointF from = drag->pressDocument - drag->center;
    const QPointF to = toDocument(view) - drag->center;
    const auto ratio = [](double a, double b) { return std::abs(b) < 1e-6 ? 1.0 : a / b; };
    double sx = ratio(to.x(), from.x()), sy = ratio(to.y(), from.y());
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        const double uniform = std::hypot(to.x(), to.y()) / std::max(1e-6, std::hypot(from.x(), from.y()));
        sx = sy = uniform;
    }
    const auto nonZero = [](double value) { return std::abs(value) < 1e-4 ? std::copysign(1e-4, value) : value; };
    session.previewTransform(around(drag->center, QTransform::fromScale(nonZero(sx), nonZero(sy))));
}

void EditorCanvas::State::dragMarquee(QPointF view)
{
    Q_UNUSED(view);
    // The rectangle draws from the drag; selection waits for release.
}

void EditorCanvas::State::finishMarquee()
{
    if (!drag->started)
        return;
    const QRectF area = QRectF(drag->pressDocument, toDocument(drag->lastView)).normalized();
    std::vector<QUuid> ids = drag->additive ? drag->selectionBefore : std::vector<QUuid>();
    // Isolated, the marquee takes the group's children only.
    const std::vector<QUuid> found = session.objectsIn(area, false);
    for (const QUuid &id : found) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    session.select(ids);
}

// Eyedropper -------------------------------------------------------------------

void EditorCanvas::State::eyedropperPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const std::optional<QUuid> leaf = hitLeaf(toDocument(view));
    if (!leaf)
        return;
    if (modifiers.testFlag(Qt::AltModifier))
        session.applyStyleTo(*leaf);
    else
        session.pickStyle(*leaf);
}
