#pragma once
#include "Document/EditorSession.h"
#include <QDialog>

// Object and Document menu dialogs: typed amounts applied on OK.
namespace ObjectDialogs {
QDialog *rotate(EditorSession &session, QWidget *window);
QDialog *reflect(EditorSession &session, QWidget *window);
QDialog *scale(EditorSession &session, QWidget *window);
QDialog *move(EditorSession &session, QWidget *window);
QDialog *offsetPath(EditorSession &session, QWidget *window);
QDialog *artboardSize(EditorSession &session, QWidget *window);
// Object ▸ Path ▸ Average: horizontal, vertical or both.
QDialog *average(EditorSession &session, QWidget *window);
// Edit ▸ Preferences: the keyboard increment and how many history states to keep.
QDialog *preferences(QWidget *window);
}
