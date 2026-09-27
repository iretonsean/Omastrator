#include "Cloud/CloudProviders.h"
#include "IO/FileError.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include "UI/CloudBadge.h"
#include "UI/CloudStorageSheet.h"
#include "UI/ProjectWorkspace.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr int noticeMs = 8000;

QString keyOf(const ProjectTab &tab)
{
    return tab.id.toString();
}
}

void ProjectWorkspace::setUpCloud()
{
    m_cloud = new CloudStorage(this);
    m_uploader = new CloudUploader(*m_cloud, this);
    connect(m_uploader, &CloudUploader::statusChanged, this, &ProjectWorkspace::showUploadStatus);
    connect(m_uploader, &CloudUploader::uploaded, this, [this](const QString &key, const CloudLocation &target, const CloudStamp &stamp) {
        const QString service = m_cloud->serviceName(target.remote);
        if (key.startsWith(QLatin1String("export:"))) {
            setNotice(QStringLiteral("Exported “%1” to %2").arg(target.fileName(), service));
            return;
        }
        const std::shared_ptr<ProjectTab> saved = tab(QUuid::fromString(key));
        if (!saved || !saved->cloud)
            return;
        // Keep Both went to a new name: the tab follows its copy.
        if (!(saved->cloud->location == target)) {
            try {
                const QString moved = CloudCache::localPath(target);
                QDir().mkpath(QFileInfo(moved).absolutePath());
                QFile::remove(moved);
                if (saved->path && QFile::copy(*saved->path, moved))
                    saved->path = moved;
            } catch (const FileError &) {
            }
            saved->cloud->location = target;
            noteRecent(target.toString());
        }
        saved->cloud->base = stamp;
        CloudCache::record(target, stamp);
        saved->cloudStatus = QStringLiteral("Saved to %1").arg(service);
        emit changed();
    });
    connect(m_uploader, &CloudUploader::conflicted, this, [this](const QString &key) {
        // While a close is deciding, its own alert covers this.
        if (const std::shared_ptr<ProjectTab> saved = tab(QUuid::fromString(key)); saved && !m_isManaging)
            askConflict(saved);
    });
    connect(m_cloud, &CloudStorage::remotesChanged, this, &ProjectWorkspace::changed);
}

void ProjectWorkspace::connectCloud()
{
    (new CloudStorageSheet(*m_cloud, window))->open();
}

void ProjectWorkspace::setNotice(const QString &text)
{
    m_notice = text;
    const int number = ++m_noticeNumber;
    if (!text.isEmpty()) {
        QTimer::singleShot(noticeMs, this, [this, number] {
            if (number == m_noticeNumber) {
                m_notice.clear();
                emit changed();
            }
        });
    }
    emit changed();
}

QString ProjectWorkspace::cloudStatusText() const
{
    return m_notice.isEmpty() ? current().cloudStatus : m_notice;
}

CloudUploader::Status ProjectWorkspace::uploadStatus(QUuid id) const
{
    return m_uploader->status(id.toString());
}

QStringList ProjectWorkspace::suffixesOf(const QStringList &filters)
{
    QStringList suffixes;
    static const QRegularExpression pattern(QStringLiteral("\\*\\.([A-Za-z0-9]+)"));
    for (const QString &filter : filters) {
        for (const QRegularExpressionMatch &match : pattern.globalMatch(filter)) {
            const QString suffix = match.captured(1).toLower();
            if (!suffixes.contains(suffix))
                suffixes << suffix;
        }
    }
    return suffixes;
}

QString ProjectWorkspace::recentLabel(const QString &entry)
{
    if (!entry.startsWith(QLatin1Char('/'))) {
        if (const std::optional<CloudLocation> cloud = CloudLocation::parse(entry)) {
            const QString type = CloudStorage::rememberedType(cloud->remote);
            return QStringLiteral("%1 — %2").arg(cloud->fileName(), type.isEmpty() ? cloud->remote : CloudProviders::forType(type).name);
        }
    }
    return QFileInfo(entry).fileName();
}

