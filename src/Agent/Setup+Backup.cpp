#include "Agent/Setup.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace {
std::optional<QByteArray> readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

QString saveBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}

// A backup's folder name: the time, and a number when two land in the same second.
const QRegularExpression &backupName()
{
    static const QRegularExpression name(QStringLiteral("^\\d{8}-\\d{6}(-\\d{2})?$"));
    return name;
}

// Backup folder names, newest first: the time sorts as text, and "-02" comes after the same second's first.
QStringList backupNames(const Setup::Environment &environment)
{
    QStringList names = QDir(environment.backups()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    names.erase(std::remove_if(names.begin(), names.end(), [](const QString &name) { return !backupName().match(name).hasMatch(); }), names.end());
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        const QString base = a.left(15), other = b.left(15);
        return base != other ? base > other : a.size() != b.size() ? a.size() > b.size() : a > b;
    });
    return names;
}

std::optional<Setup::Backup> readManifest(const QString &folder)
{
    const auto bytes = readBytes(QDir(folder).filePath(QStringLiteral("manifest.json")));
    if (!bytes)
        return std::nullopt;
    const QJsonObject json = QJsonDocument::fromJson(*bytes).object();
    if (json["version"].toInt() != 1 || !json["entries"].isArray())
        return std::nullopt;
    Setup::Backup backup;
    backup.folder = QDir::cleanPath(folder);
    backup.name = QFileInfo(backup.folder).fileName();
    backup.created = json["created"].toString();
    backup.action = json["action"].toString();
    for (const QJsonValue &value : json["entries"].toArray()) {
        const QJsonObject entry = value.toObject();
        backup.entries.push_back({entry["path"].toString(), entry["existed"].toBool(), entry["stored"].toString()});
    }
    for (const QJsonValue &directory : json["createdDirectories"].toArray())
        backup.createdDirectories << directory.toString();
    return backup;
}

bool ask(QTextStream &in, QTextStream &out, const QString &question)
{
    out << question << " [y/N] ";
    out.flush();
    const QString answer = in.readLine().trimmed().toLower();
    return answer == QLatin1String("y") || answer == QLatin1String("yes");
}
}

namespace Setup {
QString newBackupName(const Environment &environment)
{
    const QString base = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    // One more than any of this second's, never a gap an earlier prune left: the newest name must be the newest backup.
    int last = 0;
    for (const QString &existing : backupNames(environment)) {
        if (existing == base)
            last = std::max(last, 1);
        else if (existing.startsWith(base + QLatin1Char('-')))
            last = std::max(last, existing.mid(base.size() + 1).toInt());
    }
    QString name = last == 0 ? base : base + QStringLiteral("-%1").arg(last + 1, 2, 10, QLatin1Char('0'));
    return name;
}

QString writeBackup(const Environment &environment, const QString &name, const QString &action, const QStringList &paths, Backup *made)
{
    Backup backup;
    backup.name = name;
    backup.folder = QDir(environment.backups()).filePath(name);
    backup.created = QDateTime::currentDateTime().toString(Qt::ISODate);
    backup.action = action;
    // Nothing changes if this fails, so what it made is taken away again.
    const auto fail = [&](const QString &why) {
        QDir(backup.folder).removeRecursively();
        return why;
    };
    if (!QDir().mkpath(backup.folder))
        return QStringLiteral("Could not make %1.").arg(backup.folder);

    for (const QString &path : paths) {
        BackupEntry entry;
        entry.path = path;
        for (QString folder = QFileInfo(path).absolutePath(); !QFileInfo::exists(folder); folder = QFileInfo(folder).absolutePath()) {
            if (!backup.createdDirectories.contains(folder))
                backup.createdDirectories << folder;
        }
        if (QFileInfo(path).isFile()) {
            entry.existed = true;
            entry.stored = QStringLiteral("files/") + QDir::cleanPath(QFileInfo(path).absoluteFilePath()).mid(1);
            const auto original = readBytes(path);
            if (!original)
                return fail(QStringLiteral("Could not read %1.").arg(path));
            if (const QString failure = saveBytes(QDir(backup.folder).filePath(entry.stored), *original); !failure.isEmpty())
                return fail(failure);
            // The copy is only worth having if it reads back the same.
            if (readBytes(QDir(backup.folder).filePath(entry.stored)) != original)
                return fail(QStringLiteral("The copy of %1 doesn't match it.").arg(path));
            QFile::setPermissions(QDir(backup.folder).filePath(entry.stored), QFile::permissions(path));
        } else if (QFileInfo::exists(path)) {
            return fail(QStringLiteral("%1 isn't a file, so setup can't copy it.").arg(path));
        }
        backup.entries.push_back(entry);
    }
    std::sort(backup.createdDirectories.begin(), backup.createdDirectories.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });

