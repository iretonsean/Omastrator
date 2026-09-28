#include "Live/BrowserLink.h"
#include "Agent/BrowserHost.h"
#include "Logging.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
// Screenshots come back as base64 in one line; nothing sensible is larger.
constexpr qsizetype maximumLine = 64 * 1024 * 1024;
}

BrowserLink::BrowserLink(QObject *parent) : QObject(parent), m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &BrowserLink::accept);
    m_cdp.setTransport([this](const QString &text) {
        QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
        message["type"] = QStringLiteral("cdp");
        write(message);
    });
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(60);
    connect(&m_statusTimer, &QTimer::timeout, this, &BrowserLink::publishStatus);
}

BrowserLink::~BrowserLink()
{
    close();
}

QString BrowserLink::socketPath()
{
    return BrowserHost::socketPath();
}

QString BrowserLink::listen(const QString &path)
{
    close();
    // The agent socket already proved this is the only Omastrator, so a file here is left over.
    if (QFileInfo::exists(path))
        QLocalServer::removeServer(path);
    if (!m_server->listen(path))
        return QStringLiteral("Could not listen at %1: %2").arg(path, m_server->errorString());
    qCInfo(lcApp).noquote() << "browser link listening at" << m_server->fullServerName();
    return {};
}

void BrowserLink::close()
{
    drop();
    if (m_server->isListening())
        m_server->close();
}

void BrowserLink::accept()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        // A new host is a restarted Chromium or extension: the old one is gone, whatever its socket says.
        drop();
        m_host = socket;
        connect(socket, &QLocalSocket::readyRead, this, &BrowserLink::read);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (socket == m_host)
                drop();
            socket->deleteLater();
        });
    }
}

void BrowserLink::drop()
{
    const bool was = isConnected();
    if (m_host) {
        QLocalSocket *host = m_host;
        m_host = nullptr;
        host->disconnect(this);
        host->disconnectFromServer();
        host->deleteLater();
    }
    m_buffer.clear();
    m_hello = false;
    m_pid = 0;
    m_lastStatus = {};
    m_cdp.setTransportOpen(false);
    if (was)
        emit connectedChanged();
}

void BrowserLink::read()
{
    if (!m_host)
        return;
    const QPointer<QLocalSocket> host = m_host;
    m_buffer += m_host->readAll();
    while (host && host == m_host) {
        const qsizetype end = m_buffer.indexOf('\n');
        if (end < 0) {
            if (m_buffer.size() > maximumLine)
                drop();
            return;
        }
        const QByteArray line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (!line.isEmpty())
            handle(QJsonDocument::fromJson(line).object());
    }
}

void BrowserLink::handle(const QJsonObject &message)
{
    const QString type = message["type"].toString();
    if (type == QLatin1String("hello")) {
        m_pid = message["pid"].toInteger();
        m_version = message["version"].toString();
        m_hello = true;
        m_cdp.setTransportOpen(true);
        emit connectedChanged();
        m_lastStatus = {};
        statusMayHaveChanged();
    } else if (type == QLatin1String("cdp")) {
        QJsonObject cdp = message;
        cdp.remove(QStringLiteral("type"));
        m_cdp.deliver(QString::fromUtf8(QJsonDocument(cdp).toJson(QJsonDocument::Compact)));
    } else if (type == QLatin1String("call")) {
        QJsonObject reply{{"type", "reply"}, {"id", message["id"]}};
        QString error;
        const QJsonObject result = m_caller ? m_caller(message["method"].toString(), message["params"].toObject(), &error)
                                            : QJsonObject();
        if (!m_caller)
            error = QStringLiteral("Omastrator isn't ready.");
        if (error.isEmpty())
            reply["result"] = result;
        else
            reply["error"] = error;
        write(reply);
        statusMayHaveChanged();
    }
}

void BrowserLink::write(const QJsonObject &message)
{
    if (!m_host)
        return;
    m_host->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void BrowserLink::statusMayHaveChanged()
{
    if (isConnected() && m_status && !m_statusTimer.isActive())
        m_statusTimer.start();
}

void BrowserLink::publishStatus()
{
    if (!isConnected() || !m_status)
        return;
    const QJsonObject status = m_status();
    if (status == m_lastStatus)
        return;
    m_lastStatus = status;
    write({{"type", "status"}, {"status", status}});
}