QIcon ProjectWorkspace::recentIcon(const QString &entry)
{
    if (!entry.startsWith(QLatin1Char('/'))) {
        if (const std::optional<CloudLocation> cloud = CloudLocation::parse(entry))
            return CloudBadge::icon(CloudStorage::rememberedType(cloud->remote));
    }
    return {};
}

bool ProjectWorkspace::offerCloud(CloudBrowser::Mode mode, const QStringList &suffixes, const QString &name, std::function<void()> local,
                                  std::function<void(const QList<CloudLocation> &)> chosen,
                                  std::function<void(const CloudLocation &, const CloudStamp &)> chosenSave, std::function<void()> cancelled)
{
    if (m_cloud->remotes().isEmpty() || !CloudStorage::isInstalled())
        return false;
    auto *browser = new CloudBrowser(*m_cloud, mode, suffixes, name, window);
    connect(browser, &CloudBrowser::chooseLocal, this, [local] { local(); });
    if (chosen)
        connect(browser, &CloudBrowser::chosen, this, [chosen](const QList<CloudLocation> &files) { chosen(files); });
    if (chosenSave)
        connect(browser, &CloudBrowser::chosenSave, this, [chosenSave](const CloudLocation &file, const CloudStamp &existing) { chosenSave(file, existing); });
    if (cancelled)
        connect(browser, &QDialog::rejected, this, [cancelled] { cancelled(); });
    browser->open();
    return true;
}

void ProjectWorkspace::openCloud(const CloudLocation &file)
{
    for (const std::shared_ptr<ProjectTab> &existing : m_tabs) {
        if (existing->cloud && existing->cloud->location == file) {
            select(existing->id);
            return;
        }
    }
    const QString service = m_cloud->serviceName(file.remote);
    QString local;
    try {
        local = CloudCache::localPath(file);
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t open “%1”").arg(file.fileName()), error.message());
        return;
    }
    // Offline: the copy from last time, if there is one, still opens.
    const auto fallBack = [this, file, local, service](const QString &error) {
        setNotice(QString());
        const std::optional<CloudStamp> recorded = CloudCache::recorded(file);
        if (recorded && QFileInfo::exists(local)) {
            adoptCloud(file, local, recorded, QStringLiteral("Opened the copy on this computer; %1 couldn’t be reached").arg(service));
            return;
        }
        showError(QStringLiteral("Couldn’t open “%1” from %2").arg(file.fileName(), service), error);
    };
    setNotice(QStringLiteral("Opening “%1” from %2…").arg(file.fileName(), service));
    // The stamp comes first: a change between it and the download reads as a conflict, never as nothing.
    m_cloud->stat(file, [this, file, local, service, fallBack](const CloudStamp &stamp, const QString &error) {
        if (!error.isEmpty()) {
            fallBack(error);
            return;
        }
        if (!stamp.exists) {
            setNotice(QString());
            showError(QStringLiteral("Couldn’t open “%1” from %2").arg(file.fileName(), service), QStringLiteral("It isn’t there any more."));
            return;
        }
        m_cloud->download(file, local, [this, file, local, stamp, fallBack](const QString &failure) {
            if (!failure.isEmpty()) {
                fallBack(failure);
                return;
            }
            CloudCache::record(file, stamp);
            setNotice(QString());
            adoptCloud(file, local, stamp, QString());
        });
    });
}

void ProjectWorkspace::adoptCloud(const CloudLocation &file, const QString &local, const std::optional<CloudStamp> &base, const QString &status)
{
    if (!openFile(local))
        return;
    forgetRecent(local);
    noteRecent(file.toString());
    // SVGs and pictures open as new documents; only an .omai saves back to where it came from.
    if (QFileInfo(local).suffix().compare(QLatin1String(ProjectStore::extension), Qt::CaseInsensitive) != 0)
        return;
    ProjectTab &opened = current();
    opened.cloud = ProjectTab::CloudLink{file, base};
    opened.cloudStatus = status;
    qCInfo(lcIO).noquote() << "opened" << file.toString();
    emit changed();
}

