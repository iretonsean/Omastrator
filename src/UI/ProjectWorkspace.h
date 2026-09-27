#pragma once
#include "Document/EditorSession.h"
#include "IO/DocumentExporter.h"
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
    const QString defaultName;
    EditorSession session;
    // The .omai file; SVGs and images open without one.
    std::optional<QString> path;

    QString title() const;
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

    std::vector<std::shared_ptr<ProjectTab>> m_tabs;
    QUuid m_selectedID;
    bool m_isManaging = false;
    int m_nextNumber = 2;
};
