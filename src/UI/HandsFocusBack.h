#pragma once
#include "Canvas/EditorCanvas.h"
#include <QKeyEvent>
#include <QObject>
#include <QPointer>

// Enter and Esc finish with a field: the canvas takes the keyboard back, so Ctrl+Z and the next Esc reach Edit Page.
// Installed on the field's line edit (the element bar's, the Motion inspector's).
class HandsFocusBack : public QObject {
public:
    HandsFocusBack(EditorCanvas *canvas, QObject *parent) : QObject(parent), m_canvas(canvas) {}

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return false;
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) {
            // The field's own filter runs after this one and needs the key first.
            QMetaObject::invokeMethod(m_canvas.data(), [canvas = m_canvas] {
                if (canvas)
                    canvas->setFocus(Qt::OtherFocusReason);
            }, Qt::QueuedConnection);
        }
        return false;
    }

private:
    QPointer<EditorCanvas> m_canvas;
};
