#pragma once
#include <QStringList>

// The commands that run without the GUI: agent, --mcp, status, island, setup, design, desk and daemon.
namespace Cli {
// True when `command` (argv[1]) is one of them.
bool handles(const char *command);
// `args` start after the program name. Needs a QCoreApplication; returns the exit code.
int run(const QStringList &args);
}
