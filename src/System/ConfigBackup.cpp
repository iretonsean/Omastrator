#include "System/ConfigBackup.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <algorithm>

namespace {
QString slugOf(const QString &title)
{
    QString slug;
    for (const QChar c : title.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128)
            slug += c;
        else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
            slug += QLatin1Char('-');
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.left(40);
}

QString manifestOf(const QString &folder)
{
    return QDir(folder).filePath(QStringLiteral("manifest.json"));
}
}

namespace ConfigBackup {
QString root()
{
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty())
        data = QDir::homePath() + QStringLiteral("/.local/share");
    return data + QStringLiteral("/omastrator/backups");
}

QString newFolder(const QString &title)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString folder = QDir(root()).filePath(stamp + QLatin1Char('-') + slugOf(title));
    // Two in the same millisecond get a number.
    for (int n = 2; QFileInfo::exists(folder); ++n)
        folder = QDir(root()).filePath(QStringLiteral("%1-%2-%3").arg(stamp, slugOf(title)).arg(n));
    return folder;
}

QString save(const QString &folder, const QString &title, const QStringList &paths, const std::vector<QStringList> &revertCommands)
{
    if (!QDir().mkpath(folder))
        return QStringLiteral("Couldn't make the backup folder %1. Nothing was written.").arg(folder);
    QJsonArray entries;
    int index = 0;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        QJsonObject entry{{"path", path}};
        if (info.isSymLink()) {
            // The link itself, not what it points at: that's what a change replaces.
            entry["existed"] = true;
            entry["link"] = info.symLinkTarget();
        } else if (info.exists()) {
            const QString copy = QStringLiteral("%1-%2").arg(++index, 3, 10, QLatin1Char('0')).arg(info.fileName());
            QFile::remove(QDir(folder).filePath(copy));
            if (!QFile::copy(path, QDir(folder).filePath(copy)))
                return QStringLiteral("Couldn't back up %1. Nothing was written.").arg(path);
            entry["existed"] = true;
            entry["copy"] = copy;
        } else {
            entry["existed"] = false;
        }
        entries.append(entry);
    }
    QJsonArray commands;
    for (const QStringList &command : revertCommands)
        commands.append(QJsonArray::fromStringList(command));
    const QJsonObject manifest{{"title", title},
                               {"when", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
                               {"entries", entries},
                               {"revertCommands", commands},
                               {"reverted", false}};
    QSaveFile file(manifestOf(folder));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(manifest).toJson()) < 0 || !file.commit())
        return QStringLiteral("Couldn't write the backup's manifest in %1. Nothing was written.").arg(folder);
    return {};
}

std::optional<Backup> read(const QString &folder)
{
    QFile file(manifestOf(folder));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
    if (manifest.isEmpty())
        return std::nullopt;
    Backup backup;
    backup.folder = folder;
    backup.id = QFileInfo(folder).fileName();
    backup.title = manifest["title"].toString();
    backup.when = QDateTime::fromString(manifest["when"].toString(), Qt::ISODateWithMs);
    backup.reverted = manifest["reverted"].toBool();
    for (const QJsonValue &value : manifest["entries"].toArray()) {
        const QJsonObject entry = value.toObject();
        backup.entries.push_back({entry["path"].toString(), entry["existed"].toBool(), entry["link"].toString(),
                                  entry["copy"].toString().isEmpty() ? QString() : QDir(folder).filePath(entry["copy"].toString())});
    }
    for (const QJsonValue &value : manifest["revertCommands"].toArray()) {
        QStringList command;
        for (const QJsonValue &word : value.toArray())
            command.append(word.toString());
        if (!command.isEmpty())
            backup.revertCommands.push_back(command);
    }
    return backup;
}

std::vector<Backup> list()
{
    std::vector<Backup> backups;
    const QStringList folders = QDir(root()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QString &name : folders) {
        if (const auto backup = read(QDir(root()).filePath(name)))
            backups.push_back(*backup);
    }
    return backups;
}

SyncPlan revertPlan(const Backup &backup)
{
    SyncPlan plan;
    plan.title = QStringLiteral("Revert “%1”").arg(backup.title);
    plan.confirmLabel = QStringLiteral("Revert");
    plan.destination = QStringLiteral("Your desktop's config, put back as it was before “%1” (%2), from the backup in %3. Nothing is committed or published.")
                           .arg(backup.title, QLocale::system().toString(backup.when, QLocale::ShortFormat), backup.folder);
    for (const Entry &entry : backup.entries) {
        FileWrite write;
        write.path = entry.path;
        const QFileInfo now(entry.path);
        if (!entry.existed) {
            write.remove = true;
        } else if (!entry.linkTarget.isEmpty()) {
            write.linkTo = entry.linkTarget;
            write.linkBefore = now.symLinkTarget();
        } else {
            QFile saved(entry.copy);
            if (!saved.open(QIODevice::ReadOnly)) {
                plan.problem = QStringLiteral("The backup's copy of %1 is missing from %2.").arg(entry.path, backup.folder);
                return plan;
            }
            write.after = saved.readAll();
            QFile current(entry.path);
            if (current.open(QIODevice::ReadOnly))
                write.before = current.readAll();
        }
        plan.writes.push_back(write);
    }
    plan.commands = backup.revertCommands;
    plan.revertCommands = backup.revertCommands;
    plan.backupFolder = newFolder(QStringLiteral("before revert ") + backup.title);
    const QString folder = backup.folder;
    plan.apply = [folder] {
        QFile file(manifestOf(folder));
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        manifest["reverted"] = true;
        QSaveFile out(manifestOf(folder));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(manifest).toJson());
            out.commit();
        }
        return QString();
    };
    if (plan.changes().empty())
        plan.note = QStringLiteral("Every file is already as the backup has it.");
    return plan;
}
}
