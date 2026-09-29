#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <functional>
#include <optional>

// A labelled number: Return applies, arrows step, Shift by ten. It takes
// arithmetic and units, and dragging its label scrubs the value.
class NumberField : public QWidget {
    Q_OBJECT
public:
    NumberField(const QString &label, const QString &suffix, std::function<void(double)> change, QWidget *parent = nullptr);
    QLineEdit *const field;
    // The drag handle: the label, or the unit when there's no label.
    QLabel *handle() const { return m_handle; }
    // Shows the value unless being typed in.
    void sync(double value);
    // Several different values: blank, with the placeholder "Mixed".
    void syncMixed();
    // A value the field leaves unwritten, as Auto leading: blank, with `placeholder`.
    void syncUnset(double value, const QString &placeholder);
    bool isMixed() const { return m_mixed; }
    double value() const { return m_value; }
    // The step Up and Down take; Shift takes ten, Alt a tenth.
    double step = 1;
    // Clamps what the field applies.
    double minimum = -1e9;
    double maximum = 1e9;
    // Lengths read pt px mm cm in pc; other fields take plain numbers.
    bool lengths = false;
    // Set to apply "+10" or "*2" to each object on its own value.
    std::function<void(const std::function<double(double)> &)> changeEach;
    // Brackets a scrub: true as it starts, false as it ends, so it's one undo step.
    std::function<void(bool)> gesture;
    // Set to accept an emptied field, or "auto", as its own choice.
    std::function<void()> cleared;
    static QString formatted(double value);
    // Arithmetic with units; % is of `current`. Nullopt for anything else.
    static std::optional<double> evaluate(const QString &text, double current, bool lengths = true);
    // "+10", "*2" and "/2" change a value rather than replace it.
    static std::optional<std::function<double(double)>> relative(const QString &text, bool lengths = true);
    // Applies the typed text, as Return does.
    void commit();
    // Every panel control's height (docs/PANELS.md).
    static constexpr int fieldHeight = 24;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void apply(double value);
    void stepBy(double amount);
    // Ends a scrub and closes its edit, if one is open.
    void endScrub();

    const std::function<void(double)> m_change;
    QLabel *m_handle = nullptr;
    double m_value = 0;
    bool m_mixed = false;
    bool m_unset = false;
    bool m_borrowed = false;
    // A scrub in progress: where it began and the value then.
    std::optional<double> m_scrubX;
    double m_scrubStart = 0;
    bool m_scrubbing = false;
};
