#include "UI/ColorPaletteControls.h"
#include "UI/NumberField.h"
#include "UI/PaintStack.h"
#include "UI/PropertiesPanel.h"

StrokeStyle PropertiesPanel::shownStroke() const
{
    if (m_strokeStack && !m_strokeStack->isHidden() && !m_strokeStack->entries().empty())
        return m_strokeStack->entries()[size_t(m_strokeStack->activeIndex())];
    return ShownStyle::stroke(m_session);
}

void PropertiesPanel::applyStroke(const StrokeStyle &stroke)
{
    if (m_strokeStack && !m_strokeStack->isHidden() && !m_strokeStack->entries().empty()) {
        std::vector<StrokeStyle> entries = m_strokeStack->entries();
        entries[size_t(m_strokeStack->activeIndex())] = stroke;
        m_session.setStrokesOfSelection(entries, QStringLiteral("Stroke"));
        return;
    }
    m_session.setStrokeOfSelection(stroke);
}

void PropertiesPanel::synchronizePaint()
{
    m_fill->synchronize();
    m_strokePaint->synchronize();
    for (const bool strokes : {false, true}) {
        const auto entries = PaintStack::shared(m_session, strokes);
        const bool stacked = entries && PaintStack::isStacked(*entries);
        PaintStack *stack = strokes ? m_strokeStack : m_fillStack;
        (strokes ? m_strokeLine : m_fillLine)->setVisible(!stacked);
        stack->setVisible(stacked);
        if (stacked)
            stack->synchronize();
    }
    m_selectionColors->synchronize();

    // Alignment is for closed shapes and arrowheads for open ends: each shows when the selection has some.
    bool open = false, closed = false;
    const std::vector<QUuid> leaves = m_session.selectedLeaves();
    for (const QUuid &id : leaves) {
        const VectorObject *object = m_session.document() ? m_session.document()->find(id) : nullptr;
        if (!object || !object->hasPaint())
            continue;
        if (object->kind == ObjectKind::text) {
            closed = true;
            continue;
        }
        for (const Contour &contour : object->path.contours)
            (contour.closed ? closed : open) = true;
    }
    if (leaves.empty())
        open = closed = true;
    const StrokeStyle stroke = shownStroke();
    m_strokeAlign->setVisible(closed);
    m_strokeAlignCaption->setVisible(closed);
    m_strokeAlign->setCurrentIndex(int(stroke.alignment));
    m_arrows->setVisible(open);
    m_startArrow->setCurrentIndex(int(stroke.startArrow));
    m_endArrow->setCurrentIndex(int(stroke.endArrow));
    m_arrowScale->sync(stroke.arrowScale);
    m_arrowScale->setVisible(stroke.startArrow != Arrowhead::none || stroke.endArrow != Arrowhead::none);
    m_alignDashes->setVisible(!stroke.dashes.empty());
    m_alignDashes->setChecked(stroke.alignDashes);
}
