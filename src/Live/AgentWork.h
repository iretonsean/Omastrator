#pragma once
#include "Live/WriteBack.h"
#include <QJsonArray>
#include <QString>
#include <vector>

// Live's agent path (docs/OS-SUITE.md): the agent works in a git worktree
// on a branch of its own, never in the user's checkout. When it is done its
// changes are copied into the project as one write-back to review.
struct AgentWork {
    QString project;
    QString worktree;
    QString branch;
    QString requestId;
    // The user confirmed going ahead although the project had uncommitted changes.
    bool confirmedDirty = false;

    // Makes the worktree under $XDG_DATA_HOME/omastrator/worktrees. Returns why it couldn't, or empty.
    QString prepare();
    // The agent's changes as write-back changes to the project, not yet written. Files
    // the user changed since HEAD are merged three-way, and only when `confirmedDirty`.
    std::vector<WriteBack::FileChange> collect(QString *error);
    // Removes the worktree and its branch.
    void cleanup();

    struct Brief {
        QString instruction;
        std::vector<LiveEdit> edits;
        QJsonArray elements;
        QString screenshot;
        QString url;
        QString command;
    };
    QString prompt(const Brief &brief) const;
};
