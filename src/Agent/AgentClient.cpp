#include "Agent/AgentClient.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <iostream>
#include <string>

using AgentProtocol::Error;

namespace {
constexpr const char *mcpProtocolVersion = "2025-06-18";
// Renders past this stay a path rather than inline image content.
constexpr qint64 maximumInlineImage = 8 * 1024 * 1024;

QString notRunning(const QString &path)
{
    return QStringLiteral("Omastrator is not running (nothing is listening at %1). Open Omastrator, then try again.").arg(path);
}

QString mcpInstructions()
{
    return QStringLiteral(
        "Drives the Omastrator vector editor that is open on this desktop. Read with document_get, selection_get "
        "and render (look at the PNG it returns). Every edit goes into one proposal the user sees live; they press "
        "Enter to keep it or Esc to discard it, so you can't accept your own work. Call proposal_finish with a "
        "title and summary when you are done. Coordinates are points, y down, origin at the artboard's top left.");
}
}

namespace AgentClient {
Connection::Connection(QString path) : m_path(std::move(path)) {}

Connection::~Connection() = default;

void Connection::connect()
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        return;
    m_socket = std::make_unique<QLocalSocket>();
    m_buffer.clear();
    m_socket->connectToServer(m_path);
    if (!m_socket->waitForConnected(2000)) {
        m_socket.reset();
        throw Error(AgentProtocol::notRunning, notRunning(m_path));
    }
}

QJsonObject Connection::call(const QString &method, const QJsonObject &params, int timeoutMs)
{
    connect();
    const int id = m_nextID++;
    m_socket->write(AgentProtocol::frame(AgentProtocol::request(id, method, params)));
    m_socket->flush();
    for (;;) {
        qsizetype end;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            const QJsonObject reply = QJsonDocument::fromJson(line).object();
            if (reply["id"].toInt(-1) != id && !reply["id"].isNull())
                continue;
            if (reply.contains("error")) {
                const QJsonObject error = reply["error"].toObject();
                throw Error(error["code"].toInt(AgentProtocol::internalError), error["message"].toString());
            }
            return reply["result"].toObject();
        }
        if (!m_socket->waitForReadyRead(timeoutMs)) {
            const bool dropped = m_socket->state() != QLocalSocket::ConnectedState;
            m_socket.reset();
            throw Error(AgentProtocol::notRunning, dropped ? QStringLiteral("Omastrator closed the connection before answering.")
                                                           : QStringLiteral("Omastrator did not answer %1 in time.").arg(method));
        }
        m_buffer += m_socket->readAll();
    }
}

int runCli(const QStringList &args, QTextStream &out, QTextStream &err, std::istream &in)
{
    if (args.isEmpty()) {
        err << AgentProtocol::helpText();
        return 1;
    }
    if (args.front() == QLatin1String("--help") || args.front() == QLatin1String("-h") || args.front() == QLatin1String("help")) {
        out << AgentProtocol::helpText();
        return 0;
    }
    const AgentProtocol::Method *method = AgentProtocol::method(args.front());
    if (!method) {
        err << QStringLiteral("There is no method “%1”. Run `omastrator agent --help` for the list.\n").arg(args.front());
        return 1;
    }
    if (args.size() > 1 && (args[1] == QLatin1String("--help") || args[1] == QLatin1String("-h"))) {
        out << AgentProtocol::methodHelp(*method);
        return 0;
    }
    if (args.size() > 2) {
        err << QStringLiteral("Pass the params as one JSON argument, quoted: omastrator agent %1 '{…}'\n").arg(method->name);
        return 1;
    }
    QJsonObject params;
    if (args.size() == 2) {
        QByteArray text = args[1].toUtf8();
        // "-" reads the params from stdin, for SVG too long for an argument.
        if (args[1] == QLatin1String("-")) {
            const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            text = QByteArray::fromStdString(all);
        }
        QJsonParseError parse{};
        const QJsonDocument document = QJsonDocument::fromJson(text, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            err << QStringLiteral("The params must be a JSON object, such as '{\"ids\": [\"…\"]}'%1\n")
                       .arg(parse.error != QJsonParseError::NoError ? QStringLiteral(": ") + parse.errorString() + QLatin1Char('.') : QStringLiteral("."));
            return 1;
        }
        params = document.object();
    }
    try {
        Connection connection;
        const QJsonObject result = connection.call(method->name, params);
        out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
        return 0;
    } catch (const Error &failure) {
        err << failure.message() << '\n';
        return 1;
    }
}

int runCli(const QStringList &args)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    return runCli(args, out, err, std::cin);
}

QJsonObject answerMcp(const QByteArray &line, const AgentProtocol::Call &forward)
{
    return AgentProtocol::respond(line, [&](const QString &method, const QJsonObject &params) -> QJsonObject {
        if (method == QLatin1String("initialize")) {
            return {{"protocolVersion", mcpProtocolVersion},
                    {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                    {"serverInfo", QJsonObject{{"name", "omastrator"}, {"title", "Omastrator"}, {"version", OMASTRATOR_VERSION}}},
                    {"instructions", mcpInstructions()}};
        }
        if (method == QLatin1String("ping") || method.startsWith(QLatin1String("notifications/")))
            return {};
        if (method == QLatin1String("tools/list")) {
            QJsonArray tools;
            for (const AgentProtocol::Method &each : AgentProtocol::methods())
                tools.append(QJsonObject{{"name", each.name}, {"description", each.description}, {"inputSchema", each.inputSchema}});
            return {{"tools", tools}};
        }
        if (method == QLatin1String("tools/call")) {
            const QString name = params["name"].toString();
            if (!AgentProtocol::method(name))
                throw Error(AgentProtocol::invalidParams, QStringLiteral("There is no tool “%1”.").arg(name));
            if (!params["arguments"].isUndefined() && !params["arguments"].isNull() && !params["arguments"].isObject())
                throw Error(AgentProtocol::invalidParams, QStringLiteral("Tool arguments must be a JSON object."));
            try {
                const QJsonObject result = forward(name, params["arguments"].toObject());
                QJsonArray content{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented))}}};
                // A render comes back as the picture itself, so the agent sees it without a file read.
                if (name == QLatin1String("render")) {
                    QFile file(result["path"].toString());
                    if (file.size() <= maximumInlineImage && file.open(QIODevice::ReadOnly))
                        content.append(QJsonObject{{"type", "image"}, {"mimeType", "image/png"}, {"data", QString::fromLatin1(file.readAll().toBase64())}});
                }
                return {{"content", content}, {"isError", false}};
            } catch (const Error &failure) {
                return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", failure.message()}}}}, {"isError", true}};
            }
        }
        throw Error(AgentProtocol::methodNotFound, QStringLiteral("Omastrator's MCP server has no method “%1”.").arg(method));
    });
}

int serveMcp(std::istream &in, std::ostream &out)
{
    Connection connection;
    std::string line;
    while (std::getline(in, line)) {
        const QByteArray bytes = QByteArray::fromStdString(line).trimmed();
        if (bytes.isEmpty())
            continue;
        const QJsonObject reply = answerMcp(bytes, [&](const QString &method, const QJsonObject &params) {
            return connection.call(method, params);
        });
        if (!reply.isEmpty()) {
            out << AgentProtocol::frame(reply).toStdString();
            out.flush();
        }
    }
    return 0;
}

int runMcp()
{
    return serveMcp(std::cin, std::cout);
}
}
