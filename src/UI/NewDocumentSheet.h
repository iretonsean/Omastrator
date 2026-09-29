#pragma once
#include "UI/PresetStore.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizeF>
#include <QToolButton>
#include <QWidget>
#include <array>
#include <functional>
#include <optional>

// The welcome: a new artboard, a document, a recent file.
class NewDocumentSheet : public QWidget {
    Q_OBJECT
public:
    // With `onCloud`, a Cloud Storage… button opens the storage sheet.
    NewDocumentSheet(std::function<void(QSizeF)> onCreate, std::function<void()> onOpen, std::function<void(const QString &)> onOpenRecent,
                     QWidget *parent = nullptr, std::function<void()> onCloud = {});

    struct Preset {
        const char *name;
        QSizeF points;
        LengthUnit unit;
    };
    // Letter, A4, A3, two screen sizes. The list shows the presets the user saved
    // (PresetStore, newest first), then these except the ones they hid, then Custom.
    static const std::array<Preset, 5> presets;
    // Asks for a preset's name; nothing means cancelled. Tests answer it here.
    using Namer = std::function<std::optional<QString>(QWidget *parent, const QString &title, const QString &initial)>;
    static void setNamer(Namer namer);
    // Points per unit: pixels are points at 72 ppi.
    static double pointsPer(LengthUnit unit);
    // A length typed in `unit`, in points, when in range.
    static std::optional<double> dimension(const QString &text, LengthUnit unit);
    static constexpr double maximumPoints = 16384;

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct Choice {
        PresetStore::Entry entry;
        bool builtIn = false;
    };
    void fillPresets(const QString &select);
    int customIndex() const { return int(m_choices.size()); }
    const Choice *chosen() const;
    QString nameProblem(const QString &name, const QString &ignoring) const;
    std::optional<QString> askName(const QString &title, const QString &initial);
    void savePreset();
    void renamePreset();
    void deletePreset();
    void hidePreset();
    void showHiddenPresets();
    void store(const PresetStore::Section &section, const QString &select);
    void updatePresetMenu();
    void choosePreset(int index);
    void changeUnit(int index);
    void validate();
    void create();
    LengthUnit unit() const;

    const std::function<void(QSizeF)> m_onCreate;
    QComboBox *const m_preset;
    QToolButton *const m_presetMenu;
    QAction *m_save = nullptr, *m_rename = nullptr, *m_delete = nullptr, *m_hide = nullptr, *m_showHidden = nullptr;
    std::vector<Choice> m_choices;
    PresetStore::Section m_stored;
    QLineEdit *const m_width;
    QLineEdit *const m_height;
    QComboBox *const m_unit;
    QLabel *const m_note;
    QPushButton *const m_create;
    // The unit the fields were last written in.
    LengthUnit m_shownUnit = LengthUnit::pt;
};
