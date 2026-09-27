#include "UI/NumberField.h"
#include "UI/ToolHeaderStyle.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace {
// Points per unit; px is a point, as in Illustrator.
std::optional<double> unitFactor(const QString &unit)
{
    const QString lower = unit.toLower();
    if (lower == QLatin1String("pt") || lower == QLatin1String("px"))
        return 1.0;
    if (lower == QLatin1String("mm"))
        return 72 / 25.4;
    if (lower == QLatin1String("cm"))
        return 72 / 2.54;
    if (lower == QLatin1String("in"))
        return 72.0;
    if (lower == QLatin1String("pc"))
        return 12.0;
    return std::nullopt;
}

// A small recursive-descent reader: sums of products of signed factors.
class Expression {
public:
    Expression(const QString &text, double current, bool lengths) : m_text(text), m_current(current), m_lengths(lengths) {}

    std::optional<double> read()
    {
        const std::optional<double> value = sum();
        skipSpace();
        if (!value || m_at != m_text.size() || !std::isfinite(*value))
            return std::nullopt;
        return value;
    }

private:
    void skipSpace()
    {
        while (m_at < m_text.size() && m_text[m_at].isSpace())
            ++m_at;
    }

    bool take(QChar character)
    {
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == character) {
            ++m_at;
            return true;
        }
        return false;
    }

    std::optional<double> sum()
    {
        std::optional<double> value = product();
        while (value) {
            if (take(QLatin1Char('+'))) {
                const auto next = product();
                value = next ? std::optional(*value + *next) : std::nullopt;
            } else if (take(QLatin1Char('-'))) {
                const auto next = product();
                value = next ? std::optional(*value - *next) : std::nullopt;
            } else {
                break;
            }
        }
        return value;
    }

    std::optional<double> product()
    {
        std::optional<double> value = factor();
        while (value) {
            if (take(QLatin1Char('*')) || take(QChar(0x00d7))) {
                const auto next = factor();
                value = next ? std::optional(*value * *next) : std::nullopt;
            } else if (take(QLatin1Char('/'))) {
                const auto next = factor();
                if (!next || *next == 0)
                    return std::nullopt;
                value = *value / *next;
            } else {
                break;
            }
        }
        return value;
    }

    std::optional<double> factor()
    {
        if (take(QLatin1Char('-'))) {
            const auto value = factor();
            return value ? std::optional(-*value) : std::nullopt;
        }
        if (take(QLatin1Char('+')))
            return factor();
        std::optional<double> value;
        if (take(QLatin1Char('('))) {
            value = sum();
            if (!value || !take(QLatin1Char(')')))
                return std::nullopt;
        } else {
            value = number();
        }
        return value ? suffix(*value) : std::nullopt;
    }

    std::optional<double> number()
    {
        skipSpace();
        const qsizetype start = m_at;
        while (m_at < m_text.size() && (m_text[m_at].isDigit() || m_text[m_at] == QLatin1Char('.')))
            ++m_at;
        bool parsed = false;
        const double value = m_text.mid(start, m_at - start).toDouble(&parsed);
        return parsed ? std::optional(value) : std::nullopt;
    }

    // A unit or a percentage right after a number.
    std::optional<double> suffix(double value)
    {
        skipSpace();
        if (take(QLatin1Char('%')))
            return value / 100 * m_current;
        qsizetype end = m_at;
        while (end < m_text.size() && m_text[end].isLetter())
            ++end;
        if (end == m_at)
            return value;
        const std::optional<double> factor = m_lengths ? unitFactor(m_text.mid(m_at, end - m_at)) : std::nullopt;
        if (!factor)
            return std::nullopt;
        m_at = end;
        return value * *factor;
    }

    const QString m_text;
    const double m_current;
    const bool m_lengths;
    qsizetype m_at = 0;
};
}

NumberField::NumberField(const QString &label, const QString &suffix, std::function<void(double)> change, QWidget *parent)
    : QWidget(parent), field(new QLineEdit(this)), m_change(std::move(change))
{
    lengths = suffix == QLatin1String("pt");
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    const auto quiet = [this](const QString &words) {
        auto *made = new QLabel(words, this);
        made->setFont(ToolHeaderStyle::controlFont());
        made->setForegroundRole(QPalette::PlaceholderText);
        return made;
    };
    if (!label.isEmpty()) {
        m_handle = quiet(label);
        m_handle->setObjectName(QStringLiteral("numberLabel"));
        row->addWidget(m_handle);
    }
    field->setAccessibleName(label);
    field->setFont(ToolHeaderStyle::controlFont());
    field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field->installEventFilter(this);
    // Narrow docks shrink the field, never push the unit out of sight.
    field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    field->setMinimumWidth(32);
    row->addWidget(field, 1);
    if (!suffix.isEmpty()) {
        QLabel *unit = quiet(suffix);
        unit->setObjectName(QStringLiteral("numberUnit"));
        row->addWidget(unit);
        if (!m_handle)
            m_handle = unit;
    }
    if (m_handle) {
        m_handle->setCursor(Qt::SizeHorCursor);
        m_handle->installEventFilter(this);
        m_handle->setToolTip(QStringLiteral("Drag to change: Shift for ten times, Alt for a tenth"));
    }
}

