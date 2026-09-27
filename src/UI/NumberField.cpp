#include "UI/NumberField.h"
#include "UI/ToolHeaderStyle.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <cmath>

NumberField::NumberField(const QString &label, const QString &suffix, std::function<void(double)> change, QWidget *parent)
    : QWidget(parent), field(new QLineEdit(this)), m_change(std::move(change))
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    if (!label.isEmpty()) {
        auto *name = new QLabel(label, this);
        name->setFont(ToolHeaderStyle::controlFont());
        name->setForegroundRole(QPalette::PlaceholderText);
        row->addWidget(name);
    }
    field->setAccessibleName(label);
    field->setFont(ToolHeaderStyle::controlFont());
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->installEventFilter(this);
    row->addWidget(field, 1);
    if (!suffix.isEmpty()) {
        auto *unit = new QLabel(suffix, this);
        unit->setFont(ToolHeaderStyle::controlFont());
        unit->setForegroundRole(QPalette::PlaceholderText);
        row->addWidget(unit);
    }
}

void NumberField::sync(double value)
{
    m_value = value;
    if (!field->hasFocus() && !m_borrowed)
        field->setText(formatted(value));
}

QString NumberField::formatted(double value)
{
    return std::abs(value - std::round(value)) < 0.005 ? QString::number(std::lround(value)) : QString::number(value, 'f', 2);
}

// Typed text applies once, when it is a new number.
void NumberField::commit()
{
    bool number = false;
    const double typed = field->text().trimmed().toDouble(&number);
    if (number && std::isfinite(typed) && std::abs(typed - m_value) > 1e-9)
        m_change(typed);
    field->setText(formatted(m_value));
}

bool NumberField::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusOut) {
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        m_borrowed = reason == Qt::MenuBarFocusReason || reason == Qt::PopupFocusReason;
        if (!m_borrowed)
            commit();
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        commit();
        field->selectAll();
        return true;
    }
    if (key->key() == Qt::Key_Escape) {
        field->setText(formatted(m_value));
        field->clearFocus();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);
    // A step is no typing: it writes what it applied.
    const double amount = (key->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * step * (key->key() == Qt::Key_Up ? 1 : -1);
    m_change(m_value + amount);
    field->setText(formatted(m_value));
    return true;
}
