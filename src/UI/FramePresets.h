#pragma once
#include "Document/EditorSession.h"
#include "UI/PanelSection.h"
#include "UI/PresetStore.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QSizeF>
#include <QToolButton>
#include <functional>
#include <optional>
#include <vector>

namespace FramePresets {
struct Preset {
    const char *group;
    const char *name;
    QSizeF size;
};
// Phone, tablet, desktop, social and paper sizes, in points (pixels at 72 ppi); Figma's names.
const std::vector<Preset> &builtIn();
}

// Properties ▸ Frame, shown while the Frame tool is active: a list of sizes by group, and a click
// drops a frame of that size in the middle of the view. What the user saved (PresetStore's "frames"
// section, newest first) is a group of its own; built-ins can be hidden but not renamed or deleted.
class FramePresetsSection : public PanelSection {
    Q_OBJECT
public:
    FramePresetsSection(EditorSession &session, QWidget *parent);
    // Asks for a preset's name; nothing means cancelled. Tests answer it here.
    using Namer = std::function<std::optional<QString>(QWidget *parent, const QString &title, const QString &initial)>;
    static void setNamer(Namer namer);
    // Reads the file again, so a change made in another window shows.
    void reload();
    // Drops a frame of `size`, named `name`, then hands over to the Select tool as Figma does.
    void drop(const QString &name, QSizeF size);
    // The selected frame's size, under a name the user gives.
    void saveSelectedFrame();
    void renameSaved(const QString &name);
    void deleteSaved(const QString &name);
    void hideBuiltIn(const QString &name);
    void showHidden();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Row {
        QString group, name;
        QSizeF size;
        bool builtIn = false;
    };
    QString nameProblem(const QString &name, const QString &ignoring) const;
    std::optional<QString> askName(const QString &title, const QString &initial);
    void store(const PresetStore::Section &section, const QString &prefer);
    // Shows `prefer`'s group, else the one shown, else the one the user last chose.
    void fill(const QString &prefer = {});
    void rowMenu(QListWidgetItem *item, QPoint at);
    void updateMenu();

    EditorSession &m_session;
    QComboBox *const m_group;
    QListWidget *const m_list;
    QToolButton *const m_options;
    QAction *m_save = nullptr, *m_showHidden = nullptr;
    QLabel *const m_note;
    PresetStore::Section m_stored;
    bool m_loaded = false;
    std::vector<Row> m_rows;
};
