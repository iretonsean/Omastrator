#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include "Logging.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
// Linux caps one argument at 128 KiB; longer tasks travel as a file.
constexpr qsizetype maximumPromptArgument = 96 * 1024;
const QString chooseAgent = QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent.");

QString omarchy()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_OMARCHY");
    return overridden.isEmpty() ? QStringLiteral("omarchy") : overridden;
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}
}

namespace AgentLauncher {
QString defaultAgent(QString *error)
{
    auto failed = [&](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };
    QProcess process;
    process.start(omarchy(), {QStringLiteral("default"), QStringLiteral("agent")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000))
        return failed(QStringLiteral("Omarchy's agent launcher is unavailable. To connect an agent yourself, run: "
                                     "claude mcp add omastrator -- omastrator --mcp"));
    const QString agent = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || agent.isEmpty())
        return failed(chooseAgent);
    if (QStandardPaths::findExecutable(agent).isEmpty())
        return failed(QStringLiteral("%1 is selected but is not installed. %2").arg(agent, chooseAgent));
    return agent;
}

QString folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("omastrator/agent"));
}

QString writeInstructions(const QString &directory, const QString &binary, const QString &socket)
{
    if (!QDir().mkpath(directory))
        return QStringLiteral("Could not create %1.").arg(directory);
    const QByteArray text = instructions(binary).toUtf8();
    const QJsonObject server{{"type", "stdio"}, {"command", binary}, {"args", QJsonArray{"--mcp"}},
                             {"env", QJsonObject{{"OMASTRATOR_SOCKET", socket}}}};
    const QByteArray mcp = QJsonDocument(QJsonObject{{"mcpServers", QJsonObject{{"omastrator", server}}}}).toJson(QJsonDocument::Indented);
    for (const auto &[name, bytes] : {std::pair{QStringLiteral("AGENTS.md"), text}, std::pair{QStringLiteral("CLAUDE.md"), text},
                                      std::pair{QStringLiteral(".mcp.json"), mcp}}) {
        const QString failure = writeFile(QDir(directory).filePath(name), bytes);
        if (!failure.isEmpty())
            return failure;
    }
    return {};
}

QString launch(const QString &taskPrompt, const QString &listening)
{
    if (taskPrompt.trimmed().isEmpty())
        return QStringLiteral("There is nothing to ask the agent.");
    QString error;
    const QString agent = defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString directory = folder();
    const QString binary = QCoreApplication::applicationFilePath();
    // Where this app listens, which a test or second instance may have moved.
    const QString socket = listening.isEmpty() ? AgentProtocol::socketPath() : listening;
    if (const QString failure = writeInstructions(directory, binary, socket); !failure.isEmpty())
        return failure;
    QString prompt = taskPrompt;
    if (prompt.toUtf8().size() > maximumPromptArgument) {
        if (const QString failure = writeFile(QDir(directory).filePath(QStringLiteral("TASK.md")), prompt.toUtf8()); !failure.isEmpty())
            return failure;
        prompt = QStringLiteral("Your Omastrator task is in TASK.md in the working directory. Read it, and AGENTS.md, then do it.");
    }

    // Arguments go straight to the process: the prompt never passes through a shell.
    auto *process = new QProcess;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OMASTRATOR_BIN"), binary);
    environment.insert(QStringLiteral("OMASTRATOR_SOCKET"), socket);
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(directory);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->start(omarchy(), {QStringLiteral("agent"), QStringLiteral("prompt"), prompt});
    if (!process->waitForStarted(3000)) {
        const QString reason = process->errorString();
        delete process;
        return QStringLiteral("Could not launch %1: %2").arg(agent, reason);
    }
    // An immediate failure is reported; a terminal that stays open is left to run.
    if (process->waitForFinished(200)) {
        const bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
        const QString reason = QString::fromUtf8(process->readAllStandardError()).trimmed();
        delete process;
        return ok ? QString() : QStringLiteral("Could not launch %1: %2").arg(agent, reason.isEmpty() ? QStringLiteral("it exited at once.") : reason);
    }
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    qCInfo(lcApp).noquote() << "launched" << agent << "in" << directory;
    return {};
}
}