bool ProjectWorkspace::saveToCloud(ProjectTab &saving, const CloudLocation &file, const CloudStamp &existing)
{
    QString local;
    try {
        local = CloudCache::localPath(file);
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t save “%1”").arg(file.fileName()), error.message());
        return false;
    }
    QDir().mkpath(QFileInfo(local).absolutePath());
    const std::optional<ProjectTab::CloudLink> before = saving.cloud;
    if (before && !(before->location == file))
        m_uploader->cancel(keyOf(saving));
    saving.cloud = ProjectTab::CloudLink{file, existing};
    if (!saveTo(saving, local)) {
        saving.cloud = before;
        return false;
    }
    return true;
}

bool ProjectWorkspace::syncCloud(ProjectTab &saved)
{
    if (!saved.cloud)
        return false;
    QString local;
    try {
        local = CloudCache::localPath(saved.cloud->location);
    } catch (const FileError &) {
    }
    // Saved As somewhere on this computer: no longer a cloud document.
    if (!saved.path || QFileInfo(*saved.path).absoluteFilePath() != QFileInfo(local).absoluteFilePath()) {
        m_uploader->cancel(keyOf(saved));
        saved.cloud.reset();
        saved.cloudStatus.clear();
        return false;
    }
    noteRecent(saved.cloud->location.toString());
    m_uploader->upload(keyOf(saved), local, saved.cloud->location, saved.cloud->base);
    return true;
}

void ProjectWorkspace::showUploadStatus(const QString &key)
{
    const std::shared_ptr<ProjectTab> shown = tab(QUuid::fromString(key));
    const CloudUploader::Status status = m_uploader->status(key);
    const QString service = m_cloud->serviceName(status.target.remote);
    QString text;
    switch (status.phase) {
    case CloudUploader::Phase::idle: return;
    case CloudUploader::Phase::checking:
    case CloudUploader::Phase::uploading: text = QStringLiteral("Uploading to %1…").arg(service); break;
    case CloudUploader::Phase::waiting:
        text = QStringLiteral("Upload failed — retrying in %1 s").arg(std::max(1, (status.retryInMs + 999) / 1000));
        break;
    case CloudUploader::Phase::conflict: text = QStringLiteral("Not uploaded: changed on %1").arg(service); break;
    }
    if (!shown) {
        if (status.phase == CloudUploader::Phase::waiting)
            setNotice(QStringLiteral("Export upload failed — retrying"));
        return;
    }
    shown->cloudStatus = text;
    emit changed();
}

void ProjectWorkspace::retryUpload(QUuid id)
{
    m_uploader->retryNow(id.toString());
}

void ProjectWorkspace::resolveConflict(QUuid id)
{
    if (const std::shared_ptr<ProjectTab> conflicted = tab(id); conflicted && uploadStatus(id).phase == CloudUploader::Phase::conflict)
        askConflict(conflicted);
}