    QJsonArray entries;
    for (const BackupEntry &entry : backup.entries)
        entries.append(QJsonObject{{"path", entry.path}, {"existed", entry.existed}, {"stored", entry.stored}});
    // Written last: a folder without it is a copy that was never finished, and setup hadn't started.
    const QByteArray manifest = QJsonDocument(QJsonObject{{"version", 1},
                                                          {"created", backup.created},
                                                          {"action", action},
                                                          {"entries", entries},
                                                          {"createdDirectories", QJsonArray::fromStringList(backup.createdDirectories)}})
                                    .toJson(QJsonDocument::Indented);
    if (const QString failure = saveBytes(QDir(backup.folder).filePath(QStringLiteral("manifest.json")), manifest); !failure.isEmpty())
        return fail(failure);
    if (made)
        *made = backup;
    return {};
}

void pruneBackups(const Environment &environment, const QStringList &except, int keep)
{
    const QStringList names = backupNames(environment);
    for (qsizetype at = keep; at < names.size(); ++at) {
        if (!except.contains(names[at]))
            QDir(QDir(environment.backups()).filePath(names[at])).removeRecursively();
    }
}

std::vector<Backup> listBackups(const Environment &environment)
{
    std::vector<Backup> backups;
    for (const QString &name : backupNames(environment)) {
        if (auto backup = readManifest(QDir(environment.backups()).filePath(name)))
            backups.push_back(*backup);
    }
    return backups;
}

std::optional<Backup> findBackup(const Environment &environment, const QString &name)
{
    if (name.contains(QLatin1Char('/')))
        return readManifest(name);
    const std::vector<Backup> backups = listBackups(environment);
    if (name.isEmpty())
        return backups.empty() ? std::nullopt : std::optional(backups.front());
    for (const Backup &backup : backups) {
        if (backup.name == name)
            return backup;
    }
    std::optional<Backup> match;
    for (const Backup &backup : backups) {
        if (!backup.name.startsWith(name))
            continue;
        if (match)
            return std::nullopt;
        match = backup;
    }
    return match;
}

std::vector<Change> restorePlan(const Environment &environment, const Backup &backup, QString *error)
{
    std::vector<Change> plan;
    for (const BackupEntry &entry : backup.entries) {
        std::optional<QByteArray> target;
        if (entry.existed) {
            target = readBytes(QDir(backup.folder).filePath(entry.stored));
            if (!target) {
                *error = QStringLiteral("The backup %1 is missing its copy of %2, so nothing was changed.").arg(backup.name, entry.path);
                return {};
            }
        }
        Change change{QStringLiteral("restore"), QStringLiteral("Put back the files from backup %1").arg(backup.name), entry.path, readBytes(entry.path), target,
                      entry.path.startsWith(environment.plugins())};
        if (change.changes())
            plan.push_back(change);
    }
    return plan;
}

