#pragma once
#include "Live/LiveSession.h"
#include <QByteArray>
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

// One write-back awaiting Keep or Discard.
struct Review {
    QString id;
    // "Live edits" or "Ask AI: …".
    QString title;
    QString summary;
    std::vector<FileChange> changes;
    QString diff(const QString &folder) const;
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

// `git add` the files and commit them with `message`. Returns why it failed, or empty.
QString commit(const QString &folder, const QStringList &paths, const QString &message);
// A commit message for the lines of reviews kept since the last save.
QString commitMessage(const QStringList &done);

struct PublishOption {
    // "git", "vercel", "netlify", "cloudflare".
    QString id;
    QString label;
    QString program;
    QStringList arguments;
    // What it will do, said before it does: "Push main to origin (git push origin main)."
    QString description;
};
// What this repo can publish with: its git upstream, and the Vercel, Netlify or
// Cloudflare CLI where the project is set up for one and it is installed.
// Never a force push, never production by default.
std::vector<PublishOption> publishOptions(const QString &folder);
// Runs one option in the folder; returns its output, or why it failed in `error`.
QString publish(const QString &folder, const PublishOption &option, QString *error);

// Runs git in `folder`; returns stdout, or sets `error`.
QString git(const QString &folder, const QStringList &arguments, QString *error = nullptr, int timeoutMs = 30'000);
}