void ProjectWorkspace::askConflict(const std::shared_ptr<ProjectTab> &conflicted)
{
    if (!conflicted->cloud)
        return;
    const QString key = keyOf(*conflicted);
    const CloudLocation file = conflicted->cloud->location;
    const QString service = m_cloud->serviceName(file.remote);
    auto *alert = new QMessageBox(window);
    alert->setObjectName(QStringLiteral("cloudConflictAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(QStringLiteral("“%1” changed on %2 since you opened it.").arg(file.fileName(), service));
    alert->setInformativeText(QStringLiteral("Your save is on this computer and hasn’t been uploaded. Keep Both uploads yours as “%1”. "
                                             "Overwrite replaces their version with yours. Open Theirs opens their version in a new tab "
                                             "and keeps yours open, unsaved.")
                                  .arg(CloudUploader::conflictName(file.fileName(), QDateTime::currentDateTime())));
    const QPushButton *both = alert->addButton(QStringLiteral("Keep Both"), QMessageBox::AcceptRole);
    const QPushButton *overwrite = alert->addButton(QStringLiteral("Overwrite"), QMessageBox::DestructiveRole);
    const QPushButton *theirs = alert->addButton(QStringLiteral("Open Theirs"), QMessageBox::ActionRole);
    alert->addButton(QStringLiteral("Decide Later"), QMessageBox::RejectRole);
    connect(alert, &QDialog::finished, this, [this, alert, both, overwrite, theirs, key, conflicted] {
        if (m_uploader->status(key).phase != CloudUploader::Phase::conflict)
            return;
        if (alert->clickedButton() == both)
            m_uploader->keepBoth(key);
        else if (alert->clickedButton() == overwrite)
            m_uploader->overwrite(key);
        else if (alert->clickedButton() == theirs)
            openTheirs(conflicted);
    });
    alert->open();
}

// Yours stays open as an unsaved document; theirs opens from the remote in its own tab.
void ProjectWorkspace::openTheirs(const std::shared_ptr<ProjectTab> &yours)
{
    if (!yours->cloud)
        return;
    const CloudLocation file = yours->cloud->location;
    m_uploader->cancel(keyOf(*yours));
    yours->defaultName = QStringLiteral("%1 (yours)").arg(yours->title());
    yours->cloud.reset();
    yours->path.reset();
    yours->cloudStatus.clear();
    yours->session.markUnsaved();
    emit changed();
    openCloud(file);
}

void ProjectWorkspace::settleUpload(const std::shared_ptr<ProjectTab> &closing, std::function<void(bool)> then)
{
    const QString key = keyOf(*closing);
    if (!m_uploader->isPending(key)) {
        then(true);
        return;
    }
    const CloudUploader::Phase phase = m_uploader->status(key).phase;
    if (phase != CloudUploader::Phase::checking && phase != CloudUploader::Phase::uploading) {
        askUnsent(closing, then);
        return;
    }
    // Under way: wait for it, and ask only if it fails or conflicts.
    auto *watch = new QObject(this);
    connect(m_uploader, &CloudUploader::statusChanged, watch, [this, watch, key, closing, then](const QString &changedKey) {
        if (changedKey != key)
            return;
        if (!m_uploader->isPending(key)) {
            watch->deleteLater();
            then(true);
            return;
        }
        const CloudUploader::Phase now = m_uploader->status(key).phase;
        if (now == CloudUploader::Phase::waiting || now == CloudUploader::Phase::conflict) {
            watch->deleteLater();
            askUnsent(closing, then);
        }
    });
}

void ProjectWorkspace::askUnsent(const std::shared_ptr<ProjectTab> &closing, std::function<void(bool)> then)
{
    const QString key = keyOf(*closing);
    const CloudUploader::Status status = m_uploader->status(key);
    const QString service = m_cloud->serviceName(status.target.remote);
    m_selectedID = closing->id;
    emit changed();
    auto *alert = new QMessageBox(window);
    alert->setObjectName(QStringLiteral("uploadPendingAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(status.phase == CloudUploader::Phase::conflict
                       ? QStringLiteral("“%1” changed on %2, so your save wasn’t uploaded.").arg(closing->title(), service)
                       : QStringLiteral("“%1” isn’t uploaded to %2 yet.").arg(closing->title(), service));
    alert->setInformativeText(QStringLiteral("Your save is on this computer at %1. Closing stops the upload.").arg(closing->path.value_or(QString())));
    alert->addButton(QStringLiteral("Keep Open"), QMessageBox::RejectRole);
    const QPushButton *close = alert->addButton(QStringLiteral("Close Anyway"), QMessageBox::DestructiveRole);
    connect(alert, &QDialog::finished, this, [this, alert, close, key, then] {
        const bool closing = alert->clickedButton() == close;
        if (closing)
            m_uploader->cancel(key);
        then(closing);
    });
    alert->open();
}
