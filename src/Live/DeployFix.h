#pragma once
#include <QString>

// Fix with <agent> (docs/LIVE-IN-FRAME.md, Deploy): what a failed deploy's output says, in one line, and the task that
// hands the failure to the default agent. Output is untrusted: the prompt fences it as data.
namespace DeployFix {
// The reason a command failed, from what it printed: a JSON object's "message" (with its "reason"), else the last line that
// isn't blank, a box, a banner ("Update available", "Changelog", `npm i -g`) or a hint; an "error" line wins over a later
// plain one. Cut to 160 characters; empty when nothing is left.
QString reason(const QString &output);

// What a deploy log (Deploy::newLogPath) records about the failure.
struct Failure {
    bool failed = false;
    // A production deploy's log, not a save's or a preview's.
    bool deploy = false;
    // The last "$ command" line, with no "$ ".
    QString command;
    // From "(exit code N)", or -1.
    int exitCode = -1;
    // The "Failed:" line, with no "Failed: ".
    QString line;
    // The end of the log, at most `tailLines` lines and `tailBytes` characters.
    QString tail;
};
Failure read(const QString &logText);
// The same from a log file: its first line and its last 256 KB. Empty (not failed) when it can't be read.
Failure readFile(const QString &path);

struct Brief {
    QString requestId;
    QString folder;
    // The commit that was being deployed; empty outside git.
    QString commit;
    // What the project deploys with: a shell command, or empty when the agent deploys.
    QString deployCommand;
    // The command to report back with: `omastrator`, quoted.
    QString binary;
    Failure failure;
};
// The task for the agent, run in the project's folder. The tail is redacted with the project's .env values first, and the
// prompt names no .env file and no key.
QString prompt(const Brief &brief);
}
