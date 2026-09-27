#pragma once
#include "Live/Deploy.h"
#include "Live/WriteBack.h"
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// Live's version history (docs/OS-SUITE.md): the project's git log, what was
// deployed from it, and GitHub through the `gh` the user is logged into.
namespace History {
struct Entry {
    QString sha;
    QString subject;
    QString author;
    QDateTime time;
    QStringList files;
    // The commit on GitHub, when the project's remote is there.
    QString link;
    std::optional<Deploy::Record> deploy;
};
std::vector<Entry> list(const QString &folder, int limit = 50);
QStringList remotes(const QString &folder);
// "https://github.com/owner/repo" for a GitHub remote URL, else empty.
QString githubUrl(const QString &remoteUrl);
// The project's GitHub page, from its push remote; empty when it isn't on GitHub.
QString githubPage(const QString &folder);
// The changes that bring back `sha`'s version of every file that differs from HEAD, merged around the
// user's uncommitted edits. Empty with `error` set when they clash.
std::vector<WriteBack::FileChange> restore(const QString &folder, const QString &sha, QString *error);

struct PushTarget {
    QString remote;
    QString branch;
    // No upstream yet: the push sets it.
    bool setUpstream = false;
    QStringList arguments() const;
};
// Where saves push: the branch's upstream, else origin (or the only remote). nullopt with `why` when there's none.
std::optional<PushTarget> pushTarget(const QString &folder, QString *why);
}

namespace GitHub {
// `gh`, or $OMASTRATOR_GH for tests.
QString program();
struct Auth {
    bool installed = false;
    bool loggedIn = false;
    QString account;
};
// `gh auth status`: whether the user is logged in, and as whom. Runs gh, so it can take a moment.
Auth status();
// Opens a terminal running `gh auth login`; the token stays with gh. $OMASTRATOR_TERMINAL replaces xdg-terminal-exec.
QString connect();
// A repository name for the folder: its name, made safe.
QString suggestedName(const QString &folder);
// `gh repo create <name> --private --source . --push`.
QStringList createArguments(const QString &name);
}
