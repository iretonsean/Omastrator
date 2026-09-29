#pragma once
#include <QString>

// The Browser View pool's Chromium (docs/BROWSER-VIEW.md), noted in `browser-view.json` in the runtime directory so
// `omastrator reset` can stop one left by a crash without the app.
namespace BrowserPoolState {
struct State {
    qint64 pid = 0;
    QString profile;
    bool isEmpty() const { return pid <= 0; }
};

// $OMASTRATOR_RUNTIME_DIR/browser-view.json (Island::runtimeDirectory).
QString path();
// An empty state when there's no file or it can't be read.
State read();
// Returns why it failed, or empty.
QString write(const State &state);
void clear();

// Ends the browser `state` names, only if that pid's command line names the same profile (a pid that is running but
// isn't ours was reused). Returns whether one was ended; the file is cleared either way.
bool endLeftover();
// Whether a running pid's command line opens `profile` (`--user-data-dir=<profile>`).
bool namesProfile(qint64 pid, const QString &profile);
}
