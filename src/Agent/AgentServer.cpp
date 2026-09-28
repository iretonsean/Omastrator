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
    // Drags and zooms change the session many times a frame; followers hear the settled state.
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(30);
    connect(&m_statusTimer, &QTimer::timeout, this, &AgentServer::publishStatus);
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
    m_followers.clear();
    m_path.clear();
}

void AgentServer::statusMayHaveChanged()
{
    emit statusMayChange();
    if (!m_followers.isEmpty() && !m_statusTimer.isActive())
        m_statusTimer.start();
}

void AgentServer::publishStatus()
{
    if (m_followers.isEmpty())
        return;
    const QJsonObject status = m_tools.status();
    const QByteArray line = AgentProtocol::frame({{"jsonrpc", "2.0"}, {"method", "status"}, {"params", status}});
    for (auto it = m_followers.begin(); it != m_followers.end(); ++it) {
        if (it.value() == status)
            continue;
        it.value() = status;
        it.key()->write(line);
    }
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
            m_followers.remove(socket);
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
        const QJsonObject reply = AgentProtocol::respond(line, [this, socket](const QString &method, const QJsonObject &params) {
            // `omastrator status --follow`: this answer, then a notification per change.
            if (method == QLatin1String("status_follow")) {
                const QJsonObject status = m_tools.status();
                m_followers.insert(socket, status);
                return status;
            }
            return m_tools.call(method, params);
        });
        if (alive && !reply.isEmpty())
            socket->write(AgentProtocol::frame(reply));
        statusMayHaveChanged();
    }
}
