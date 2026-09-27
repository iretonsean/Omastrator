#pragma once
#include "System/SyncPlan.h"
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// Backups of the desktop's real config (docs/ANYWHERE.md, phase 4): before a
// confirmed change writes anything, each file it touches is saved as it was,
// with a manifest, under $XDG_DATA_HOME/omastrator/backups/<id>. Revert puts
// every file back byte for byte (or removes one that didn't exist, or puts a
// link back), through the same confirmation as the change itself.
namespace ConfigBackup {
struct Entry {
    QString path;
    bool existed = false;
    // A symbolic link: where it pointed (the wallpaper's link).
    QString linkTarget;
    // The saved copy, inside the backup's folder.
    QString copy;
};

struct Backup {
    QString folder;
    QString id;
    QString title;
    QDateTime when;
    std::vector<Entry> entries;
    std::vector<QStringList> revertCommands;
    bool reverted = false;
};

// $XDG_DATA_HOME/omastrator/backups (normally ~/.local/share/omastrator/backups).
QString root();
// A folder for a new backup, named by the time and `title`; not made until something is saved in it.
QString newFolder(const QString &title);
// Saves each path as it is now (bytes, a link, or absent) with a manifest. Returns why it failed, or empty.
QString save(const QString &folder, const QString &title, const QStringList &paths, const std::vector<QStringList> &revertCommands);
std::optional<Backup> read(const QString &folder);
// Every backup, newest first.
std::vector<Backup> list();
// A plan that puts every file back as the backup has it, then runs its revert commands. It backs up what it
// replaces too, so a revert can be reverted.
SyncPlan revertPlan(const Backup &backup);
}
