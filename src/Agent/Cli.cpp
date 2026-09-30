#include "Agent/Cli.h"
#include "Agent/AgentClient.h"
#include "Agent/BrowserHost.h"
#include "Agent/DesignCli.h"
#include "Agent/Island.h"
#include "Agent/Setup.h"
#include "Agent/StatusStream.h"
#include <QTextStream>
#include <cstring>

namespace Cli {
namespace {
// Chromium starts a native messaging host with the extension's origin as its first argument.
bool fromChromium(const char *argument)
{
    return std::strncmp(argument, "chrome-extension://", 19) == 0;
}

// argv[1] only: `omastrator --help` must not start the GUI or reach the agent socket.
bool isHelp(const char *argument)
{
    return std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0;
}

bool isVersion(const char *argument)
{
    return std::strcmp(argument, "--version") == 0 || std::strcmp(argument, "-V") == 0;
}

// Lists every command `handles` accepts; keep the two in step.
QString usage()
{
    return QStringLiteral(
        "Usage: omastrator [FILE...]\n"
        "       omastrator <command> [args]\n\n"
        "With no command, opens the files (or a new document) in Omastrator's window.\n\n"
        "Commands:\n"
        "  agent <method> [JSON]   Call an agent method; `agent --help` lists them.\n"
        "  --mcp                   Serve the agent methods as an MCP server on stdio.\n"
        "  status [--follow]       Print what Omastrator is doing, as JSON.\n"
        "  island <verb>           Desktop commands: mode, activity, new and more.\n"
        "  setup                   Check and set up Omarchy integration and helpers.\n"
        "  design <verb>           Design mode: inspect and restyle the desktop and apps.\n"
        "  desk [show|window|toggle|hide]\n"
        "                          Open the Desk canvas.\n"
        "  daemon [start|stop|status]\n"
        "                          Run Omastrator in the background (also: --daemon).\n"
        "  reset                   Turn design mode off and restore keys and workspaces.\n"
        "  browser-host            Native messaging host for the Chromium extension.\n\n"
        "Options:\n"
        "  -h, --help              Print this help and exit.\n"
        "  -V, --version           Print the version and exit.\n"
        "  --daemon [FILE...]      Start the app in the background, with no window.\n\n"
        "Run `omastrator <command> --help` for a command's own help.\n");
}
}

bool handles(const char *command)
{
    if (fromChromium(command) || isHelp(command) || isVersion(command))
        return true;
    for (const char *each : {"agent", "--mcp", "status", "island", "setup", "design", "desk", "daemon", "reset", "browser-host"}) {
        if (std::strcmp(command, each) == 0)
            return true;
    }
    return false;
}

int run(const QStringList &args)
{
    const QString command = args.value(0);
    if (isHelp(command.toUtf8().constData())) {
        QTextStream(stdout) << usage();
        return 0;
    }
    if (isVersion(command.toUtf8().constData())) {
        QTextStream(stdout) << "omastrator " << OMASTRATOR_VERSION << "\n";
        return 0;
    }
    if (command == QLatin1String("--mcp"))
        return AgentClient::runMcp();
    if (command == QLatin1String("agent"))
        return AgentClient::runCli(args.mid(1));
    // Started by Chromium for Omastrator's extension: stdout carries its messages only.
    if (command == QLatin1String("browser-host") || fromChromium(command.toUtf8().constData()))
        return BrowserHost::run();
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (command == QLatin1String("status"))
        return StatusStream::runCli(args.mid(1), out, err);
    if (command == QLatin1String("island"))
        return Island::runCli(args.mid(1), out, err);
    if (command == QLatin1String("design"))
        return DesignCli::runDesign(args.mid(1), out, err);
    if (command == QLatin1String("desk"))
        return DesignCli::runDesk(args.mid(1), out, err);
    if (command == QLatin1String("reset"))
        return DesignCli::runReset(out, err);
    if (command == QLatin1String("daemon"))
        return DesignCli::runDaemon(args.mid(1), out, err);
    if (command == QLatin1String("setup")) {
        QTextStream in(stdin);
        return Setup::runCli(args.mid(1), in, out, err);
    }
    err << QStringLiteral("Unknown command “%1”.\n").arg(command);
    return 1;
}
}
