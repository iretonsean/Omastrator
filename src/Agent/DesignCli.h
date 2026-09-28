#pragma once
#include <QStringList>
#include <QTextStream>

// `omastrator design …`, `omastrator desk …` and `omastrator daemon …` (docs/ANYWHERE.md):
// thin clients of the background app's `design`, `show_window` and `quit_app` methods.
namespace DesignCli {
int runDesign(const QStringList &args, QTextStream &out, QTextStream &err);
int runDesk(const QStringList &args, QTextStream &out, QTextStream &err);
int runDaemon(const QStringList &args, QTextStream &out, QTextStream &err);
// `omastrator reset`: the escape hatch, which works even when the app isn't answering.
int runReset(QTextStream &out, QTextStream &err);
QString designHelp();
}
