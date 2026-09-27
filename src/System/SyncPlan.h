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
    FileWrite() = default;
    FileWrite(QString path, std::optional<QByteArray> before, QByteArray after, QString copyFrom = {})
        : path(std::move(path)), before(std::move(before)), after(std::move(after)), copyFrom(std::move(copyFrom))
    {
    }

    QString path;
    // What the file holds now; nullopt when it doesn't exist yet.
    std::optional<QByteArray> before;
    QByteArray after;
    // A file copied as it is (a theme's wallpaper) instead of `after`.
    QString copyFrom;
    // A symbolic link: what it points at after, and before (empty when it isn't a link now).
    QString linkTo;
    QString linkBefore;
    // The file is deleted (a revert of a file that didn't exist).
    bool remove = false;
    // The plan's command writes it (`omarchy font set`): shown, checked and backed up, but not written here.
    bool byCommand = false;
    // "new", "changed", "unchanged", "copied", "link" or "removed".
    QString state() const;
    // Contents that aren't text (a wallpaper): summarised by size, never diffed.
    bool binary() const;
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
    // More programs run after `command`, in order.
    std::vector<QStringList> commands;
    // Before anything is written, every file in `writes` (and `alsoBackUp`) is saved here with a manifest,
    // so the change can be reverted byte for byte (System/ConfigBackup.h). Empty: no backup.
    QString backupFolder;
    QStringList alsoBackUp;
    // Run after a revert puts the files back (reloads that make the old files take effect).
    std::vector<QStringList> revertCommands;
    // What can and can't be seen before and after, in plain words, for the dialog.
    QString note;
    // The confirm button's words, when the usual ones don't fit ("Revert").
    QString confirmLabel;
    // In-app changes after the files (a pull into the document): returns why it failed, or empty.
    std::function<QString()> apply;
    // What happens in the app, for the dialog ("Merges 12 tokens into Untitled-1 as one undo step").
    QString inApp;
    // Why the plan can't run at all, shown instead of a confirm button.
    QString problem;

    std::vector<const FileWrite *> changes() const;
    // `command`, then `commands`.
    std::vector<QStringList> allCommands() const;
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
