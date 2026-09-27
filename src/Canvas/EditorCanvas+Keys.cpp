#include "Canvas/EditorCanvasState.h"
#include <QKeyEvent>
#include <QSettings>
#include <algorithm>

namespace {
std::optional<Tool> toolForKey(int key)
{
    switch (key) {
    case Qt::Key_V:
        return Tool::select;
    case Qt::Key_A:
        return Tool::directSelect;
    case Qt::Key_P:
        return Tool::pen;
    case Qt::Key_N:
        return Tool::pencil;
    case Qt::Key_T:
        return Tool::text;
    case Qt::Key_Backslash:
        return Tool::line;
    case Qt::Key_M:
        return Tool::rectangle;
    case Qt::Key_L:
        return Tool::ellipse;
    case Qt::Key_R:
        return Tool::rotate;
    case Qt::Key_S:
        return Tool::scale;
    case Qt::Key_I:
        return Tool::eyedropper;
    case Qt::Key_G:
        return Tool::gradient;
    case Qt::Key_C:
        return Tool::scissors;
    case Qt::Key_H:
        return Tool::hand;
    case Qt::Key_Z:
        return Tool::zoom;
    default:
        return std::nullopt;
    }
}
}

bool EditorCanvas::State::keyPress(QKeyEvent *event)
{
    if (!session.hasDocument())
        return false;
    // Open type is a text field: it takes every key it knows.
    if (text) {
        if (event->key() == Qt::Key_Escape) {
            finishText();
            // Illustrator leaves the type selected under the Selection tool.
            return true;
        }
        const InlineTextEditor::Result result = text->keyPress(*event);
        if (result == InlineTextEditor::Result::edited)
            applyText();
        restartCaret();
        return result != InlineTextEditor::Result::ignored || InlineTextEditor::claims(*event);
    }
    // Alt alone shows the measurements, or turns Shape Builder's highlight to erasing.
    if (event->key() == Qt::Key_Alt) {
        // Some platforms report a modifier key's press without its own flag.
        modifiers |= Qt::AltModifier;
        canvas.update();
        return false;
    }
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const int key = event->key();
    // Digits are opacity, as in Figma and Paper: 1 is 10 %, 0 is 100 %.
    if (key >= Qt::Key_0 && key <= Qt::Key_9 && plain && !shift && !drag && session.hasSelection()
        && (session.tool() == Tool::select || session.tool() == Tool::directSelect)) {
        typeOpacity(key - Qt::Key_0);
        return true;
    }
    finishOpacity();
    switch (key) {
    case Qt::Key_Space:
        if (!event->isAutoRepeat() && plain) {
            spaceHeld = true;
            hovered.reset();
        }
        return plain;
    case Qt::Key_Escape:
        if (drag && drag->kind != DragKind::pen) {
            cancelDrag();
        } else if (pen) {
            finishPen();
        } else if (session.isolatedGroup()) {
            session.exitIsolation();
        } else {
            return false;
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (pen) {
            finishPen();
            return true;
        }
        return false;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (drag || !plain)
            return false;
        if (session.tool() == Tool::width && widthPointIndex && deleteWidthPoint())
            return true;
        if (pen)
            finishPen();
        if (session.tool() == Tool::artboard) {
            if (session.document()->artboardCount() > 1)
                session.deleteArtboard(session.activeArtboard());
            return true;
        }
        session.deleteSelection();
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        return nudge(event);
    default:
        break;
    }
    // Shift-M: Shape Builder, as in Illustrator.
    if (plain && shift && !event->isAutoRepeat() && event->key() == Qt::Key_M) {
        session.selectTool(Tool::shapeBuilder);
        return true;
    }
    // Shift-W: Width tool.
    if (plain && shift && !event->isAutoRepeat() && event->key() == Qt::Key_W) {
        session.selectTool(Tool::width);
        return true;
    }
    // Shift-O: the Artboard tool.
    if (plain && shift && !event->isAutoRepeat() && event->key() == Qt::Key_O) {
        session.selectTool(Tool::artboard);
        return true;
    }
    if (plain && !shift && !event->isAutoRepeat()) {
        if (const std::optional<Tool> tool = toolForKey(key)) {
            session.selectTool(*tool);
            return true;
        }
    }
    return false;
}

bool EditorCanvas::State::nudge(QKeyEvent *event)
{
    const Qt::KeyboardModifiers held = event->modifiers() & ~Qt::KeypadModifier;
    if (drag || (held & (Qt::ControlModifier | Qt::MetaModifier)))
        return false;
    const double step = EditorCanvas::keyboardIncrement() * (held.testFlag(Qt::ShiftModifier) ? 10 : 1);
    const int key = event->key();
    const QPointF delta = key == Qt::Key_Left ? QPointF(-step, 0)
        : key == Qt::Key_Right               ? QPointF(step, 0)
        : key == Qt::Key_Up                  ? QPointF(0, -step)
                                             : QPointF(0, step);
    if (!held.testFlag(Qt::AltModifier)) {
        session.moveSelection(delta);
        return true;
    }
    // Alt+arrows on type are its tracking and leading; picked anchors don't copy.
    const std::vector<QUuid> leaves = session.selectedLeaves();
    const bool allText = !leaves.empty() && std::all_of(leaves.begin(), leaves.end(), [&](const QUuid &id) {
        return session.document()->find(id)->kind == ObjectKind::text;
    });
    if (!session.hasSelection() || allText || (session.tool() == Tool::directSelect && !session.pickedNodes().empty()))
        return false;
    session.duplicateSelection(delta);
    return true;
}

void EditorCanvas::State::typeOpacity(int digit)
{
    const bool second = opacityDigit >= 0 && sinceDigit.isValid() && sinceDigit.elapsed() <= 500 && session.isInteracting()
        && session.interactionName() == QLatin1String("Opacity");
    const double percent = second ? opacityDigit * 10 + digit : digit == 0 ? 100 : digit * 10;
    if (!second)
        session.beginInteraction(QStringLiteral("Opacity"));
    for (const QUuid &id : session.selection()) {
        const VectorObject *current = session.document()->find(id);
        if (!current || session.document()->isEffectivelyLocked(id))
            continue;
        VectorObject changed = *current;
        changed.opacity = percent / 100;
        session.previewObject(changed);
    }
    if (second) {
        finishOpacity();
        return;
    }
    // One digit waits briefly for a second; either way it's one undo step.
    opacityDigit = digit;
    sinceDigit.start();
    opacityCommit.start(500);
}

void EditorCanvas::State::finishOpacity()
{
    opacityCommit.stop();
    opacityDigit = -1;
    if (session.isInteracting() && session.interactionName() == QLatin1String("Opacity"))
        session.commitInteraction();
}

bool EditorCanvas::State::keyRelease(QKeyEvent *event)
{
    // Alt's release ends the measurements and Shape Builder's erase highlight.
    if (event->key() == Qt::Key_Alt) {
        modifiers &= ~Qt::AltModifier;
        canvas.update();
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && spaceHeld) {
        spaceHeld = false;
        if (hover)
            updateHover(*hover);
        return true;
    }
    return false;
}

double EditorCanvas::keyboardIncrement()
{
    const double stored = QSettings().value(QStringLiteral("keyboardIncrement"), 1.0).toDouble();
    return stored > 0 ? stored : 1.0;
}

void EditorCanvas::setKeyboardIncrement(double points)
{
    if (points > 0)
        QSettings().setValue(QStringLiteral("keyboardIncrement"), points);
}
