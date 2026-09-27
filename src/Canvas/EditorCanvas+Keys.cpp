#include "Canvas/EditorCanvasState.h"
#include <QKeyEvent>

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
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
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
        } else if (enteredGroup) {
            enteredGroup.reset();
            canvas.update();
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
        if (pen)
            finishPen();
        session.deleteSelection();
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (drag || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            return false;
        const double step = shift ? 10 : 1;
        const QPointF delta = event->key() == Qt::Key_Left ? QPointF(-step, 0)
            : event->key() == Qt::Key_Right               ? QPointF(step, 0)
            : event->key() == Qt::Key_Up                  ? QPointF(0, -step)
                                                          : QPointF(0, step);
        session.moveSelection(delta);
        return true;
    }
    default:
        break;
    }
    if (plain && !shift && !event->isAutoRepeat()) {
        if (const std::optional<Tool> tool = toolForKey(event->key())) {
            session.selectTool(*tool);
            return true;
        }
    }
    return false;
}

bool EditorCanvas::State::keyRelease(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && spaceHeld) {
        spaceHeld = false;
        if (hover)
            updateHover(*hover);
        return true;
    }
    return false;
}
