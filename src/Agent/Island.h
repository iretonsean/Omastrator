#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

// What `omastrator island …` keeps in a file so it works with the app closed: the
// mode (Normal or Design) and the last activity line. The desktop island pill is
// gone (docs/OS-SUITE.md); the name stays for the command. `omastrator status
// --follow` reads the file.
namespace Island {
// Normal, and Design (design mode everywhere, docs/ANYWHERE.md).
const QStringList &modes();

// $OMASTRATOR_RUNTIME_DIR, else $XDG_RUNTIME_DIR/omastrator, else /tmp/omastrator-<uid>.
QString runtimeDirectory();
// island.json in the runtime directory: gone at logout, so each session starts in Normal.
QString statePath();
// The folder under $XDG_STATE_HOME that keeps what outlives a session (dictation's seen kinds).
QString stateDirectory();

struct State {
    QString mode = QStringLiteral("normal");
    QString activity;
    // When the activity was set, in ms since the epoch: each one is announced once.
    qint64 activityId = 0;
    int activitySeconds = 3;

    QJsonObject toJson() const;
    static State fromJson(const QJsonObject &json);
};
State read();
// Returns why it failed, or empty.
QString write(const State &state);
// Remembers the line for the status stream and shows it as a desktop notification (its first line the summary, the rest
// the body, `seconds` how long it stays): the messages of commands started outside the app. $OMASTRATOR_NOTIFY replaces
// notify-send, for tests.
QString setActivity(const QString &text, int seconds = 3);

// Starts the Omastrator app unless it is already listening, and waits for its socket.
// $OMASTRATOR_APP replaces the app's command, for tests. Returns why it failed, or empty.
QString ensureAppRunning(int timeoutMs = 15'000);
bool appIsRunning();
// Hands the keyboard back to the apps: leaves any Omastrator submap in Hyprland ($OMASTRATOR_HYPRCTL in tests).
void resetKeys();
// Holds design mode's submap, where Escape leaves, however design mode was turned on; only when Hyprland defines it.
void holdDesignKeys();
// Holds `mode`'s submap when Hyprland defines it, else gives the keys back; normal always gives them back.
// Every mode change goes through this, so a mode left by a click never keeps its letters bound.
void holdKeysFor(const QString &mode);

// `omastrator island <verb> …`. Returns the exit code.
int runCli(const QStringList &args, QTextStream &out, QTextStream &err);
QString helpText();
}
