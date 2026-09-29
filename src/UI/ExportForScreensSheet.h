#pragma once
#include "Document/EditorSession.h"
#include <QDialog>
#include <QUuid>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

// File ▸ Export ▸ Export for Screens… (docs/QOL-RESEARCH.md P2-10): artboards and export
// assets, a batch of scales and formats, written in one go through ScreenExport.
class ExportForScreensSheet : public QDialog {
    Q_OBJECT
public:
    explicit ExportForScreensSheet(EditorSession &session, QWidget *parent = nullptr);

private:
    void browse();
    void runExport();
    void restoreSettings();
    void saveSettings() const;
    std::vector<QUuid> checkedArtboards() const;
    std::vector<QUuid> checkedAssets() const;
    std::vector<double> checkedScales() const;
    QStringList checkedFormats() const;

    EditorSession &m_session;
    QListWidget *const m_artboards;
    QListWidget *const m_assets;
    std::vector<std::pair<double, QCheckBox *>> m_scales;
    std::vector<std::pair<QString, QCheckBox *>> m_formats;
    QLineEdit *const m_folder;
    QProgressBar *const m_progress;
    QLabel *const m_status;
    QPushButton *const m_export;
    QPushButton *const m_openFolder;
};
