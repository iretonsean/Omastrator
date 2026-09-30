#pragma once
#include <QString>
#include <QStringList>

// The agent's output contract for motion (docs/MOTION.md, section 4), checked after it says it is done: the motion is in a marked
// block, every token the block uses is declared, each @keyframes name is used once in the project, and a reduced-motion rule is
// there unless the designer turned it off. A result that breaks it is still shown; it only says why the inspector can't tune it.
namespace MotionContract {
struct Report {
    // The blocks the agent wrote or changed, by name.
    QStringList blocks;
    // What breaks the contract, one line each.
    QStringList problems;
    bool ok() const { return problems.isEmpty(); }
    // The line the inspector shows when it doesn't hold; empty when it does.
    QString notice() const;
};

// `project` is the project as it is, `worktree` the agent's copy, `changed` the files it changed (relative to both).
Report check(const QString &project, const QString &worktree, const QStringList &changed, bool reducedMotion);
}
