#include "Agent/Cli.h"
#include "Agent/AgentClient.h"
#include "Agent/DesignCli.h"
#include "Agent/Island.h"
#include "Agent/Setup.h"
#include "Agent/StatusStream.h"
#include <QTextStream>
#include <cstring>

namespace Cli {
bool handles(const char *command)
{
    for (const char *each : {"agent", "--mcp", "status", "island", "setup", "design", "desk", "daemon", "reset"}) {
        if (std::strcmp(command, each) == 0)
            return true;
    }
    return false;
}

int run(const QStringList &args)
{
    const QString command = args.value(0);
    if (command == QLatin1String("--mcp"))
        return AgentClient::runMcp();
    if (command == QLatin1String("agent"))
        return AgentClient::runCli(args.mid(1));
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
