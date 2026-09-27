#include "Agent/AgentServer.h"
#include "Agent/AgentProtocol.h"
#include "Agent/AgentTools.h"
#include "Logging.h"
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>

namespace {
// A request past this is not an SVG anyone meant to send.
constexpr qsizetype maximumLine = 64 * 1024 * 1024;
}

AgentServer::AgentServer(AgentTools &tools, QObject *parent)
    : QObject(parent), m_tools(tools), m_server(new QLocalServer(this))
{
    // Same user only: the socket file is 0600.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &AgentServer::accept);
}

AgentServer::~AgentServer()
{
    close();
}

QString AgentServer::listen()
{
    return listen(AgentProtocol::socketPath());
}

QString AgentServer::listen(const QString &path)
{
    close();
    if (QFileInfo::exists(path)) {
        // A socket that answers belongs to a running Omastrator; one that doesn't is left over.
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(200)) {
            probe.disconnectFromServer();
            return QStringLiteral("Another Omastrator is already listening at %1.").arg(path);
        }
        QLocalServer::removeServer(path);
    }
    if (!m_server->listen(path)) {
        const QString reason = m_server->errorString();
        qCWarning(lcApp).noquote() << "agent socket" << path << "failed:" << reason;
        return QStringLiteral("Could not listen at %1: %2").arg(path, reason);
    }
    m_path = m_server->fullServerName();
    qCInfo(lcApp).noquote() << "agent socket listening at" << m_path;
    return {};
}

void AgentServer::close()
{
    if (!m_server->isListening())
        return;
    m_server->close();
    for (QLocalSocket *socket : m_pending.keys())
        socket->disconnectFromServer();
    m_pending.clear();
    m_path.clear();
}

bool AgentServer::isListening() const
{
    return m_server->isListening();
}

void AgentServer::accept()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_pending.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_pending.remove(socket);
            socket->deleteLater();
        });
    }
}

void AgentServer::read(QLocalSocket *socket)
{
    // The tools may run an event loop, in which the client can go.
    const QPointer<QLocalSocket> alive(socket);
    if (!m_pending.contains(socket))
        return;
    m_pending[socket] += socket->readAll();
    while (alive && m_pending.contains(socket)) {
        QByteArray &buffer = m_pending[socket];
        const qsizetype end = buffer.indexOf('\n');
        if (end < 0) {
            if (buffer.size() > maximumLine) {
                socket->write(AgentProtocol::frame(AgentProtocol::error(QJsonValue::Null, AgentProtocol::invalidRequest,
                                                                        QStringLiteral("The request is larger than 64 MB."))));
                socket->disconnectFromServer();
            }
            return;
        }
        const QByteArray line = buffer.left(end).trimmed();
        buffer.remove(0, end + 1);
        if (line.isEmpty())
            continue;
        const QJsonObject reply = AgentProtocol::respond(line, [this](const QString &method, const QJsonObject &params) {
            return m_tools.call(method, params);
        });
        if (alive && !reply.isEmpty())
            socket->write(AgentProtocol::frame(reply));
    }
}
