#include "Live/WriteBack.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

// Committing only Omastrator's own changes, and undoing them around the user's.
namespace {
// Runs git with `input` on stdin and GIT_INDEX_FILE set when `index` is given; binary-safe.
std::optional<QByteArray> run(const QString &folder, const QStringList &arguments, QString *error, const QByteArray &input = {},
                              const QString &index = {})
{
    QProcess process;
    process.setWorkingDirectory(folder);
    if (!index.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("GIT_INDEX_FILE"), index);
        process.setProcessEnvironment(environment);
    }
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForStarted(5000)) {
        *error = QStringLiteral("git isn't installed.");
        return std::nullopt;
    }
    if (!input.isNull())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(60'000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("git %1 failed.").arg(arguments.value(0));
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

// "100644 blob <sha>\t<path>" → {mode, sha}, from `git ls-tree` or `git ls-files -s`.
std::pair<QString, QString> entry(const QByteArray &line)
{
    const QList<QByteArray> fields = line.left(line.indexOf('\t')).split(' ');
    if (fields.size() < 3)
        return {};
    // ls-tree: mode type sha; ls-files -s: mode sha stage.
    return fields[1] == "blob" ? std::pair{QString::fromLatin1(fields[0]), QString::fromLatin1(fields[2])}
                               : std::pair{QString::fromLatin1(fields[0]), QString::fromLatin1(fields[1])};
}
}

