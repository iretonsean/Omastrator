#include "System/SyncPlan.h"
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
    if (!copyFrom.isEmpty())
        return QStringLiteral("copied");
    if (!before)
        return QStringLiteral("new");
    return *before == after ? QStringLiteral("unchanged") : QStringLiteral("changed");
}

QString FileWrite::summary() const
{
    if (!copyFrom.isEmpty())
        return QStringLiteral("copied from %1").arg(copyFrom);
    const QStringList now = linesOf(after);
    if (!before)
        return QStringLiteral("new, %1 lines").arg(now.size());
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
    return QStringLiteral("+%1 −%2 lines").arg(added).arg(removed.size());
}

QString FileWrite::diff(int maxLines) const
{
    if (!copyFrom.isEmpty())
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

QString SyncPlan::diffSummary() const
{
    int changed = 0, added = 0, copied = 0;
    for (const FileWrite &write : writes) {
        const QString state = write.state();
        changed += state == QLatin1String("changed");
        added += state == QLatin1String("new");
        copied += state == QLatin1String("copied");
    }
    const int total = changed + added + copied;
    if (total == 0)
        return QStringLiteral("No files are written.");
    QStringList parts;
    if (changed)
        parts.append(QStringLiteral("%1 changed").arg(changed));
    if (added)
        parts.append(QStringLiteral("%1 new").arg(added));
    if (copied)
        parts.append(QStringLiteral("%1 copied").arg(copied));
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
        QFile file(write.path);
        const bool exists = file.exists();
        if (!write.copyFrom.isEmpty())
            continue;
        if (exists != write.before.has_value())
            return QStringLiteral("%1 changed since the preview. Nothing was written.").arg(write.path);
        if (exists && (!file.open(QIODevice::ReadOnly) || file.readAll() != *write.before))
            return QStringLiteral("%1 changed since the preview. Nothing was written.").arg(write.path);
    }
    QStringList written;
    for (const FileWrite &write : plan.writes) {
        if (write.state() == QLatin1String("unchanged"))
            continue;
        QDir().mkpath(QFileInfo(write.path).absolutePath());
        if (!write.copyFrom.isEmpty()) {
            QFile::remove(write.path);
            if (!QFile::copy(write.copyFrom, write.path))
                return QStringLiteral("Couldn't copy %1 to %2.").arg(write.copyFrom, write.path);
        } else {
            QSaveFile file(write.path);
            if (!file.open(QIODevice::WriteOnly) || file.write(write.after) != write.after.size() || !file.commit())
                return QStringLiteral("Couldn't write %1: %2").arg(write.path, file.errorString());
        }
        written.append(write.path);
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
    if (!plan.command.isEmpty()) {
        if (const QString failed = run(plan.command.front(), plan.command.mid(1), QDir::homePath()); !failed.isEmpty())
            return failed;
    }
    if (plan.apply)
        return plan.apply();
    return {};
}
}
