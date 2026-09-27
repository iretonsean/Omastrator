#pragma once
#include "Live/LiveSession.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// Writing Live edits back to the project (docs/OS-SUITE.md). The
// deterministic path changes a file only when the change is certain; every
// write keeps the files' old bytes so Discard restores them exactly.
namespace WriteBack {
struct FileChange {
    QString path;
    // nullopt: the file didn't exist.
    std::optional<QByteArray> before;
    // nullopt: the file is deleted.
    std::optional<QByteArray> after;
};

// One write-back, kept as a record: the change is written at once, and
// Review changes shows it later, with Discard.
struct Review {
    QString id;
    // "Live edits" or "Ask AI: …".
    QString title;
    QString summary;
    std::vector<FileChange> changes;
    // The project the files belong to.
    QString folder;
    // The commit it went into; empty until saved.
    QString commit;
    QDateTime time = QDateTime::currentDateTime();
    QString diff(const QString &folder) const;
    QString diff() const { return diff(folder); }
};

struct Plan {
    std::vector<FileChange> changes;
    // The edits the deterministic path can't be sure of; they go to the agent.
    std::vector<LiveEdit> unresolved;
    // One line per edit written, for the review and the commit message.
    QStringList done;
};

// The files the project tracks: `git ls-files`, else every text file outside
// node_modules, dist, build and dot-folders. Paths are relative.
QStringList trackedFiles(const QString &folder);
// Of `paths` (relative), those with uncommitted changes.
QStringList dirtyFiles(const QString &folder, const QStringList &paths = {});
bool isGitRepository(const QString &folder);

// What the deterministic path would write for `edits`, without writing.
Plan plan(const QString &folder, const std::vector<LiveEdit> &edits);
// Writes each change's `after`; `restore` writes each `before` back. Return why they failed, or empty.
QString apply(const std::vector<FileChange> &changes);
QString restore(const std::vector<FileChange> &changes);

// A file Omastrator changed, and its bytes before it did (nullopt: it didn't exist).
struct Own {
    QString path;
    std::optional<QByteArray> base;
};
// Commits Omastrator's own change to each file: HEAD plus the difference between `base` and what's on disk,
// through a separate index. The user's other uncommitted edits stay uncommitted and what they staged stays
// staged; if their edits overlap Omastrator's, nothing is committed. `sha` gets the commit, or empty when there
// was nothing to commit. Returns why it failed, or empty.
QString commit(const QString &folder, const std::vector<Own> &files, const QString &message, QString *sha = nullptr);
// Three-way merge (git merge-file): the change from `base` to `theirs`, applied to `ours`. nullopt if they clash.
std::optional<QByteArray> merge(const QByteArray &base, const QByteArray &ours, const QByteArray &theirs);
// The file's bytes in HEAD (or another commit), or nullopt when it has none.
std::optional<QByteArray> committed(const QString &folder, const QString &relative, const QString &commit = QStringLiteral("HEAD"));
std::optional<QByteArray> readFile(const QString &path);
// The changes that undo `changes` from the files as they are now, merged around anything changed since.
// Empty with `error` set if they can't be undone cleanly.
std::vector<FileChange> reverse(const std::vector<FileChange> &changes, QString *error);
// A commit message for the lines of reviews kept since the last save.
QString commitMessage(const QStringList &done);

// Runs git in `folder`; returns stdout, or sets `error`.
QString git(const QString &folder, const QStringList &arguments, QString *error = nullptr, int timeoutMs = 30'000);
}
