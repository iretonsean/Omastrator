#include "Live/History.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace History {
std::vector<Entry> list(const QString &folder, int limit)
{
    std::vector<Entry> entries;
    if (!WriteBack::isGitRepository(folder))
        return entries;
    QString error;
    const QString log = WriteBack::git(folder, {QStringLiteral("log"), QStringLiteral("-n"), QString::number(limit), QStringLiteral("--no-renames"),
                                                QStringLiteral("--format=%x1e%H%x1f%an%x1f%at%x1f%s"), QStringLiteral("--name-only")},
                                       &error);
    const QString page = githubPage(folder);
    const std::vector<Deploy::Record> deploys = Deploy::records(folder);
    for (const QString &record : log.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList lines = record.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QStringList fields = lines.value(0).split(QChar(0x1f));
        if (fields.size() < 4)
            continue;
        Entry entry{fields[0], fields[3], fields[1], QDateTime::fromSecsSinceEpoch(fields[2].toLongLong()), lines.mid(1), {}, {}};
        if (!page.isEmpty())
            entry.link = QStringLiteral("%1/commit/%2").arg(page, entry.sha);
        for (auto it = deploys.rbegin(); it != deploys.rend(); ++it) {
            if (it->ok && it->commit == entry.sha) {
                entry.deploy = *it;
                break;
            }
        }
        entries.push_back(entry);
    }
    return entries;
}

QStringList remotes(const QString &folder)
{
    QString error;
    return WriteBack::git(folder, {QStringLiteral("remote")}, &error).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString githubUrl(const QString &remoteUrl)
{
    static const QRegularExpression github(QStringLiteral("^(?:https://(?:[^@/]+@)?github\\.com/|git@github\\.com:|ssh://git@github\\.com/)([^/]+)/([^/]+?)(?:\\.git)?/?$"));
    const auto match = github.match(remoteUrl.trimmed());
    return match.hasMatch() ? QStringLiteral("https://github.com/%1/%2").arg(match.captured(1), match.captured(2)) : QString();
}

QString githubPage(const QString &folder)
{
    QString why;
    const auto target = pushTarget(folder, &why);
    if (!target)
        return {};
    QString error;
    return githubUrl(WriteBack::git(folder, {QStringLiteral("remote"), QStringLiteral("get-url"), target->remote}, &error));
}

std::vector<WriteBack::FileChange> restore(const QString &folder, const QString &sha, QString *error)
{
    std::vector<WriteBack::FileChange> changes;
    WriteBack::git(folder, {QStringLiteral("cat-file"), QStringLiteral("-e"), sha + QStringLiteral("^{commit}")}, error);
    if (!error->isEmpty()) {
        *error = QStringLiteral("There's no commit %1 in this project.").arg(sha.left(12));
        return {};
    }
    const QStringList paths = WriteBack::git(folder, {QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("--no-renames"), QStringLiteral("HEAD"), sha}, error)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (!error->isEmpty())
        return {};
    for (const QString &relative : paths) {
        const QString path = QDir(folder).filePath(relative);
        const std::optional<QByteArray> head = WriteBack::committed(folder, relative);
        const std::optional<QByteArray> target = WriteBack::committed(folder, relative, sha);
        const std::optional<QByteArray> now = WriteBack::readFile(path);
        std::optional<QByteArray> after = target;
        if (now != head) {
            // The user's uncommitted edits stay, around the old version.
            if (!now || !head || !target) {
                *error = QStringLiteral("%1 has uncommitted changes of yours. Commit or stash them, then restore.").arg(relative);
                return {};
            }
            after = WriteBack::merge(*head, *now, *target);
            if (!after) {
                *error = QStringLiteral("%1 has uncommitted changes of yours that clash with that version. Commit or stash them, then restore.").arg(relative);
                return {};
            }
        }
        if (after != now)
            changes.push_back({path, now, after});
    }
    error->clear();
    return changes;
}

QStringList PushTarget::arguments() const
{
    QStringList arguments{QStringLiteral("push")};
    if (setUpstream)
        arguments << QStringLiteral("-u");
    // Never forced.
    return arguments << remote << QStringLiteral("HEAD:refs/heads/") + branch;
}

std::optional<PushTarget> pushTarget(const QString &folder, QString *why)
{
    if (!WriteBack::isGitRepository(folder)) {
        *why = QStringLiteral("The project isn't in git.");
        return std::nullopt;
    }
    QString error;
    const QString upstream = WriteBack::git(folder, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("--symbolic-full-name"),
                                                     QStringLiteral("@{u}")}, &error).trimmed();
    if (error.isEmpty() && upstream.contains(QLatin1Char('/')))
        return PushTarget{upstream.section(QLatin1Char('/'), 0, 0), upstream.section(QLatin1Char('/'), 1), false};
    const QString branch = WriteBack::git(folder, {QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD")}, &error).trimmed();
    if (branch.isEmpty()) {
        *why = QStringLiteral("The project isn't on a branch, so there's nowhere to push.");
        return std::nullopt;
    }
    const QStringList names = remotes(folder);
    if (names.isEmpty()) {
        *why = QStringLiteral("The project has no remote to push to.");
        return std::nullopt;
    }
    return PushTarget{names.contains(QStringLiteral("origin")) ? QStringLiteral("origin") : names.front(), branch, true};
}
}

namespace GitHub {
QString program()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_GH");
    return overridden.isEmpty() ? QStringLiteral("gh") : overridden;
}

Auth status()
{
    Auth auth;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program(), {QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--hostname"), QStringLiteral("github.com")});
    if (!process.waitForStarted(3000))
        return auth;
    auth.installed = true;
    if (!process.waitForFinished(15'000)) {
        process.kill();
        return auth;
    }
    auth.loggedIn = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    static const QRegularExpression account(QStringLiteral("Logged in to \\S+ (?:account|as) ([A-Za-z0-9-]+)"));
    auth.account = account.match(QString::fromUtf8(process.readAll())).captured(1);
    return auth;
}

QString connect()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_TERMINAL");
    const QString terminal = overridden.isEmpty() ? QStringLiteral("xdg-terminal-exec") : overridden;
    if (overridden.isEmpty() && QStandardPaths::findExecutable(terminal).isEmpty())
        return QStringLiteral("There's no terminal launcher (xdg-terminal-exec). Run `gh auth login` in a terminal.");
    if (!QProcess::startDetached(terminal, {program(), QStringLiteral("auth"), QStringLiteral("login")}))
        return QStringLiteral("Couldn't open a terminal. Run `gh auth login` in one.");
    return {};
}

QString suggestedName(const QString &folder)
{
    QString name = QFileInfo(folder).fileName();
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    name.replace(unsafe, QStringLiteral("-"));
    return name.isEmpty() ? QStringLiteral("site") : name;
}

QStringList createArguments(const QString &name)
{
    return {QStringLiteral("repo"), QStringLiteral("create"), name, QStringLiteral("--private"), QStringLiteral("--source"), QStringLiteral("."),
            QStringLiteral("--push")};
}
}
