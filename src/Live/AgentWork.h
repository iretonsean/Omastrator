#pragma once
#include "Live/WriteBack.h"
#include <QJsonArray>
#include <QString>
#include <vector>

// Live's agent path (docs/OS-SUITE.md): the agent works in a git worktree
// on a branch of its own, never in the user's checkout. When it is done its
// changes are copied into the project as one write-back, recorded for review.
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
    // changed since the agent's commit are merged three-way; the user's uncommitted ones only when `confirmedDirty`.
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
    // Hand to Agent (docs/ANYWHERE.md): a mockup from any surface, for an app whose source is this project.
    struct Package {
        QString instruction;
        // What it came from: "example.com/pricing", "foot", "the document in front".
        QString source;
        QString url;
        // The mockup as a picture and as vectors; the SVG may be empty.
        QString png;
        QString svg;
        // The surface as it is now, and before a site's edits, when there's one.
        QString screenshot;
        QString original;
        // selectors.json: each lifted shape's source element and where it sits.
        QString selectors;
        // A site's edits as CSS, and the same as a before → after list.
        QString css;
        QString diff;
        QString command;
    };
    QString handoffPrompt(const Package &package) const;
};
