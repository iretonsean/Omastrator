#include "Canvas/EditorCanvasState.h"

// Dispatch ---------------------------------------------------------------------

void EditorCanvas::State::press(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    // A click in the type being edited moves its caret; anywhere else ends it.
    if (text) {
        if (textBox().adjusted(-reach(4), -reach(4), reach(4), reach(4)).contains(document)
            && (session.tool() == Tool::text || session.tool() == Tool::select || session.tool() == Tool::directSelect)) {
            text->caret = text->positionAt(document);
            if (!modifiers.testFlag(Qt::ShiftModifier))
                text->anchor = text->caret;
            text->preedit.clear();
            beginDrag(DragKind::textSelect, view);
            restartCaret();
            return;
        }
        finishText();
    }
    if (spaceHeld || session.tool() == Tool::hand) {
        beginDrag(DragKind::pan, view);
        return;
    }
    switch (session.tool()) {
    case Tool::select:
        selectPress(view, modifiers);
        break;
    case Tool::directSelect:
        directPress(view, modifiers);
        break;
    case Tool::pen:
        penPress(view, modifiers);
        break;
    case Tool::pencil:
        pencilPress(view);
        break;
    case Tool::text:
        textPress(view);
        break;
    case Tool::line:
    case Tool::rectangle:
    case Tool::roundedRectangle:
    case Tool::ellipse:
    case Tool::polygon:
    case Tool::star:
        shapePress(view);
        break;
    case Tool::rotate:
    case Tool::scale:
        transformToolPress(view);
        break;
    case Tool::eyedropper:
        eyedropperPress(view);
        break;
    case Tool::zoom:
        beginDrag(DragKind::zoomRect, view);
        break;
    case Tool::hand:
        break;
    }
}

void EditorCanvas::State::move(QPointF view, Qt::KeyboardModifiers modifiers, bool held)
{
    if (!session.hasDocument())
        return;
    if (!drag) {
        updateHover(view);
        return;
    }
    // Without a button the release was lost: finish there.
    if (!held) {
        release(view, modifiers);
        updateHover(view);
        return;
    }
    if (!drag->started && pastDragDistance(view))
        drag->started = true;
    switch (drag->kind) {
    case DragKind::pan:
        session.panView(QSizeF(view.x() - drag->lastView.x(), view.y() - drag->lastView.y()));
        break;
    case DragKind::zoomRect:
    case DragKind::marquee:
        dragMarquee(view);
        break;
    case DragKind::move:
        dragMove(view, modifiers);
        break;
    case DragKind::scale:
        dragScale(view, modifiers);
        break;
    case DragKind::rotate:
        dragRotate(view, modifiers);
        break;
    case DragKind::scaleTool:
        dragScaleTool(view, modifiers);
        break;
    case DragKind::nodes:
        dragNodes(view, modifiers);
        break;
    case DragKind::handle:
        dragHandle(view, modifiers);
        break;
    case DragKind::shape:
        dragShape(view, modifiers);
        break;
    case DragKind::pencil:
        dragPencil(view);
        break;
    case DragKind::pen:
        dragPenHandle(view, modifiers);
        break;
    case DragKind::textSelect:
        if (text) {
            text->caret = text->positionAt(toDocument(view));
            restartCaret();
        }
        break;
    }
    if (drag)
        drag->lastView = view;
    canvas.update();
}

void EditorCanvas::State::release(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag)
        return;
    if (!drag->started && pastDragDistance(view))
        drag->started = true;
    drag->lastView = view;
    const DragKind kind = drag->kind;
    switch (kind) {
    case DragKind::zoomRect:
        finishZoom(view, modifiers);
        break;
    case DragKind::marquee:
        if (session.tool() == Tool::directSelect)
            finishDirectMarquee();
        else
            finishMarquee();
        break;
    case DragKind::pencil:
        finishPencil();
        break;
    case DragKind::pen:
        penRelease();
        break;
    case DragKind::move:
    case DragKind::scale:
    case DragKind::rotate:
    case DragKind::scaleTool:
    case DragKind::nodes:
    case DragKind::handle:
    case DragKind::shape:
        if (drag->interacting && session.isInteracting())
            session.commitInteraction();
        break;
    case DragKind::pan:
    case DragKind::textSelect:
        break;
    }
    drag.reset();
    clearGuides();
    canvas.update();
}

