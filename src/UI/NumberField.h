#pragma once
#include <QLineEdit>
#include <QWidget>
#include <functional>

// A labelled number: Return applies, arrows step, Shift by ten.
class NumberField : public QWidget {
    Q_OBJECT
public:
    NumberField(const QString &label, const QString &suffix, std::function<void(double)> change, QWidget *parent = nullptr);
    QLineEdit *const field;
    // Shows the value unless being typed in.
    void sync(double value);
    double value() const { return m_value; }
    // The step Up and Down take; Shift takes ten.
    double step = 1;
    static QString formatted(double value);
    // Applies the typed text, as Return does.
    void commit();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const std::function<void(double)> m_change;
    double m_value = 0;
    bool m_borrowed = false;
};
