#include "UI/ObjectDialogs.h"
#include "Canvas/EditorCanvas.h"
#include "UI/KeyboardShortcuts.h"
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

namespace {
// A window-modal sheet: its fields, then OK and Cancel.
QDialog *sheet(QWidget *window, const QString &name, const QString &title, QFormLayout *&form)
{
    auto *dialog = new QDialog(window);
    dialog->setObjectName(name);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(title);
    form = new QFormLayout(dialog);
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(10);
    return dialog;
}

void finish(QDialog *dialog, QFormLayout *form, std::function<void()> apply)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("dialogOK"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("dialogCancel"));
    NativeShortcut::bind(*dialog, buttons->button(QDialogButtonBox::Ok), buttons->button(QDialogButtonBox::Cancel));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, apply = std::move(apply)] {
        apply();
        dialog->accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->open();
}

QDoubleSpinBox *number(QDialog *dialog, const QString &name, double value, double minimum, double maximum, const QString &suffix)
{
    auto *box = new QDoubleSpinBox(dialog);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(2);
    box->setValue(value);
    box->setSuffix(suffix);
    box->setFixedWidth(120);
    return box;
}
}

QDialog *ObjectDialogs::rotate(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("rotateDialog"), QStringLiteral("Rotate"), form);
    QDoubleSpinBox *angle = number(dialog, QStringLiteral("angle"), 45, -360, 360, QStringLiteral("°"));
    form->addRow(QStringLiteral("Angle:"), angle);
    // Positive turns counterclockwise, as Illustrator's.
    finish(dialog, form, [&session, angle] { session.rotateSelection(-angle->value()); });
    return dialog;
}

QDialog *ObjectDialogs::reflect(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("reflectDialog"), QStringLiteral("Reflect"), form);
    auto *horizontal = new QRadioButton(QStringLiteral("Horizontal"), dialog);
    horizontal->setObjectName(QStringLiteral("reflectHorizontal"));
    auto *vertical = new QRadioButton(QStringLiteral("Vertical"), dialog);
    vertical->setObjectName(QStringLiteral("reflectVertical"));
    vertical->setChecked(true);
    form->addRow(QStringLiteral("Axis:"), horizontal);
    form->addRow(QString(), vertical);
    // A vertical axis mirrors left and right.
    finish(dialog, form, [&session, vertical] { session.flipSelection(vertical->isChecked() ? Qt::Horizontal : Qt::Vertical); });
    return dialog;
}

QDialog *ObjectDialogs::scale(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("scaleDialog"), QStringLiteral("Scale"), form);
    QDoubleSpinBox *horizontal = number(dialog, QStringLiteral("scaleHorizontal"), 100, 0.01, 100000, QStringLiteral("%"));
    QDoubleSpinBox *vertical = number(dialog, QStringLiteral("scaleVertical"), 100, 0.01, 100000, QStringLiteral("%"));
    form->addRow(QStringLiteral("Horizontal:"), horizontal);
    form->addRow(QStringLiteral("Vertical:"), vertical);
    finish(dialog, form, [&session, horizontal, vertical] { session.scaleSelection(horizontal->value() / 100, vertical->value() / 100); });
    return dialog;
}

QDialog *ObjectDialogs::move(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("moveDialog"), QStringLiteral("Move"), form);
    QDoubleSpinBox *horizontal = number(dialog, QStringLiteral("moveHorizontal"), 0, -100000, 100000, QStringLiteral(" pt"));
    QDoubleSpinBox *vertical = number(dialog, QStringLiteral("moveVertical"), 0, -100000, 100000, QStringLiteral(" pt"));
    form->addRow(QStringLiteral("Horizontal:"), horizontal);
    form->addRow(QStringLiteral("Vertical:"), vertical);
    finish(dialog, form, [&session, horizontal, vertical] { session.moveSelection(QPointF(horizontal->value(), vertical->value())); });
    return dialog;
}

QDialog *ObjectDialogs::offsetPath(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("offsetDialog"), QStringLiteral("Offset Path"), form);
    QDoubleSpinBox *distance = number(dialog, QStringLiteral("offset"), 10, -1000, 1000, QStringLiteral(" pt"));
    form->addRow(QStringLiteral("Offset:"), distance);
    finish(dialog, form, [&session, distance] { session.offsetSelection(distance->value()); });
    return dialog;
}

QDialog *ObjectDialogs::average(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("averageDialog"), QStringLiteral("Average"), form);
    auto *horizontal = new QRadioButton(QStringLiteral("Horizontal"), dialog);
    horizontal->setObjectName(QStringLiteral("averageHorizontal"));
    horizontal->setToolTip(QStringLiteral("Line the points up on one horizontal line"));
    auto *vertical = new QRadioButton(QStringLiteral("Vertical"), dialog);
    vertical->setObjectName(QStringLiteral("averageVertical"));
    vertical->setToolTip(QStringLiteral("Line the points up on one vertical line"));
    auto *both = new QRadioButton(QStringLiteral("Both"), dialog);
    both->setObjectName(QStringLiteral("averageBoth"));
    both->setToolTip(QStringLiteral("Move the points onto one spot"));
    both->setChecked(true);
    form->addRow(QStringLiteral("Axis:"), horizontal);
    form->addRow(QString(), vertical);
    form->addRow(QString(), both);
    finish(dialog, form, [&session, horizontal, vertical] {
        session.averagePoints(horizontal->isChecked() ? Qt::Horizontal : vertical->isChecked() ? Qt::Vertical : Qt::Horizontal | Qt::Vertical);
    });
    return dialog;
}

QDialog *ObjectDialogs::artboardSize(EditorSession &session, QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("artboardDialog"), QStringLiteral("Artboard Size"), form);
    const QSizeF size = session.document() ? session.document()->size : QSizeF(612, 792);
    QDoubleSpinBox *width = number(dialog, QStringLiteral("artboardWidth"), size.width(), 1, 16384, QStringLiteral(" pt"));
    QDoubleSpinBox *height = number(dialog, QStringLiteral("artboardHeight"), size.height(), 1, 16384, QStringLiteral(" pt"));
    form->addRow(QStringLiteral("Width:"), width);
    form->addRow(QStringLiteral("Height:"), height);
    finish(dialog, form, [&session, width, height] { session.setArtboardSize(QSizeF(width->value(), height->value())); });
    return dialog;
}

QDialog *ObjectDialogs::preferences(QWidget *window)
{
    QFormLayout *form = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("preferencesDialog"), QStringLiteral("Preferences"), form);
    QDoubleSpinBox *increment = number(dialog, QStringLiteral("keyboardIncrement"), EditorCanvas::keyboardIncrement(), 0.01, 1000, QStringLiteral(" pt"));
    increment->setToolTip(QStringLiteral("How far an arrow key moves the selection. Shift moves ten times as far."));
    form->addRow(QStringLiteral("Keyboard increment:"), increment);
    auto *history = new QSpinBox(dialog);
    history->setObjectName(QStringLiteral("historyLimit"));
    history->setRange(1, 1000);
    history->setValue(EditorSession::historyLimit());
    history->setFixedWidth(120);
    history->setToolTip(QStringLiteral("How many steps each document can undo. Past it, the oldest go first."));
    form->addRow(QStringLiteral("History states:"), history);
    finish(dialog, form, [increment, history] {
        EditorCanvas::setKeyboardIncrement(increment->value());
        EditorSession::setHistoryLimit(history->value());
    });
    return dialog;
}
