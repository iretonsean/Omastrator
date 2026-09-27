#pragma once
#include "Agent/AgentProtocol.h"
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <iosfwd>
#include <memory>

class QLocalSocket;

// The two ways in from outside the app: `omastrator agent <method> [json]`
// and `omastrator --mcp`. Neither needs a GUI.
namespace AgentClient {
// One blocking connection to the running app's socket.
class Connection {
public:
    explicit Connection(QString path = AgentProtocol::socketPath());
    ~Connection();
    // Sends one request and waits for its answer; throws AgentProtocol::Error.
    QJsonObject call(const QString &method, const QJsonObject &params, int timeoutMs = 120'000);

private:
    void connect();
    QString m_path;
    std::unique_ptr<QLocalSocket> m_socket;
    QByteArray m_buffer;
    int m_nextID = 1;
};

// `args` follow `agent`. Results go to `out`, failures to `err`; returns the exit code.
int runCli(const QStringList &args, QTextStream &out, QTextStream &err, std::istream &in);
int runCli(const QStringList &args);

// Answers one MCP message; tools/call goes through `forward`. Notifications answer with an empty object.
QJsonObject answerMcp(const QByteArray &line, const AgentProtocol::Call &forward);
// Serves MCP over newline-delimited JSON until `in` ends, forwarding to the app's socket.
int serveMcp(std::istream &in, std::ostream &out);
int runMcp();
}
