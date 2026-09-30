#include "System/SyncPlan.h"
#include "System/ConfigBackup.h"
#include <QLocale>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <algorithm>

namespace {
QStringList linesOf(const QByteArray &bytes)
{
    QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast();
    return lines;
}

QString run(const QString &program, const QStringList &arguments, const QString &folder, QString *output = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.start(program, arguments);
    if (!process.waitForStarted(5000))
        return QStringLiteral("Couldn't start %1.").arg(program);
    process.waitForFinished(60'000);
    const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output)
        *output = text;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return QStringLiteral("%1 %2 failed: %3").arg(QFileInfo(program).fileName(), arguments.value(0), error.isEmpty() ? text : error);
    }
    return {};
}
}

QString FileWrite::state() const
{
    if (remove)
        return QFileInfo::exists(path) || QFileInfo(path).isSymLink() ? QStringLiteral("removed") : QStringLiteral("unchanged");
    if (!linkTo.isEmpty())
        return linkTo == linkBefore ? QStringLiteral("unchanged") : QStringLiteral("link");
    if (!copyFrom.isEmpty())
        return QStringLiteral("copied");
    if (!before)
        return QStringLiteral("new");
    return *before == after ? QStringLiteral("unchanged") : QStringLiteral("changed");
}

bool FileWrite::binary() const
{
    const auto hasNul = [](const QByteArray &bytes) { return bytes.left(8000).contains('\0'); };
    return hasNul(after) || (before && hasNul(*before));
}

QString FileWrite::summary() const
{
    const QString by = byCommand ? QStringLiteral(" (by the command)") : QString();
    if (remove)
        return QStringLiteral("deleted") + by;
    if (!linkTo.isEmpty())
        return (linkBefore.isEmpty() ? QStringLiteral("link to %1") : QStringLiteral("link to %1, was %2")).arg(linkTo, linkBefore) + by;
    if (!copyFrom.isEmpty())
        return QStringLiteral("copied from %1").arg(copyFrom) + by;
    if (binary()) {
        const QString size = QLocale::system().formattedDataSize(after.size());
        return (before ? QStringLiteral("replaced, %1") : QStringLiteral("new, %1")).arg(size) + by;
    }
    const QStringList now = linesOf(after);
    if (!before)
        return QStringLiteral("new, %1 lines").arg(now.size()) + by;
    const QStringList was = linesOf(*before);
    // Lines only on one side, as a multiset.
    QStringList removed = was;
    int added = 0;
    for (const QString &line : now) {
        if (const qsizetype at = removed.indexOf(line); at >= 0)
            removed.removeAt(at);
        else
            ++added;
    }
    if (added == 0 && removed.isEmpty())
        return QStringLiteral("no changes");
    return QStringLiteral("+%1 −%2 lines").arg(added).arg(removed.size()) + by;
}

QString FileWrite::diff(int maxLines) const
{
    if (!linkTo.isEmpty())
        return (linkBefore.isEmpty() ? QString() : QStringLiteral("− → ") + linkBefore + QLatin1Char('\n')) + QStringLiteral("+ → ") + linkTo;
    if (remove)
        return QStringLiteral("− (the whole file)");
    if (!copyFrom.isEmpty() || binary())
        return {};
    const QStringList now = linesOf(after);
    const QStringList was = before ? linesOf(*before) : QStringList();
    QStringList out;
    QStringList remaining = now;
    for (const QString &line : was) {
        if (const qsizetype at = remaining.indexOf(line); at >= 0)
            remaining.removeAt(at);
        else
            out.append(QStringLiteral("− ") + line.trimmed());
    }
    QStringList left = was;
    for (const QString &line : now) {
        if (const qsizetype at = left.indexOf(line); at >= 0)
            left.removeAt(at);
        else
            out.append(QStringLiteral("+ ") + line.trimmed());
    }
    if (out.size() > maxLines) {
        const qsizetype more = out.size() - maxLines;
        out = out.mid(0, maxLines);
        out.append(QStringLiteral("… %1 more").arg(more));
    }
    return out.join(QLatin1Char('\n'));
}

std::vector<const FileWrite *> SyncPlan::changes() const
{
    std::vector<const FileWrite *> result;
    for (const FileWrite &write : writes) {
        if (write.state() != QLatin1String("unchanged"))
            result.push_back(&write);
    }
    return result;
}

std::vector<QStringList> SyncPlan::allCommands() const
{
    std::vector<QStringList> all;
    if (!command.isEmpty())
        all.push_back(command);
    for (const QStringList &more : commands) {
        if (!more.isEmpty())
            all.push_back(more);
    }
    return all;
}

QString SyncPlan::diffSummary() const
{
    int changed = 0, added = 0, copied = 0, removed = 0;
    for (const FileWrite &write : writes) {
        const QString state = write.state();
        changed += state == QLatin1String("changed") || state == QLatin1String("link");
        added += state == QLatin1String("new");
        copied += state == QLatin1String("copied");
        removed += state == QLatin1String("removed");
    }
    const int total = changed + added + copied + removed;
    if (total == 0)
        return QStringLiteral("No files are written.");
    QStringList parts;
    if (changed)
        parts.append(QStringLiteral("%1 changed").arg(changed));
    if (added)
        parts.append(QStringLiteral("%1 new").arg(added));
    if (copied)
        parts.append(QStringLiteral("%1 copied").arg(copied));
    if (removed)
        parts.append(QStringLiteral("%1 deleted").arg(removed));
    return QStringLiteral("%1 %2: %3").arg(total).arg(total == 1 ? QStringLiteral("file") : QStringLiteral("files"), parts.join(QStringLiteral(", ")));
}

