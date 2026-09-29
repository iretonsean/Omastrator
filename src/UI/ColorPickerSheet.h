#pragma once
#include "UI/FloatingPanel.h"
#include <QColor>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <array>
#include <functional>
#include <optional>
#include <vector>

// A picker entry commits on Return or leaving.
class PickerField : public QLineEdit {
    Q_OBJECT
public:
    PickerField(std::function<void()> commit, std::function<void(int)> step, QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    const std::function<void()> m_commit;
    const std::function<void(int)> m_step;
};

// Hue, saturation and brightness, as the picker edits them.
struct PickerHSB {
    double hue = 0;
    double saturation = 0;
    double brightness = 0;
    static PickerHSB from(const QColor &color);
    QColor color() const;
    friend bool operator==(const PickerHSB &, const PickerHSB &) = default;
};

// The last colours picked anywhere in the app, newest first, kept across runs.
namespace RecentColors {
constexpr int limit = 12;
std::vector<QColor> list();
void add(const QColor &color);
}

// The colour picker: field, hue strip, preview, RGB, hex and recent colours.
class ColorPickerSheet : public QWidget {
    Q_OBJECT
public:
    // `finish` gets the colour on OK, nothing on Cancel. `withAlpha` adds an opacity field (0 to 100%); without it the
    // colour is always opaque.
    ColorPickerSheet(const QColor &initial, std::function<void(std::optional<QColor>)> finish, QWidget *parent = nullptr, bool withAlpha = false);
    QColor color() const;
    const PickerHSB &hsb() const { return m_hsb; }
    void setHSB(const PickerHSB &hsb);
    void setAlphaPercent(int percent);

    // Shows a picker in `panel`, applying the colour on OK.
    static void showIn(FloatingPanel &panel, const QString &title, const QColor &initial, std::function<void(QColor)> apply, bool withAlpha = false);

signals:
    void colorChanged(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    PickerField *channel(int index);
    void pick(const std::function<void(PickerHSB &)> &edit);
    void setChannel(int channel, double value);
    void commitHex();
    void synchronize();

    const QColor m_original;
    PickerHSB m_hsb;
    int m_alphaPercent = 100;
    PickerField *m_alpha = nullptr;
    QWidget *const m_field;
    QWidget *const m_hue;
    QWidget *const m_preview;
    const std::array<PickerField *, 3> m_channels;
    PickerField *const m_hex;
    QPushButton *const m_ok;
    QPushButton *const m_cancel;
};