int runListBackups(const Environment &environment, QTextStream &out)
{
    const std::vector<Backup> backups = listBackups(environment);
    if (backups.empty()) {
        out << "No setup backups yet. Setup makes one before it changes anything, in " << environment.backups() << ".\n";
        return 0;
    }
    out << "Setup backups in " << environment.backups() << ", newest first:\n";
    for (const Backup &backup : backups) {
        QString when = backup.created;
        when.replace(QLatin1Char('T'), QLatin1Char(' '));
        const qsizetype files = std::count_if(backup.entries.begin(), backup.entries.end(), [](const BackupEntry &entry) { return entry.existed; });
        out << "  " << backup.name << "  " << backup.action << "  " << when << "  " << files << (files == 1 ? " file" : " files") << " kept\n";
    }
    out << "\nPut one back with: omastrator setup --restore [NAME]\n";
    return 0;
}

int runRestore(const Environment &environment, const QString &name, bool yes, bool dryRun, QTextStream &in, QTextStream &out, QTextStream &err)
{
    const std::optional<Backup> backup = findBackup(environment, name);
    if (!backup) {
        if (listBackups(environment).empty())
            err << "There is no setup backup to restore from yet. Setup makes one before it changes anything.\n";
        else
            err << QStringLiteral("No single backup matches \"%1\". `omastrator setup --list-backups` lists them.\n").arg(name);
        return 1;
    }
    QString error;
    const std::vector<Change> plan = restorePlan(environment, *backup, &error);
    if (!error.isEmpty()) {
        err << error << '\n';
        return 1;
    }
    if (plan.empty()) {
        out << "Every file already matches the backup " << backup->name << ", so there is nothing to restore.\n";
        return 0;
    }
    out << "Backup " << backup->name << " (" << backup->action << ", " << QString(backup->created).replace(QLatin1Char('T'), QLatin1Char(' ')) << ") would put back:\n";
    for (const Change &change : plan) {
        if (change.summarize)
            out << (change.before ? (change.after ? "  update " : "  delete ") : "  new    ") << change.path << '\n';
        else
            out << unifiedDiff(change.path, change.before, change.after);
    }
    if (dryRun) {
        out << "\nDry run: nothing changed.\n";
        return 0;
    }
    if (!yes && !ask(in, out, QStringLiteral("Put these files back? Anything you changed in them since setup is lost."))) {
        out << "Skipped.\n";
        return 0;
    }
    // What is about to be replaced is copied first, so a restore can be undone with another one.
    const QString backupName = newBackupName(environment);
    QStringList paths;
    for (const Change &change : plan)
        paths << change.path;
    paths.removeDuplicates();
    if (const QString failure = writeBackup(environment, backupName, QStringLiteral("restore"), paths, nullptr); !failure.isEmpty()) {
        err << QStringLiteral("Nothing was changed: the restore couldn't back up the current files first. %1\n").arg(failure);
        return 1;
    }
    // The backup being restored from stays too, so it can be used again.
    pruneBackups(environment, {backupName, backup->name});
    out << "\nBacked up the current files to " << QDir(environment.backups()).filePath(backupName) << " first.\n";
    bool reload = false;
    for (const Change &change : plan) {
        if (change.after) {
            if (const QString failure = saveBytes(change.path, *change.after); !failure.isEmpty()) {
                err << failure << QStringLiteral("\nThe restore stopped part way; run it again once that is fixed.\n");
                return 1;
            }
        } else {
            QFile::remove(change.path);
        }
        reload = reload || change.summarize || change.path == environment.shellJson();
    }
    // Folders setup made go again once empty, deepest first; anything the user put there stays.
    for (const QString &folder : backup->createdDirectories)
        QDir().rmdir(folder);
    QStringList notes;
    if (reload)
        reloadOmarchyShell(&notes);
    for (const QString &note : notes)
        out << '\n' << note << '\n';
    out << "\nRestored " << plan.size() << (plan.size() == 1 ? " file" : " files") << " from " << backup->folder << ".\n";
    return 0;
}
}