namespace SyncRunner {
QString git()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_GIT");
    return overridden.isEmpty() ? QStringLiteral("git") : overridden;
}

std::optional<GitTarget> gitFor(const QString &folder, const QString &message)
{
    if (!QFileInfo(folder).isDir())
        return std::nullopt;
    QString top, branch;
    if (!run(git(), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, folder, &top).isEmpty() || top.isEmpty())
        return std::nullopt;
    if (!run(git(), {QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")}, folder, &branch).isEmpty() || branch.isEmpty())
        branch = QStringLiteral("(detached HEAD)");
    return GitTarget{QFileInfo(top).absoluteFilePath(), branch, true, message};
}

QString execute(const SyncPlan &plan, const Confirmation &confirmation)
{
    if (confirmation.plan() != &plan)
        return QStringLiteral("This confirmation was for another plan.");
    if (!plan.problem.isEmpty())
        return plan.problem;
    // Nothing is written if any file changed since the preview.
    for (const FileWrite &write : plan.writes) {
        const QString stale = QStringLiteral("%1 changed since the preview. Nothing was written.").arg(write.path);
        if (!write.copyFrom.isEmpty() || write.remove)
            continue;
        if (!write.linkTo.isEmpty()) {
            if (QFileInfo(write.path).symLinkTarget() != write.linkBefore)
                return stale;
            continue;
        }
        QFile file(write.path);
        const bool exists = file.exists();
        if (exists != write.before.has_value())
            return stale;
        if (exists && (!file.open(QIODevice::ReadOnly) || file.readAll() != *write.before))
            return stale;
    }
    // The backup comes first, so a write that fails halfway can still be reverted.
    if (!plan.backupFolder.isEmpty()) {
        QStringList paths = plan.alsoBackUp;
        for (const FileWrite &write : plan.writes) {
            if (!paths.contains(write.path))
                paths.append(write.path);
        }
        if (const QString failed = ConfigBackup::save(plan.backupFolder, plan.title, paths, plan.revertCommands); !failed.isEmpty())
            return failed;
    }
    QStringList written;
    for (const FileWrite &write : plan.writes) {
        if (write.byCommand || write.state() == QLatin1String("unchanged"))
            continue;
        if (write.remove) {
            if (!QFile::remove(write.path))
                return QStringLiteral("Couldn't delete %1.").arg(write.path);
            written.append(write.path);
            continue;
        }
        QDir().mkpath(QFileInfo(write.path).absolutePath());
        if (!write.linkTo.isEmpty()) {
            QFile::remove(write.path);
            if (!QFile::link(write.linkTo, write.path))
                return QStringLiteral("Couldn't link %1 to %2.").arg(write.path, write.linkTo);
        } else if (!write.copyFrom.isEmpty()) {
            QFile::remove(write.path);
            if (!QFile::copy(write.copyFrom, write.path))
                return QStringLiteral("Couldn't copy %1 to %2.").arg(write.copyFrom, write.path);
        } else {
            // A link is replaced by the file, as a revert of a linked file restores its bytes.
            if (QFileInfo(write.path).isSymLink())
                QFile::remove(write.path);
            QSaveFile file(write.path);
            if (!file.open(QIODevice::WriteOnly) || file.write(write.after) != write.after.size() || !file.commit())
                return QStringLiteral("Couldn't write %1: %2").arg(write.path, file.errorString());
        }
        written.append(write.path);
    }
    if (plan.git && plan.git->create && !QFileInfo::exists(QDir(plan.git->repository).filePath(QStringLiteral(".git")))) {
        if (const QString failed = run(git(), {QStringLiteral("init"), QStringLiteral("-q")}, plan.git->repository); !failed.isEmpty())
            return failed;
        // Before the first commit, so it also works where `git init -b` doesn't.
        if (const QString failed = run(git(), {QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"), QStringLiteral("refs/heads/") + plan.git->branch},
                                       plan.git->repository);
            !failed.isEmpty())
            return failed;
    }
    if (plan.git && plan.git->commit && !written.isEmpty()) {
        QStringList relative;
        for (const QString &path : written)
            relative.append(QDir(plan.git->repository).relativeFilePath(path));
        if (const QString failed = run(git(), QStringList{QStringLiteral("add"), QStringLiteral("--")} + relative, plan.git->repository); !failed.isEmpty())
            return failed;
        if (const QString failed = run(git(), QStringList{QStringLiteral("commit"), QStringLiteral("-m"), plan.git->message, QStringLiteral("--")} + relative,
                                       plan.git->repository);
            !failed.isEmpty())
            return failed;
    }
    for (const QStringList &command : plan.allCommands()) {
        if (const QString failed = run(command.front(), command.mid(1), QDir::homePath()); !failed.isEmpty())
            return failed;
    }
    if (plan.apply)
        return plan.apply();
    return {};
}
}