void NumberField::sync(double value)
{
    m_value = value;
    m_mixed = false;
    m_unset = false;
    field->setPlaceholderText(QString());
    if (!field->hasFocus() && !m_borrowed)
        field->setText(formatted(value));
}

void NumberField::syncUnset(double value, const QString &placeholder)
{
    m_value = value;
    m_mixed = false;
    m_unset = true;
    field->setPlaceholderText(placeholder);
    if (!field->hasFocus() && !m_borrowed)
        field->clear();
}

void NumberField::syncMixed()
{
    m_mixed = true;
    m_unset = false;
    field->setPlaceholderText(QStringLiteral("Mixed"));
    if (!field->hasFocus() && !m_borrowed)
        field->clear();
}

QString NumberField::formatted(double value)
{
    return std::abs(value - std::round(value)) < 0.005 ? QString::number(std::lround(value)) : QString::number(value, 'f', 2);
}

std::optional<double> NumberField::evaluate(const QString &text, double current, bool lengths)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    if (const auto change = relative(trimmed, lengths))
        return (*change)(current);
    return Expression(trimmed, current, lengths).read();
}

std::optional<std::function<double(double)>> NumberField::relative(const QString &text, bool lengths)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() < 2)
        return std::nullopt;
    const QChar op = trimmed.at(0);
    if (op != QLatin1Char('+') && op != QLatin1Char('*') && op != QLatin1Char('/') && op != QChar(0x00d7))
        return std::nullopt;
    const QString rest = trimmed.mid(1);
    // The operand can't use %, which is of each object's own value.
    const std::optional<double> operand = Expression(rest, 0, lengths).read();
    if (!operand || rest.contains(QLatin1Char('%')) || (op == QLatin1Char('/') && *operand == 0))
        return std::nullopt;
    const double by = *operand;
    if (op == QLatin1Char('+'))
        return [by](double value) { return value + by; };
    if (op == QLatin1Char('/'))
        return [by](double value) { return value / by; };
    return [by](double value) { return value * by; };
}

void NumberField::apply(double value)
{
    const double clamped = std::clamp(value, minimum, maximum);
    if (m_mixed || std::abs(clamped - m_value) > 1e-9)
        m_change(clamped);
}

// Typed text applies once, when it is a new number.
void NumberField::commit()
{
    const QString typed = field->text().trimmed();
    if (cleared && (typed.isEmpty() || typed.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0)) {
        if (!m_unset)
            cleared();
        field->setText(m_mixed || m_unset ? QString() : formatted(m_value));
        return;
    }
    if (typed.isEmpty() || (m_mixed && typed == QLatin1String("Mixed"))) {
        field->setText(m_mixed || m_unset ? QString() : formatted(m_value));
        return;
    }
    const auto change = relative(typed, lengths);
    if (change && changeEach) {
        const double low = minimum, high = maximum;
        const std::function<double(double)> each = *change;
        changeEach([each, low, high](double value) { return std::clamp(each(value), low, high); });
    } else if (const std::optional<double> value = evaluate(typed, m_value, lengths)) {
        apply(*value);
    }
    field->setText(m_mixed || m_unset ? QString() : formatted(m_value));
}

void NumberField::stepBy(double amount)
{
    if (m_mixed && changeEach) {
        const double low = minimum, high = maximum;
        changeEach([amount, low, high](double value) { return std::clamp(value + amount, low, high); });
    } else {
        apply(m_value + amount);
    }
    field->setText(m_mixed ? QString() : formatted(m_value));
}

bool NumberField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_handle && isEnabled()) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (mouse->button() == Qt::LeftButton) {
                m_scrubX = mouse->globalPosition().x();
                m_scrubStart = m_value;
                m_scrubbing = false;
                return true;
            }
            break;
        case QEvent::MouseMove:
            if (m_scrubX) {
                const double dx = mouse->globalPosition().x() - *m_scrubX;
                if (!m_scrubbing && std::abs(dx) < 2)
                    return true;
                if (!m_scrubbing) {
                    m_scrubbing = true;
                    if (gesture)
                        gesture(true);
                }
                const Qt::KeyboardModifiers held = mouse->modifiers();
                const double rate = held.testFlag(Qt::ShiftModifier) ? 10 : held.testFlag(Qt::AltModifier) ? 0.1 : 1;
                // A pixel is a step; a tenth-step scrub keeps a decimal.
                const double raw = m_scrubStart + std::round(dx) * step * rate;
                apply(rate < 1 ? std::round(raw * 100) / 100 : raw);
                field->setText(formatted(m_value));
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_scrubX && mouse->button() == Qt::LeftButton) {
                m_scrubX.reset();
                if (m_scrubbing && gesture)
                    gesture(false);
                m_scrubbing = false;
                return true;
            }
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }
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
        field->setText(m_mixed || m_unset ? QString() : formatted(m_value));
        field->clearFocus();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);
    // A step is no typing: it writes what it applied.
    const Qt::KeyboardModifiers held = key->modifiers();
    const double rate = held.testFlag(Qt::ShiftModifier) ? 10 : held.testFlag(Qt::AltModifier) ? 0.1 : 1;
    stepBy(rate * step * (key->key() == Qt::Key_Up ? 1 : -1));
    return true;
}
