#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

// The island's own state: its mode, whether it is expanded, and the activity
// line it shows briefly. It lives in a file so the island works with the app
// closed; `omastrator island …` writes it and `omastrator status --follow`
// reads it. See docs/OS-SUITE.md.
namespace Island {
// Normal, Draw, Capture, AI and Live, in the order the arrows step through.
const QStringList &modes();

// $OMASTRATOR_RUNTIME_DIR, else $XDG_RUNTIME_DIR/omastrator, else /tmp/omastrator-<uid>.
QString runtimeDirectory();
// island.json in the runtime directory: gone at logout, so each session starts in Normal.
QString statePath();
// The modes whose label has shown once, kept across sessions under $XDG_STATE_HOME.
QString seenPath();

struct State {
    QString mode = QStringLiteral("normal");
    bool expanded = false;
    QString activity;
    // When the activity was set, in ms since the epoch: the island shows each one once.
    qint64 activityId = 0;
    int activitySeconds = 3;
    QStringList seen;

    QJsonObject toJson() const;
    static State fromJson(const QJsonObject &json);
};
State read();
// Returns why it failed, or empty.
QString write(const State &state);
QString setActivity(const QString &text, int seconds = 3);

// Starts the Omastrator app unless it is already listening, and waits for its socket.
// $OMASTRATOR_APP replaces the app's command, for tests. Returns why it failed, or empty.
QString ensureAppRunning(int timeoutMs = 15'000);
bool appIsRunning();

// `omastrator island <verb> …`. Returns the exit code.
int runCli(const QStringList &args, QTextStream &out, QTextStream &err);
QString helpText();
}
