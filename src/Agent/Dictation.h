#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

// Dictation (docs/OS-SUITE.md): push-to-talk into a WAV with pw-record,
// transcribed by Omarchy's voxtype with the design vocabulary as Whisper's
// initial prompt, normalised, then parsed. Tier 1 is a local grammar that
// runs at once; everything else becomes an Edit with Instruction proposal.
// Voxtype's own config and service are never changed.
namespace Dictation {
// Lowercase, punctuation gone, design terms spelled one way, number words as
// digits, and spoken hex ("hash F F six six zero zero") as "#ff6600".
QString normalize(const QString &heard);

struct Command {
    // 1: the local grammar. 2: goes to the agent. 0: nothing heard, or "cancel".
    int tier = 0;
    // What the island shows after "Heard:": "Pen tool", "Align left", "Ask the agent: …".
    QString description;
    // The desktop method and params that carry it out.
    QString method;
    QJsonObject params;
    // A stable name for the kind of command, for "runs at once after the first use".
    QString kind;
};
Command parse(const QString &normalized);
bool isCancel(const QString &normalized);

// $OMASTRATOR_VOXTYPE, else voxtype on PATH; empty when missing.
QString voxtype();
// The plain message when voxtype is missing, with the command that installs it.
QString missingVoxtype();
// Whisper's initial prompt from the vocabulary.
QString initialPrompt();
// `voxtype --initial-prompt … transcribe <wav>`; the transcript, or empty with `error`.
QString transcribe(const QString &wav, QString *error);

// `omastrator island dictate <start|stop|cancel|transcribe FILE|parse TEXT>`.
int runCli(const QStringList &args, QTextStream &out, QTextStream &err);
}
