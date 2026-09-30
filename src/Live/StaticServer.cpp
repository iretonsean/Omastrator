#include "Live/StaticServer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>

StaticServer::StaticServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { answer(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

QString StaticServer::serve(const QString &folder)
{
    stop();
    m_folder = QDir(folder).canonicalPath();
    if (m_folder.isEmpty())
        return QStringLiteral("%1 doesn't exist.").arg(folder);
    if (!m_server.listen(QHostAddress::LocalHost, 0))
        return QStringLiteral("Could not serve %1: %2").arg(folder, m_server.errorString());
    return {};
}

void StaticServer::stop()
{
    m_server.close();
}

void StaticServer::setPaused(bool paused)
{
    if (!m_server.isListening())
        return;
    if (paused)
        m_server.pauseAccepting();
    else
        m_server.resumeAccepting();
}

QUrl StaticServer::url() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort()));
}

QByteArray StaticServer::mimeType(const QString &path)
{
    static const QHash<QString, QByteArray> types{
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"}, {"css", "text/css; charset=utf-8"},
        {"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"}, {"json", "application/json"},
        {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"webp", "image/webp"}, {"ico", "image/x-icon"}, {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"txt", "text/plain; charset=utf-8"}};
    return types.value(QFileInfo(path).suffix().toLower(), "application/octet-stream");
}

void StaticServer::answer(QTcpSocket *socket)
{
    QByteArray request = socket->property("request").toByteArray() + socket->readAll();
    if (!request.contains("\r\n\r\n")) {
        socket->setProperty("request", request);
        return;
    }
    const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
    auto reply = [&](const QByteArray &status, const QByteArray &type, const QByteArray &body, bool head = false) {
        socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + (head ? QByteArray() : body));
        socket->disconnectFromHost();
    };
    if (first.size() < 2 || (first[0] != "GET" && first[0] != "HEAD")) {
        reply("405 Method Not Allowed", "text/plain", "Only GET and HEAD.\n");
        return;
    }
    QString path = QUrl::fromPercentEncoding(first[1].split('?').front());
    const QString resolved = QDir::cleanPath(m_folder + QLatin1Char('/') + path);
    QString file = resolved;
    if (QFileInfo(file).isDir())
        file = QDir(file).filePath(QStringLiteral("index.html"));
    const QString canonical = QFileInfo(file).canonicalFilePath();
    // Nothing outside the folder, symlinks included.
    if (canonical.isEmpty() || !(canonical == m_folder || canonical.startsWith(m_folder + QLatin1Char('/')))) {
        reply("404 Not Found", "text/plain", "Not found.\n", first[0] == "HEAD");
        return;
    }
    QFile content(canonical);
    if (!content.open(QIODevice::ReadOnly)) {
        reply("404 Not Found", "text/plain", "Not found.\n", first[0] == "HEAD");
        return;
    }
    reply("200 OK", mimeType(canonical), content.readAll(), first[0] == "HEAD");
}