namespace WriteBack {
std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

std::optional<QByteArray> committed(const QString &folder, const QString &relative, const QString &commit)
{
    QString error;
    return run(folder, {QStringLiteral("show"), QStringLiteral("%1:%2").arg(commit, relative)}, &error);
}

std::optional<QByteArray> merge(const QByteArray &base, const QByteArray &ours, const QByteArray &theirs)
{
    QTemporaryDir scratch;
    const QString baseFile = scratch.filePath(QStringLiteral("base")), oursFile = scratch.filePath(QStringLiteral("ours")),
                  theirsFile = scratch.filePath(QStringLiteral("theirs"));
    for (const auto &[file, bytes] : {std::pair{baseFile, base}, std::pair{oursFile, ours}, std::pair{theirsFile, theirs}}) {
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
            return std::nullopt;
    }
    QProcess process;
    process.start(QStringLiteral("git"), {QStringLiteral("merge-file"), QStringLiteral("-p"), oursFile, baseFile, theirsFile});
    process.waitForFinished(60'000);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return std::nullopt;
    return process.readAllStandardOutput();
}

QString commit(const QString &folder, const std::vector<Own> &files, const QString &message, QString *sha)
{
    if (sha)
        sha->clear();
    if (!isGitRepository(folder))
        return QStringLiteral("%1 isn't a git repository, so there's nothing to commit to. The files are saved.").arg(folder);
    QString error;
    const QString head = QString::fromLatin1(run(folder, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("HEAD")},
                                                 &error).value_or(QByteArray()))
                             .trimmed();
    // A separate index from HEAD: the user's own staging is never part of this commit.
    QTemporaryDir scratch;
    const QString index = scratch.filePath(QStringLiteral("index"));
    if (!head.isEmpty() && !run(folder, {QStringLiteral("read-tree"), head}, &error, {}, index))
        return error;
    struct Written {
        QString relative;
        QString mode;
        QString blob;
    };
    std::vector<Written> written;
    for (const Own &file : files) {
        const QString relative = QDir(folder).relativeFilePath(file.path);
        const std::optional<QByteArray> before = head.isEmpty() ? std::nullopt : committed(folder, relative, head);
        const std::optional<QByteArray> now = readFile(file.path);
        std::optional<QByteArray> content;
        if (file.base == before) {
            content = now;
        } else if (file.base && before && now) {
            // HEAD plus only what Omastrator changed; the user's edits around it stay on disk, uncommitted.
            content = merge(*file.base, *before, *now);
            if (!content)
                return QStringLiteral("%1 has uncommitted changes of yours that overlap Omastrator's. Commit or stash yours, then try again.")
                    .arg(relative);
        } else {
            return QStringLiteral("%1 has uncommitted changes of yours that Omastrator can't separate from its own. Commit or stash yours, then try again.")
                .arg(relative);
        }
        if (content == before)
            continue;
        const auto tree = head.isEmpty() ? std::nullopt : run(folder, {QStringLiteral("ls-tree"), head, QStringLiteral("--"), relative}, &error);
        QString mode = tree ? entry(*tree).first : QString();
        if (mode.isEmpty())
            mode = QFileInfo(file.path).isExecutable() ? QStringLiteral("100755") : QStringLiteral("100644");
        QString blob;
        if (content) {
            const auto hashed = run(folder, {QStringLiteral("hash-object"), QStringLiteral("-w"), QStringLiteral("--stdin")}, &error, *content);
            if (!hashed)
                return error;
            blob = QString::fromLatin1(*hashed).trimmed();
            if (!run(folder, {QStringLiteral("update-index"), QStringLiteral("--add"), QStringLiteral("--cacheinfo"), QStringLiteral("%1,%2,%3").arg(mode, blob, relative)},
                     &error, {}, index))
                return error;
        } else if (!run(folder, {QStringLiteral("update-index"), QStringLiteral("--force-remove"), QStringLiteral("--"), relative}, &error, {}, index)) {
            return error;
        }
        written.push_back({relative, mode, blob});
    }
    if (written.empty())
        return {};
    const auto tree = run(folder, {QStringLiteral("write-tree")}, &error, {}, index);
    if (!tree)
        return error;
    QStringList arguments{QStringLiteral("commit-tree"), QString::fromLatin1(*tree).trimmed()};
    if (!head.isEmpty())
        arguments << QStringLiteral("-p") << head;
    arguments << QStringLiteral("-F") << QStringLiteral("-");
    const auto made = run(folder, arguments, &error, message.toUtf8());
    if (!made)
        return error;
    const QString commitSha = QString::fromLatin1(*made).trimmed();
    // Which paths the user had staged differently from HEAD, before HEAD moves.
    QStringList staged;
    for (const Written &file : written) {
        const auto inIndex = run(folder, {QStringLiteral("ls-files"), QStringLiteral("-s"), QStringLiteral("--"), file.relative}, &error);
        const auto inHead = head.isEmpty() ? std::nullopt : run(folder, {QStringLiteral("ls-tree"), head, QStringLiteral("--"), file.relative}, &error);
        const QString indexBlob = inIndex ? entry(*inIndex).second : QString();
        const QString headBlob = inHead ? entry(*inHead).second : QString();
        if (indexBlob != headBlob)
            staged << file.relative;
    }
    QStringList update{QStringLiteral("update-ref"), QStringLiteral("-m"), QStringLiteral("commit (Omastrator): %1").arg(message.section(QLatin1Char('\n'), 0, 0)),
                       QStringLiteral("HEAD"), commitSha};
    if (!head.isEmpty())
        update << head;
    if (!run(folder, update, &error))
        return error;
    // The real index follows the commit, except where the user staged something of their own.
    for (const Written &file : written) {
        if (staged.contains(file.relative))
            continue;
        if (file.blob.isEmpty())
            run(folder, {QStringLiteral("update-index"), QStringLiteral("--force-remove"), QStringLiteral("--"), file.relative}, &error);
        else
            run(folder, {QStringLiteral("update-index"), QStringLiteral("--add"), QStringLiteral("--cacheinfo"), QStringLiteral("%1,%2,%3").arg(file.mode, file.blob, file.relative)},
                &error);
    }
    if (sha)
        *sha = commitSha;
    return {};
}

std::vector<FileChange> reverse(const std::vector<FileChange> &changes, QString *error)
{
    std::vector<FileChange> undo;
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
        const std::optional<QByteArray> now = readFile(it->path);
        if (now == it->after) {
            undo.push_back({it->path, now, it->before});
        } else if (now && it->after && it->before) {
            // Changed since: take out only this change.
            const auto merged = merge(*it->after, *now, *it->before);
            if (!merged) {
                *error = QStringLiteral("%1 changed since, and this change can't be taken out around the newer edits.").arg(QFileInfo(it->path).fileName());
                return {};
            }
            undo.push_back({it->path, now, merged});
        } else {
            *error = QStringLiteral("%1 was created or deleted since, so this change can't be taken out.").arg(QFileInfo(it->path).fileName());
            return {};
        }
    }
    error->clear();
    return undo;
}
}
