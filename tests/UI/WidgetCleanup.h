#pragma once
#include <QApplication>
#include <QPointer>
#include <QWidget>
#include <functional>

// Deleting a dialog deletes its child dialogs, which a plain topLevelWidgets() snapshot still holds; track them so none is touched twice.
inline void deleteTopLevelWidgets(const std::function<bool(QWidget *)> &matches)
{
    QList<QPointer<QWidget>> doomed;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (matches(widget))
            doomed.append(widget);
    }
    for (const QPointer<QWidget> &widget : doomed) {
        if (widget)
            delete widget.data();
    }
}
