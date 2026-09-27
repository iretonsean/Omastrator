#pragma once
#include "Cloud/CloudStorage.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>

// Open, Save As, Place and Export with a location sidebar: This Computer (the normal dialog)
// and each connected remote, browsed here.
class CloudBrowser : public QDialog {
    Q_OBJECT
public:
    enum class Mode { open, save, place, exportFile };
    // `suffixes` without dots, lower case; empty accepts every file. `name` suggests a save's name.
    CloudBrowser(CloudStorage &storage, Mode mode, QStringList suffixes, const QString &name, QWidget *parent = nullptr);

    // The remote in view, or empty for This Computer.
    QString remoteShown() const { return m_folder ? m_folder->remote : QString(); }
    CloudLocation folder() const { return m_folder.value_or(CloudLocation{}); }
    void showRemote(const QString &remote);
    void navigate(const CloudLocation &folder);
    // Whether a listing is in flight.
    bool isLoading() const { return m_listing; }

signals:
    // This Computer: the caller opens its usual file dialog.
    void chooseLocal();
    // Open and Place: one or more files.
    void chosen(const QList<CloudLocation> &files);
    // Save and Export: where, and what's there now (exists false for a new name).
    void chosenSave(const CloudLocation &file, const CloudStamp &existing);

private:
    void fillLocations();
    void fillBreadcrumbs();
    void fillEntries(const QList<CloudEntry> &entries);
    void choose();
    void activate(QListWidgetItem *item);
    void makeFolder();
    void confirmReplace(const CloudLocation &file);
    bool accepts(const QString &name) const;

    CloudStorage &m_storage;
    const Mode m_mode;
    const QStringList m_suffixes;
    QListWidget *const m_locations;
    QStackedWidget *const m_pages;
    QWidget *const m_breadcrumbs;
    QHBoxLayout *m_crumbRow = nullptr;
    QListWidget *const m_entries;
    QLabel *const m_status;
    QLineEdit *const m_name;
    QPushButton *const m_choose;
    std::optional<CloudLocation> m_folder;
    QPointer<CloudJob> m_listing;
};
