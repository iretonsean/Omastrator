#pragma once
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizeF>
#include <QWidget>
#include <array>
#include <functional>
#include <optional>

enum class LengthUnit { pt, px, in, mm };

// The welcome: a new artboard, a document, a recent file.
class NewDocumentSheet : public QWidget {
    Q_OBJECT
public:
    NewDocumentSheet(std::function<void(QSizeF)> onCreate, std::function<void()> onOpen, std::function<void(const QString &)> onOpenRecent,
                     QWidget *parent = nullptr);

    struct Preset {
        const char *name;
        QSizeF points;
        LengthUnit unit;
    };
    // Letter, A4, A3, two screen sizes; Custom follows them.
    static const std::array<Preset, 5> presets;
    // Points per unit: pixels are points at 72 ppi.
    static double pointsPer(LengthUnit unit);
    // A length typed in `unit`, in points, when in range.
    static std::optional<double> dimension(const QString &text, LengthUnit unit);
    static constexpr double maximumPoints = 16384;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void choosePreset(int index);
    void changeUnit(int index);
    void validate();
    void create();
    LengthUnit unit() const;

    const std::function<void(QSizeF)> m_onCreate;
    QComboBox *const m_preset;
    QLineEdit *const m_width;
    QLineEdit *const m_height;
    QComboBox *const m_unit;
    QLabel *const m_note;
    QPushButton *const m_create;
    // The unit the fields were last written in.
    LengthUnit m_shownUnit = LengthUnit::pt;
};
