#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

// Every push or pull of a design system is a plan first (docs/DESIGN-SYSTEMS.md,
// the author's confirmation rule): where it publishes, each file it will write
// with its full path, the git repository and branch it may commit to, and any
// command it runs. Nothing happens until the plan is confirmed.
struct FileWrite {
    QString path;
    // What the file holds now; nullopt when it doesn't exist yet.
    std::optional<QByteArray> before;
    QByteArray after;
    // A file copied as it is (a theme's wallpaper) instead of `after`.
    QString copyFrom;
    // "new", "changed", "unchanged" or "copied".
    QString state() const;
    // "+12 −3 lines", "new, 24 lines", "copied".
    QString summary() const;
    // A short unified-style diff of the changed lines, for the preview.
    QString diff(int maxLines = 40) const;
};

struct GitTarget {
    // The repository's top folder and the branch checked out there.
    QString repository;
    QString branch;
    bool commit = true;
    QString message;
};

struct SyncPlan {
    enum class Direction { push, pull };
    Direction direction = Direction::push;
    // "Push tokens to the project", for the dialog's title.
    QString title;
    // Where it publishes, in plain words with the full path.
    QString destination;
    // Files read to make the plan (pulls).
    QStringList reads;
    std::vector<FileWrite> writes;
    std::optional<GitTarget> git;
    // A program run after the files are written, as its full command line.
    QStringList command;
    // In-app changes after the files (a pull into the document): returns why it failed, or empty.
    std::function<QString()> apply;
    // What happens in the app, for the dialog ("Merges 12 tokens into Untitled-1 as one undo step").
    QString inApp;
    // Why the plan can't run at all, shown instead of a confirm button.
    QString problem;

    std::vector<const FileWrite *> changes() const;
    // "3 files: 2 changed, 1 new".
    QString diffSummary() const;
};

class SyncConfirmDialog;

// Proof that the person confirmed this plan: only the confirmation dialog makes one.
class Confirmation {
public:
    const SyncPlan *plan() const { return m_plan; }

private:
    explicit Confirmation(const SyncPlan &plan) : m_plan(&plan) {}
    const SyncPlan *m_plan;
    friend class SyncConfirmDialog;
};

namespace SyncRunner {
// Writes, commits, runs and applies a confirmed plan. Refuses a confirmation for another plan, and a file
// changed since the preview. Returns why it stopped, or empty.
QString execute(const SyncPlan &plan, const Confirmation &confirmation);
// The repository holding `folder` and its branch, if it's in one.
std::optional<GitTarget> gitFor(const QString &folder, const QString &message);
// $OMASTRATOR_GIT, else git.
QString git();
}
