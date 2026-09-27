#pragma once
#include "Cloud/CloudUploader.h"
#include "Document/EditorSession.h"
#include "IO/DocumentExporter.h"
#include "UI/CloudBrowser.h"
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// One open document: its session and its file.
class ProjectTab {
public:
    explicit ProjectTab(QString name);

    const QUuid id = QUuid::createUuid();
    // Changes only when a conflict leaves "yours" apart from theirs.
    QString defaultName;
    EditorSession session;
    // The .omai file; SVGs and images open without one.
    std::optional<QString> path;
    // A cloud document: `path` is its cache copy, uploaded to `location` after each save.
    struct CloudLink {
        CloudLocation location;
        // The remote as it was when opened or last uploaded; a different one is a conflict.
        std::optional<CloudStamp> base;
    };
    std::optional<CloudLink> cloud;
    QString cloudStatus;

    QString title() const;
    // Where it lives, for tooltips: "work:Designs/logo.omai" or the local path.
    QString place() const;
    static QString nameWithoutSuffix(const QString &path);
};

// A picture export's pixels per point, JPEG quality and paper.
struct RasterOptions {
    double scale = 1;
    int quality = 90;
    bool transparent = false;
};

// The documents open in one window, one in front.
class ProjectWorkspace : public QObject {
    Q_OBJECT
public:
    ProjectWorkspace();

    const std::vector<std::shared_ptr<ProjectTab>> &tabs() const { return m_tabs; }
    QUuid selectedID() const { return m_selectedID; }
    // True while an alert or dialog decides a tab's fate.
    bool isManaging() const { return m_isManaging; }
    // Dialogs and alerts open over it.
    QPointer<QWidget> window;
    // Set, failures come here instead of an alert: the agent bridge reports them itself.
    std::function<void(const QString &title, const QString &message)> errorHandler;

    ProjectTab &current() const;
    std::shared_ptr<ProjectTab> tab(QUuid id) const;
    ProjectTab &addTab(bool reuseEmpty = true, const QString &name = QString());
    void select(QUuid id);
    // A tab showing the welcome sheet.
    void newTab();
    // A blank artboard, in a new tab if needed.
    void createDocument(QSizeF size);

    // Files: failures show their FileError in an alert.
    bool openFile(const QString &path);
    bool saveTo(ProjectTab &tab, const QString &path);
    bool placeFile(const QString &path);
    bool exportTo(const QString &path, DocumentExporter::Format format, const RasterOptions &options = {});
    // The dialogs in front of those.
    void open();
    void save(QUuid id, bool asNew = false, std::function<void(bool)> done = {});
    void place();
    void exportAs(DocumentExporter::Format format);
    // Each `done` runs from the event loop, never inline.
    void close(QUuid id, std::function<void()> done = {});
    void confirmQuit(std::function<void(bool)> done);
    void closeWindow(QWidget *closing);
    // Files named at launch; unknown ones are reported.
    void receive(const QStringList &paths);

    // Cloud storage: rclone's remotes, and uploads after each save.
    CloudStorage &cloud() const { return *m_cloud; }
    CloudUploader &uploads() const { return *m_uploader; }
    void openCloud(const CloudLocation &file);
    bool saveToCloud(ProjectTab &tab, const CloudLocation &file, const CloudStamp &existing);
    // File ▸ Connect Cloud Storage…
    void connectCloud();
    void retryUpload(QUuid id);
    void resolveConflict(QUuid id);
    CloudUploader::Status uploadStatus(QUuid id) const;
    // The status line: a passing notice, else the front tab's upload.
    QString cloudStatusText() const;
    // Recent entries: a cloud one reads "logo.omai — Google Drive" with its badge.
    static QString recentLabel(const QString &entry);
    static QIcon recentIcon(const QString &entry);
    // "png", "svg"… from dialog filters.
    static QStringList suffixesOf(const QStringList &filters);

    static QStringList recentFiles();
    static void noteRecent(const QString &path);
    static void clearRecent();
    // Every type Open reads, for its dialog.
    static QStringList openFilters();

signals:
    void changed();

private:
    void setManaging(bool managing);
    void finish(const std::function<void()> &done);
    void finish(const std::function<void(bool)> &done, bool value);
    void showError(const QString &title, const QString &message);
    void reportLeftOut(const QString &path, const QStringList &warnings);
    // Save, Don't Save or Cancel for a tab with changes.
    void confirmClose(const std::shared_ptr<ProjectTab> &tab, std::function<void(bool)> then);
    void askNext(std::vector<std::shared_ptr<ProjectTab>> order, size_t index, std::function<void(bool)> done);
    void adopt(std::shared_ptr<ProjectTab> tab);
    void removeTab(QUuid id);
    QString suggestedName(const QString &suffix) const;
    void openHere();
    void saveHere(const std::shared_ptr<ProjectTab> &saving, bool asNew, std::function<void(bool)> done);
    void placeHere();
    // The cloud browser when a remote is connected; false means use the local dialog.
    bool offerCloud(CloudBrowser::Mode mode, const QStringList &suffixes, const QString &name, std::function<void()> local,
                    std::function<void(const QList<CloudLocation> &)> chosen, std::function<void(const CloudLocation &, const CloudStamp &)> chosenSave,
                    std::function<void()> cancelled = {});
    void setUpCloud();
    void adoptCloud(const CloudLocation &file, const QString &local, const std::optional<CloudStamp> &base, const QString &status);
    // After a local save: the upload for a cloud tab, or the link dropped when saved elsewhere.
    bool syncCloud(ProjectTab &tab);
    void showUploadStatus(const QString &key);
    void askConflict(const std::shared_ptr<ProjectTab> &tab);
    void openTheirs(const std::shared_ptr<ProjectTab> &tab);
    void settleUpload(const std::shared_ptr<ProjectTab> &tab, std::function<void(bool)> then);
    void askUnsent(const std::shared_ptr<ProjectTab> &tab, std::function<void(bool)> then);
    void setNotice(const QString &text);
    static void forgetRecent(const QString &path);

    std::vector<std::shared_ptr<ProjectTab>> m_tabs;
    QUuid m_selectedID;
    bool m_isManaging = false;
    int m_nextNumber = 2;
    CloudStorage *m_cloud = nullptr;
    CloudUploader *m_uploader = nullptr;
    QString m_notice;
    int m_noticeNumber = 0;
};
