#include "Agent/Cli.h"
#include "Agent/AgentClient.h"
#include "Agent/Island.h"
#include "Agent/StatusStream.h"
#include <QTextStream>
#include <cstring>

namespace Cli {
bool handles(const char *command)
{
    for (const char *each : {"agent", "--mcp", "status", "island"}) {
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
    err << QStringLiteral("Unknown command “%1”.\n").arg(command);
    return 1;
}
}
