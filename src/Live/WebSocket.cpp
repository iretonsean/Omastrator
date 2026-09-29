#include "Live/WebSocket.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtEndian>

namespace {
constexpr quint8 continuation = 0x0, text = 0x1, binary = 0x2, closeFrame = 0x8, ping = 0x9, pong = 0xA;
// Far past any DevTools message; beyond it the peer is broken.
constexpr quint64 maximumMessage = 256ull * 1024 * 1024;
}

WebSocketClient::WebSocketClient(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        const QString request = QStringLiteral(
                                    "GET %1 HTTP/1.1\r\nHost: %2\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: %3\r\nSec-WebSocket-Version: 13\r\n\r\n")
                                    .arg(m_resource, m_host, QString::fromLatin1(m_key));
        m_socket.write(request.toLatin1());
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        m_buffer += m_socket.readAll();
        if (m_state == State::handshaking)
            readHandshake();
        if (m_state == State::open || m_state == State::closing)
            readFrames();
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        const bool wasOpen = m_state != State::closed;
        m_state = State::closed;
        if (wasOpen)
            emit disconnected();
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_state == State::handshaking)
            fail(m_socket.errorString());
    });
}

WebSocketClient::~WebSocketClient()
{
    m_socket.disconnect(this);
}

void WebSocketClient::open(const QUrl &url)
{
    m_buffer.clear();
    m_message.clear();
    QByteArray nonce(16, Qt::Uninitialized);
    for (char &byte : nonce)
        byte = char(QRandomGenerator::global()->bounded(256));
    m_key = nonce.toBase64();
    m_resource = url.path(QUrl::FullyEncoded).isEmpty() ? QStringLiteral("/") : url.path(QUrl::FullyEncoded);
    if (url.hasQuery())
        m_resource += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
    m_host = QStringLiteral("%1:%2").arg(url.host(), QString::number(url.port(80)));
    m_state = State::handshaking;
    m_socket.connectToHost(url.host(), quint16(url.port(80)));
}

QByteArray WebSocketClient::acceptFor(const QByteArray &key)
{
    return QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
}

void WebSocketClient::readHandshake()
{
    const qsizetype end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0)
        return;
    const QByteArray head = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty() || !lines.front().contains(" 101 ")) {
        fail(QStringLiteral("The browser refused the connection: %1").arg(QString::fromLatin1(lines.value(0).trimmed())));
        return;
    }
    QByteArray accept;
    for (const QByteArray &line : lines) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-accept")
            accept = line.mid(colon + 1).trimmed();
    }
    if (accept != acceptFor(m_key)) {
        fail(QStringLiteral("The browser's WebSocket answer did not match."));
        return;
    }
    m_state = State::open;
    emit connected();
}

void WebSocketClient::readFrames()
{
    for (;;) {
        if (m_buffer.size() < 2)
            return;
        const auto *bytes = reinterpret_cast<const uchar *>(m_buffer.constData());
        const bool final = bytes[0] & 0x80;
        const quint8 opcode = bytes[0] & 0x0F;
        const bool masked = bytes[1] & 0x80;
        quint64 length = bytes[1] & 0x7F;
        qsizetype at = 2;
        if (length == 126) {
            if (m_buffer.size() < 4)
                return;
            length = qFromBigEndian<quint16>(bytes + 2);
            at = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10)
                return;
            length = qFromBigEndian<quint64>(bytes + 2);
            at = 10;
        }
        if (length > maximumMessage) {
            fail(QStringLiteral("The browser sent a message larger than 256 MB."));
            return;
        }
        quint8 mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (m_buffer.size() < at + 4)
                return;
            std::copy(bytes + at, bytes + at + 4, mask);
            at += 4;
        }
        if (quint64(m_buffer.size() - at) < length)
            return;
        QByteArray payload = m_buffer.mid(at, qsizetype(length));
        m_buffer.remove(0, at + qsizetype(length));
        if (masked) {
            for (qsizetype index = 0; index < payload.size(); ++index)
                payload[index] = char(payload[index] ^ mask[index % 4]);
        }
        switch (opcode) {
        case text:
        case binary:
            m_message = payload;
            m_messageOpcode = opcode;
            break;
        case continuation:
            m_message += payload;
            break;
        case ping:
            send(pong, payload);
            continue;
        case pong:
            continue;
        case closeFrame:
            if (m_state == State::open)
                send(closeFrame, payload.left(2));
            m_state = State::closing;
            m_socket.disconnectFromHost();
            return;
        default:
            fail(QStringLiteral("The browser sent an unknown WebSocket frame."));
            return;
        }
        if (final && (opcode == text || opcode == binary || opcode == continuation)) {
            const QByteArray message = std::exchange(m_message, {});
            if (m_messageOpcode == text)
                emit textReceived(QString::fromUtf8(message));
        }
    }
}

QByteArray WebSocketClient::frame(quint8 opcode, const QByteArray &payload, quint32 mask, bool final)
{
    QByteArray out;
    out.append(char((final ? 0x80 : 0) | opcode));
    const quint64 length = quint64(payload.size());
    if (length < 126) {
        out.append(char(0x80 | length));
    } else if (length <= 0xFFFF) {
        out.append(char(0x80 | 126));
        uchar size[2];
        qToBigEndian<quint16>(quint16(length), size);
        out.append(reinterpret_cast<const char *>(size), 2);
    } else {
        out.append(char(0x80 | 127));
        uchar size[8];
        qToBigEndian<quint64>(length, size);
        out.append(reinterpret_cast<const char *>(size), 8);
    }
    uchar key[4];
    qToBigEndian<quint32>(mask, key);
    out.append(reinterpret_cast<const char *>(key), 4);
    const qsizetype start = out.size();
    out.append(payload);
    for (qsizetype index = 0; index < payload.size(); ++index)
        out[start + index] = char(out[start + index] ^ key[index % 4]);
    return out;
}

void WebSocketClient::send(quint8 opcode, const QByteArray &payload)
{
    m_socket.write(frame(opcode, payload, QRandomGenerator::global()->generate()));
}

void WebSocketClient::sendText(const QString &message)
{
    if (m_state == State::open)
        send(text, message.toUtf8());
}

void WebSocketClient::close()
{
    if (m_state == State::open) {
        send(closeFrame, QByteArray("\x03\xe8", 2));
        m_state = State::closing;
        m_socket.flush();
    }
    m_socket.disconnectFromHost();
}

void WebSocketClient::fail(const QString &message)
{
    m_state = State::closed;
    m_socket.abort();
    emit errorOccurred(message);
}
