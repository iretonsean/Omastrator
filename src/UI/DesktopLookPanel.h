#pragma once
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <array>
#include <functional>

class DesignController;
class NumberField;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

// Desktop Look (docs/ANYWHERE.md, phase 4): Omarchy's gaps and borders, the
// bar, the font, the wallpaper and the theme's colours, and restyling the app
// pointed at. Every change previews on the desktop at once; Save shows the
// confirmation dialog with every file, diff and command, and History reverts.
class DesktopLookPanel : public QWidget {
    Q_OBJECT
public:
    DesktopLookPanel(DesignController &controller, QWidget *parent = nullptr);

    // "windows", "bar", "font", "wallpaper", "colours" or "app"; shows and raises the panel.
    void showSection(const QString &section);

protected:
    // Closing drops the unsaved preview and the gap handles: the desktop goes back to how it was.
    void closeEvent(QCloseEvent *event) override;

public:
    // Reads the desktop and the pending edit again.
    void reload();

    QTabWidget *tabs() const { return m_tabs; }
    NumberField *field(const QString &key) const { return m_fields.value(key); }
    QPushButton *colorButton(const QString &key) const { return m_colors.value(key); }
    QComboBox *barPosition() const { return m_barPosition; }
    QListWidget *barList(int section) const { return m_barLists[size_t(section)]; }
    QComboBox *history() const { return m_history; }
    QLabel *note() const { return m_note; }
    QLabel *appInfo() const { return m_appInfo; }
    // The last thing the controller said about an action here.
    QString lastError() const { return m_lastError; }
    // Tests pick colours instead of a person: the dialog isn't shown.
    static void setColorResponder(std::function<QColor(const QString &key, const QColor &initial)> responder);
    // A change as the controls make it.
    void preview(const QJsonObject &edits);
    void pickColor(const QString &key);
    void save();
    void discard();
    void revertSelected();
    void previewApp();
    void saveApp();

private:
    QWidget *windowsPage();
    QWidget *barPage();
    QWidget *fontPage();
    QWidget *wallpaperPage();
    QWidget *coloursPage();
    QWidget *appPage();
    NumberField *numberField(const QString &key, const QString &label, double minimum, double maximum, QWidget *parent);
    QPushButton *swatch(const QString &key, QWidget *parent);
    void setSwatch(QPushButton *button, const QColor &color);
    void barOrderChanged();
    QString run(const QString &method, const QJsonObject &params, QJsonObject *result = nullptr);

    DesignController &m_controller;
    QTabWidget *m_tabs = nullptr;
    QHash<QString, NumberField *> m_fields;
    QHash<QString, QPushButton *> m_colors;
    QComboBox *m_barPosition = nullptr;
    QCheckBox *m_barTransparent = nullptr;
    std::array<QListWidget *, 3> m_barLists{};
    QComboBox *m_font = nullptr;
    QLabel *m_wallpaper = nullptr;
    QGridLayout *m_palette = nullptr;
    QWidget *m_paletteHost = nullptr;
    QLabel *m_note = nullptr;
    QComboBox *m_history = nullptr;
    QLabel *m_appInfo = nullptr;
    QComboBox *m_appFont = nullptr;
    QJsonObject m_look;
    QJsonObject m_style;
    QTimer m_barTimer;
    bool m_filling = false;
    QString m_lastError;
};
