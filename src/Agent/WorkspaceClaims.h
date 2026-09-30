#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// The named Hyprland workspaces Omastrator's pages hold (docs/WORKSPACES.md), kept in `workspaces.json` in the
// runtime directory so `omastrator reset` and the next start can give them back without the app.
namespace WorkspaceClaims {
struct Claim {
    // `design:<document> · <page>`
    QString name;
    QString tabId;
    QString pageId;
    // The windows Omastrator put there (the editor and stand-ins); anything else on the workspace is the user's.
    QStringList windows;
    // What the name came from, for the bar's page dots (docs/WORKSPACES.md, "In the bar"). Files from before have neither.
    QString document;
    QString pageName;
    bool operator==(const Claim &) const = default;
};

struct State {
    // The Hyprland instance and app that made the claims.
    QString signature;
    qint64 pid = 0;
    // The workspace that was focused when the first claim was made: where the user's windows go back to. Its
    // id says how to select it (a numbered one by number: by name it may be gone, and a bare name would make a
    // new one); files from before had only the name, and 0 then.
    int returnId = 0;
    QString returnWorkspace;
    QList<Claim> claims;
    bool isEmpty() const { return claims.isEmpty(); }
    bool operator==(const State &) const = default;
};

// $OMASTRATOR_RUNTIME_DIR/workspaces.json (Island::runtimeDirectory).
QString path();
// An empty state when there's no file or it can't be read.
State read();
// Rewrites the file; an empty state empties it. Returns why it failed, or empty.
QString write(const State &state);

struct GiveBack {
    // The user's windows moved off the claimed workspaces.
    int moved = 0;
    // Why Hyprland couldn't be asked, or empty.
    QString error;
    // The workspace they went to.
    QString to;
};
// Gives back every claim in `state`: moves windows Omastrator didn't put there to the return workspace, and if
// the user is standing on a claimed workspace, goes to the return one. Does not close our own windows or touch the file.
GiveBack giveBack(const State &state);
// `giveBack` of the file's claims, then the file emptied: the reset escape hatch, with no app needed.
GiveBack giveBackFromFile();
// At app start: a file left by an app that's gone (crashed) gives back what it can; one from another Hyprland
// session is only emptied, since its windows belong to a compositor that's gone. A live app's file stays.
// Returns true when it removed claims.
bool cleanUp(qint64 ownPid = 0);
}
