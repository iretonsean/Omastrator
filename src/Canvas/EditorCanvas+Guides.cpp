#include "Canvas/EditorCanvasState.h"
#include "Canvas/Rulers.h"
#include "Rendering/VectorRenderer.h"
#include <QInputDialog>
#include <QPainter>
#include <cmath>

namespace {
constexpr double guideReach = 3;
// Illustrator's guides are cyan.
const QColor guideColor(0, 184, 255);

QPen cosmetic(const QColor &color, double width = 1)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}
}

std::optional<int> EditorCanvas::State::guideAt(QPointF view) const
{
    const std::optional<VectorDocument> &document = session.document();
    if (!document || !session.showsGuides || session.guidesLocked)
        return std::nullopt;
    std::optional<int> best;
    double bestDistance = guideReach;
    for (int index = 0; index < int(document->guides.size()); ++index) {
        const Guide &guide = document->guides[size_t(index)];
        const QPointF at = toView(QPointF(guide.position, guide.position));
        const double distance = std::abs(guide.orientation == Qt::Horizontal ? view.y() - at.y() : view.x() - at.x());
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

void EditorCanvas::State::guidePress(int index, QPointF view)
{
    const Guide &guide = session.document()->guides[size_t(index)];
    beginDrag(DragKind::guide, view);
    drag->guide = index;
    drag->guideAxis = guide.orientation;
    drag->guidePosition = guide.position;
}

void EditorCanvas::State::beginRulerGuide(Qt::Orientation axis, QPointF view)
{
    beginDrag(DragKind::guide, view);
    drag->guide = -1;
    drag->guideAxis = axis;
    const QPointF document = toDocument(view);
    drag->guidePosition = axis == Qt::Horizontal ? document.y() : document.x();
    // Drawn out, the guides show again.
    if (!session.showsGuides)
        session.setShowsGuides(true);
}

void EditorCanvas::State::dragGuide(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    double position = drag->guideAxis == Qt::Horizontal ? document.y() : document.x();
    // Shift steps whole points; Snap to Pixel always does.
    if (session.snapsToPixel || modifiers.testFlag(Qt::ShiftModifier))
        position = std::round(position);
    drag->guidePosition = position;
}

void EditorCanvas::State::finishGuide(QPointF view)
{
    // Dropped back on its ruler, a guide goes; a click without a drag leaves it be.
    const bool onRuler = session.showsRulers
        && (drag->guideAxis == Qt::Horizontal ? view.y() < Rulers::thickness : view.x() < Rulers::thickness);
    if (drag->guide < 0) {
        if (drag->started && !onRuler)
            session.addGuide({drag->guideAxis, drag->guidePosition});
    } else if (drag->started) {
        if (onRuler || !QRectF(canvas.rect()).contains(view))
            session.removeGuide(drag->guide);
        else
            session.moveGuide(drag->guide, drag->guidePosition);
    }
}

void EditorCanvas::State::editGuide(int index)
{
    const Guide guide = session.document()->guides[size_t(index)];
    auto *dialog = new QInputDialog(&canvas);
    dialog->setObjectName(QStringLiteral("guidePositionDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Guide Position"));
    dialog->setInputMode(QInputDialog::DoubleInput);
    dialog->setLabelText(guide.orientation == Qt::Horizontal ? QStringLiteral("Y (pt):") : QStringLiteral("X (pt):"));
    dialog->setDoubleDecimals(3);
    dialog->setDoubleRange(-1e6, 1e6);
    dialog->setDoubleValue(guide.position);
    QObject::connect(dialog, &QInputDialog::doubleValueSelected, &canvas, [this, index](double position) { session.moveGuide(index, position); });
    dialog->open();
}

void EditorCanvas::State::drawGuides(QPainter &painter) const
{
    const std::optional<VectorDocument> &document = session.document();
    const bool dragging = drag && drag->kind == DragKind::guide && drag->started;
    if (!document || (!session.showsGuides && !dragging))
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRectF area(canvas.rect());
    const auto line = [&](const Guide &guide) {
        const QPointF at = toView(QPointF(guide.position, guide.position));
        if (guide.orientation == Qt::Horizontal)
            painter.drawLine(QPointF(area.left(), at.y()), QPointF(area.right(), at.y()));
        else
            painter.drawLine(QPointF(at.x(), area.top()), QPointF(at.x(), area.bottom()));
    };
    QColor shown = guideColor;
    // Locked guides are a little quieter.
    shown.setAlphaF(session.guidesLocked ? 0.55f : 0.9f);
    painter.setPen(cosmetic(shown));
    if (session.showsGuides) {
        for (int index = 0; index < int(document->guides.size()); ++index) {
            if (!(dragging && drag->guide == index))
                line(document->guides[size_t(index)]);
        }
    }
    painter.restore();
    if (dragging) {
        painter.save();
        painter.setPen(cosmetic(guideColor, 1.5));
        line({drag->guideAxis, drag->guidePosition});
        painter.restore();
        const QString label = (drag->guideAxis == Qt::Horizontal ? QStringLiteral("Y ") : QStringLiteral("X ")) + SmartGuides::label(drag->guidePosition);
        drawLabel(painter, drag->lastView + QPointF(40, 24), label);
    }
}

void EditorCanvas::State::drawPixelGrid(QPainter &painter, const QRectF &artboard) const
{
    // One line per point, only close enough in to tell them apart.
    if (!session.showsPixelGrid || session.viewport.zoom() < 6)
        return;
    const QRectF visible = artboard.intersected(QRectF(canvas.rect()));
    if (visible.isEmpty())
        return;
    const QPointF first = toDocument(visible.topLeft()), last = toDocument(visible.bottomRight());
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setClipRect(visible);
    painter.setPen(QPen(QColor(0, 0, 0, 22), 1 / session.viewport.backingScale));
    for (double x = std::ceil(first.x()); x <= last.x(); x += 1) {
        const double at = toView(QPointF(x, 0)).x();
        painter.drawLine(QPointF(at, visible.top()), QPointF(at, visible.bottom()));
    }
    for (double y = std::ceil(first.y()); y <= last.y(); y += 1) {
        const double at = toView(QPointF(0, y)).y();
        painter.drawLine(QPointF(visible.left(), at), QPointF(visible.right(), at));
    }
    painter.restore();
}

void EditorCanvas::State::drawIsolated(QPainter &painter, const VectorDocument &document, const QUuid &group) const
{
    VectorRenderer::Options options;
    options.outlineMode = session.showsOutline;
    if (!options.outlineMode) {
        for (const Artboard &board : document.allArtboards())
            painter.fillRect(board.rect, board.background);
    }
    // The rest at half strength, then the group on top at full.
    VectorRenderer::Options faded = options;
    faded.drawBackground = false;
    faded.skip.push_back(group);
    painter.save();
    painter.setOpacity(0.5);
    VectorRenderer::draw(painter, document, faded);
    painter.restore();
    VectorRenderer::Options plain = options;
    plain.drawBackground = false;
    VectorRenderer::drawObject(painter, document, group, plain);
}
