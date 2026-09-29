#include "Canvas/EditorCanvasState.h"
#include "Canvas/Rulers.h"
#include <QGuiApplication>
#include <QStyleHints>

// Dispatch ---------------------------------------------------------------------

void EditorCanvas::State::press(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF document = toDocument(view);
    finishOpacity();
    // Link mode (a thread's out port was clicked) takes the next click, wherever it lands.
    if (threadLinkPress(view)) {
        canvas.update();
        return;
    }
    // A click in the type being edited moves its caret; anywhere else ends it.
    if (text) {
        if (textBox().adjusted(-reach(4), -reach(4), reach(4), reach(4)).contains(document)
            && (session.tool() == Tool::text || session.tool() == Tool::select || session.tool() == Tool::directSelect)) {
            // A third click in quick succession takes the whole line.
            if (sinceDoubleClick.isValid() && sinceDoubleClick.elapsed() <= QGuiApplication::styleHints()->mouseDoubleClickInterval()
                && QLineF(view, doubleClickView).length() <= QGuiApplication::styleHints()->startDragDistance()) {
                sinceDoubleClick.invalidate();
                text->preedit.clear();
                text->selectLine(text->positionAt(document));
                restartCaret();
                return;
            }
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
    // A press on a ruler draws a guide out of it: the top one a horizontal guide, the left one a vertical.
    if (session.showsRulers && (view.x() < Rulers::thickness || view.y() < Rulers::thickness)) {
        if (view.x() >= Rulers::thickness)
            beginRulerGuide(Qt::Horizontal, view);
        else if (view.y() >= Rulers::thickness)
            beginRulerGuide(Qt::Vertical, view);
        return;
    }
    // A Browser View's bar and sign-in strip take their presses before any tool does.
    if (browserBarPress(view))
        return;
    // A held width preview ends with the next press anywhere but the page the Browse tool is looking at.
    if (session.tool() != Tool::browse)
        endHeldPreview();
    if (session.tool() == Tool::browse) {
        browsePress(view, modifiers, false);
        return;
    }
    // A guide under the Selection tools moves; a live corner's widget sets its radius;
    // a selected path text's bracket slides its start.
    if (session.tool() == Tool::select || session.tool() == Tool::directSelect) {
        if (const std::optional<CornerWidget> corner = cornerWidgetAt(view)) {
            cornerPress(*corner, view);
            return;
        }
        if (const std::optional<QUuid> id = pathBracketAt(view)) {
            beginDrag(DragKind::pathBracket, view);
            drag->object = *id;
            return;
        }
        if (const std::optional<QUuid> id = outPortHitAt(view)) {
            linkArmedFrom = id;
            return;
        }
        if (!handleAt(view) && !hitLeaf(document)) {
            if (const std::optional<int> guide = guideAt(view)) {
                guidePress(*guide, view);
                return;
            }
        }
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
    case Tool::typeOnPath:
        typeOnPathPress(view);
        break;
    case Tool::frame:
    case Tool::browserView:
    case Tool::line:
    case Tool::rectangle:
    case Tool::roundedRectangle:
    case Tool::ellipse:
    case Tool::polygon:
    case Tool::star:
        shapePress(view);
        break;
    case Tool::shapeBuilder:
        builderPress(view, modifiers);
        break;
    case Tool::scissors:
        scissorsPress(view);
        break;
    case Tool::rotate:
    case Tool::scale:
        transformToolPress(view);
        break;
    case Tool::gradient:
        gradientPress(view, modifiers);
        break;
    case Tool::width:
        widthPress(view, modifiers);
        break;
    case Tool::eyedropper:
        eyedropperPress(view, modifiers);
        break;
    case Tool::zoom:
        beginDrag(DragKind::zoomRect, view);
        break;
    case Tool::hand:
    case Tool::browse:
        break;
    case Tool::artboard:
        artboardPress(view, modifiers);
        break;
    }
}

void EditorCanvas::State::move(QPointF view, Qt::KeyboardModifiers modifiers, bool held)
{
    if (!session.hasDocument())
        return;
    if (session.tool() == Tool::browse && (!drag || drag->kind == DragKind::browse)) {
        // Without a button the release was lost: the page gets it.
        if (drag && !held) {
            release(view, modifiers);
            return;
        }
        browseMove(view, modifiers, held);
        return;
    }
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
    case DragKind::convert:
        dragConvert(view, modifiers);
        break;
    case DragKind::textArea:
        drag->grabbed = snapPoint(guidesExcluding({}), toDocument(view));
        break;
    case DragKind::shapeBuilder:
        dragBuilder(view, modifiers);
        break;
    case DragKind::gradient:
        dragGradient(view, modifiers);
        break;
    case DragKind::width:
        dragWidth(view, modifiers);
        break;
    case DragKind::guide:
        dragGuide(view, modifiers);
        break;
    case DragKind::corner:
        dragCorner(view, modifiers);
        break;
    case DragKind::pathBracket:
        dragPathBracket(view);
        break;
    case DragKind::artboard:
        dragArtboard(view, modifiers);
        break;
    case DragKind::browse:
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
    case DragKind::shapeBuilder:
        finishBuilder(modifiers);
        break;
    case DragKind::guide:
        finishGuide(view);
        break;
    case DragKind::corner:
        finishCorner(modifiers);
        break;
    case DragKind::pathBracket:
        if (drag->interacting && session.isInteracting())
            session.commitInteraction();
        break;
    case DragKind::move:
        // A click that moved nothing on an object already selected makes it the key object.
        if (!drag->started && drag->keyCandidate)
            session.setKeyObject(session.keyObject() == drag->keyCandidate ? std::nullopt : drag->keyCandidate);
        if (drag->interacting && session.isInteracting())
            session.commitInteraction();
        break;
    case DragKind::scale:
    case DragKind::rotate:
    case DragKind::scaleTool:
    case DragKind::nodes:
    case DragKind::handle:
    case DragKind::convert:
        if (drag->interacting && session.isInteracting()) {
            // A Browser View's width was only a preview: the frame goes back to its design width.
            if (drag->previewFrame)
                session.cancelInteraction();
            else
                session.commitInteraction();
        }
        break;
    case DragKind::shape:
        if (drag->interacting && session.isInteracting())
            session.commitInteraction();
        if (session.tool() == Tool::browserView)
            finishBrowserView();
        break;
    case DragKind::gradient:
        finishGradient();
        break;
    case DragKind::width:
        finishWidth();
        break;
    case DragKind::textArea:
        finishTextArea();
        break;
    case DragKind::artboard:
        finishArtboard();
        break;
    case DragKind::browse:
        browseRelease(view, modifiers);
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
    // The page's button comes up where the pointer was.
    if (kind == DragKind::browse)
        browseRelease(drag->lastView, modifiers);
    drag.reset();
    // The pen's path stays: only its last drag goes back.
    if (interacting && kind != DragKind::pen && session.isInteracting())
        session.cancelInteraction();
    clearGuides();
    canvas.update();
}

void EditorCanvas::State::doubleClick(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (session.tool() == Tool::browse) {
        if (drag)
            release(view, modifiers);
        browsePress(view, modifiers, true);
        return;
    }
    const QPointF document = toDocument(view);
    // A second click in the type being edited selects its word.
    if (text && textBox().adjusted(-reach(4), -reach(4), reach(4), reach(4)).contains(document)) {
        text->selectWord(text->positionAt(document));
        drag.reset();
        sinceDoubleClick.start();
        doubleClickView = view;
        restartCaret();
        return;
    }
    if (session.tool() == Tool::text)
        return;
    if (session.tool() != Tool::select && session.tool() != Tool::directSelect)
        return;
    if (drag)
        release(view, modifiers);
    const std::optional<QUuid> leaf = hitLeaf(document);
    if (!leaf) {
        // A double-click on a guide types its place; elsewhere outside, isolation steps out a level.
        if (const std::optional<int> guide = guideAt(view)) {
            editGuide(*guide);
        } else if (!session.isolation().empty()) {
            const std::optional<QUuid> under = session.document()->hitTest(document, reach(3));
            if (!under || !session.document()->isAncestor(*session.isolatedGroup(), *under))
                session.exitIsolation(int(session.isolation().size()) - 1);
        }
        return;
    }
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
        session.isolate(*target);
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
    if (was == Tool::browse)
        browseLeave();
    if (drag && drag->kind != DragKind::pan)
        cancelDrag();
    if (pen && shownTool != Tool::pen)
        finishPen();
    if (text && shownTool != Tool::text && was == Tool::text)
        finishText();
    linkArmedFrom.reset();
    hovered.reset();
    hoverGuides.reset();
    builderRegion.reset();
    builderEdge.reset();
    widthPointIndex.reset();
    updateHoverGuides(drag ? std::nullopt : hover);
    updateCursor();
}

void EditorCanvas::State::documentChanged()
{
    hoverGuides.reset();
    built.reset();
    builderRegion.reset();
    builderEdge.reset();
    const std::optional<VectorDocument> &document = session.document();
    // Undo, an edit or the agent ended the preview: the button no longer holds anything.
    if (held && !session.isPreviewOnly())
        held.reset();
    if (browseFocus && (!document || !document->find(*browseFocus)))
        setBrowseFocus(std::nullopt);
    if (browseHover && (!document || !document->find(*browseHover)))
        browseHover.reset();
    if (!document) {
        drag.reset();
        pen.reset();
        if (text) {
            text.reset();
            caretBlink.stop();
            canvas.setAttribute(Qt::WA_InputMethodEnabled, false);
            emit canvas.textEditingChanged(false);
        }
        hovered.reset();
        return;
    }
    if (pen && !document->find(pen->object))
        pen.reset();
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