void EditorCanvas::State::cancelDrag()
{
    if (!drag)
        return;
    const bool interacting = drag->interacting;
    const DragKind kind = drag->kind;
    drag.reset();
    // The pen's path stays: only its last drag goes back.
    if (interacting && kind != DragKind::pen && session.isInteracting())
        session.cancelInteraction();
    clearGuides();
    canvas.update();
}

void EditorCanvas::State::doubleClick(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    if (session.tool() == Tool::text) {
        // A second click in the type selects its word.
        if (text) {
            const QString &content = text->text();
            int from = text->caret, to = text->caret;
            while (from > 0 && content[from - 1].isLetterOrNumber())
                --from;
            while (to < content.size() && content[to].isLetterOrNumber())
                ++to;
            text->anchor = from;
            text->caret = to;
            drag.reset();
        }
        return;
    }
    if (session.tool() != Tool::select && session.tool() != Tool::directSelect)
        return;
    if (drag)
        release(view, modifiers);
    const std::optional<QUuid> leaf = hitLeaf(document);
    if (!leaf)
        return;
    const VectorDocument &doc = *session.document();
    const VectorObject *object = doc.find(*leaf);
    if (object->kind == ObjectKind::text) {
        const VectorObject copy = *object;
        session.selectTool(Tool::text);
        beginTextEditing(copy, true, document);
        return;
    }
    if (session.tool() != Tool::select)
        return;
    // Isolate the group under the pointer, a level at a time.
    const std::optional<QUuid> target = selectableTarget(*leaf);
    if (target && doc.find(*target) && doc.find(*target)->kind == ObjectKind::group) {
        enteredGroup = target;
        if (const std::optional<QUuid> inside = selectableTarget(*leaf))
            session.select({*inside});
    }
}

void EditorCanvas::State::toolChanged()
{
    if (session.tool() == shownTool)
        return;
    const Tool was = shownTool;
    shownTool = session.tool();
    if (drag && drag->kind != DragKind::pan)
        cancelDrag();
    if (pen && shownTool != Tool::pen)
        finishPen();
    if (text && shownTool != Tool::text && was == Tool::text)
        finishText();
    if (shownTool != Tool::select)
        enteredGroup.reset();
    hovered.reset();
    updateCursor();
}

void EditorCanvas::State::documentChanged()
{
    const std::optional<VectorDocument> &document = session.document();
    if (!document) {
        drag.reset();
        pen.reset();
        if (text) {
            text.reset();
            caretBlink.stop();
            canvas.setAttribute(Qt::WA_InputMethodEnabled, false);
            emit canvas.textEditingChanged(false);
        }
        enteredGroup.reset();
        hovered.reset();
        return;
    }
    if (pen && !document->find(pen->object))
        pen.reset();
    if (enteredGroup && !document->find(*enteredGroup))
        enteredGroup.reset();
    if (hovered && !document->find(*hovered))
        hovered.reset();
    // Undo or a panel changed the type being edited: follow it.
    if (text && !applyingText && text->inDocument) {
        const VectorObject *object = document->find(text->object.id);
        if (!object || object->kind != ObjectKind::text) {
            text.reset();
            caretBlink.stop();
            canvas.setAttribute(Qt::WA_InputMethodEnabled, false);
            emit canvas.textEditingChanged(false);
            return;
        }
        text->object = *object;
        const int length = int(text->text().size());
        text->caret = std::min(text->caret, length);
        text->anchor = std::min(text->anchor, length);
    }
}
