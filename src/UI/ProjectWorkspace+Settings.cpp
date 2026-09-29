#include "IO/FileError.h"
#include "Logging.h"
#include "UI/ProjectWorkspace.h"
#include "UI/SettingsBundle.h"
#include "UI/SettingsTransfer.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {
const QString settingsFileName = QStringLiteral("Omastrator Settings.json");
}

void ProjectWorkspace::exportSettings()
{
    // To a remote: written to the cache, then uploaded in the background.
    const auto chosenSave = [this](const CloudLocation &file, const CloudStamp &) {
        QString local;
        try {
            local = CloudCache::localPath(file);
        } catch (const FileError &error) {
            showError(QStringLiteral("Couldn’t export settings"), error.message());
            return;
        }
        QDir().mkpath(QFileInfo(local).absolutePath());
        if (!exportSettingsTo(local))
            return;
        setNotice(QStringLiteral("Exporting settings to %1…").arg(m_cloud->serviceName(file.remote)));
        m_uploader->upload(QStringLiteral("export:") + file.toString(), local, file, std::nullopt);
    };
    const auto here = [this] {
        auto *panel = new QFileDialog(window, QStringLiteral("Export Settings"));
        panel->setAttribute(Qt::WA_DeleteOnClose);
        panel->setAcceptMode(QFileDialog::AcceptSave);
        panel->setNameFilter(QStringLiteral("Omastrator settings (*.json)"));
        panel->setDefaultSuffix(QStringLiteral("json"));
        panel->selectFile(settingsFileName);
        connect(panel, &QDialog::finished, this, [this, panel](int result) {
            if (result == QDialog::Accepted && !panel->selectedFiles().isEmpty())
                exportSettingsTo(panel->selectedFiles().constFirst());
        });
        panel->open();
    };
    if (!offerCloud(CloudBrowser::Mode::exportFile, {QStringLiteral("json")}, settingsFileName, here, {}, chosenSave))
        here();
}

bool ProjectWorkspace::exportSettingsTo(const QString &path)
{
    QStringList notes;
    if (const QString failure = SettingsBundle::exportTo(path, &notes); !failure.isEmpty()) {
        showError(QStringLiteral("Couldn’t export settings"), failure);
        return false;
    }
    qCInfo(lcIO).noquote() << "exported settings" << path;
    setNotice(QStringLiteral("Exported settings to “%1”").arg(QFileInfo(path).fileName()) + (notes.isEmpty() ? QString() : QLatin1Char(' ') + notes.join(QLatin1Char(' '))));
    return true;
}

void ProjectWorkspace::importSettings()
{
    const auto here = [this] {
        auto *panel = new QFileDialog(window, QStringLiteral("Import Settings"));
        panel->setAttribute(Qt::WA_DeleteOnClose);
        panel->setFileMode(QFileDialog::ExistingFile);
        panel->setNameFilter(QStringLiteral("Omastrator settings (*.json)"));
        connect(panel, &QDialog::finished, this, [this, panel](int result) {
            if (result == QDialog::Accepted && !panel->selectedFiles().isEmpty())
                importSettingsFrom(panel->selectedFiles().constFirst());
        });
        panel->open();
    };
    // From a remote: downloaded to a temporary folder that goes when the import is done.
    const auto chosen = [this](const QList<CloudLocation> &files) {
        if (files.isEmpty())
            return;
        const CloudLocation file = files.constFirst();
        auto folder = std::make_shared<QTemporaryDir>();
        if (!folder->isValid()) {
            showError(QStringLiteral("Couldn’t import settings"), QStringLiteral("There was no room to keep a copy from %1.").arg(m_cloud->serviceName(file.remote)));
            return;
        }
        const QString local = folder->filePath(QStringLiteral("settings.json"));
        setNotice(QStringLiteral("Getting settings from %1…").arg(m_cloud->serviceName(file.remote)));
        m_cloud->download(file, local, [this, file, local, folder](const QString &failure) {
            if (!failure.isEmpty()) {
                setNotice(QString());
                showError(QStringLiteral("Couldn’t get “%1”").arg(file.fileName()), failure);
                return;
            }
            setNotice(QString());
            importSettingsFrom(local);
        });
    };
    if (!offerCloud(CloudBrowser::Mode::open, {QStringLiteral("json")}, QString(), here, chosen, {}))
        here();
}

void ProjectWorkspace::importSettingsFrom(const QString &path)
{
    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(path, &error);
    if (!error.isEmpty()) {
        showError(QStringLiteral("Couldn’t import settings"), error);
        return;
    }
    if (plan.isEmpty()) {
        setNotice(QStringLiteral("These settings match the ones you have, so nothing changed."));
        return;
    }
    bool confirmed = false;
    QString backup;
    if (const QString failure = SettingsConfirmDialog::run(plan, window, &confirmed, &backup); !failure.isEmpty()) {
        showError(QStringLiteral("Couldn’t import settings"), failure);
        return;
    }
    if (!confirmed)
        return;
    qCInfo(lcIO).noquote() << "imported settings" << path << "backup" << backup;
    setNotice(QStringLiteral("Imported settings. The ones you had are in “%1”. Layout and swatch changes appear after Omastrator restarts.").arg(QFileInfo(backup).fileName()));
}
