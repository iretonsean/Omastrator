#include "Live/Cdp.h"
#include "Live/WebSocket.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QtEndian>

// The in-tree WebSocket client against a small server written here from RFC 6455.
namespace {
struct Frame {
    bool final;
    quint8 opcode;
    QByteArray payload;
    bool masked;
};

// Reads one client frame from `buffer`, unmasking it; nullopt until it is whole.
std::optional<Frame> takeFrame(QByteArray &buffer)
{
    if (buffer.size() < 2)
        return std::nullopt;
    const auto *bytes = reinterpret_cast<const uchar *>(buffer.constData());
    quint64 length = bytes[1] & 0x7F;
    qsizetype at = 2;
    if (length == 126) {
        length = qFromBigEndian<quint16>(bytes + 2);
        at = 4;
    } else if (length == 127) {
        length = qFromBigEndian<quint64>(bytes + 2);
        at = 10;
    }
    const bool masked = bytes[1] & 0x80;
    const qsizetype start = at + (masked ? 4 : 0);
    if (buffer.size() < start + qsizetype(length))
        return std::nullopt;
    QByteArray payload = buffer.mid(start, qsizetype(length));
    for (qsizetype i = 0; masked && i < payload.size(); ++i)
        payload[i] = char(payload[i] ^ bytes[at + i % 4]);
    Frame frame{bool(bytes[0] & 0x80), quint8(bytes[0] & 0x0F), payload, masked};
    buffer.remove(0, start + qsizetype(length));
    return frame;
}

// A server frame: never masked.
QByteArray serverFrame(quint8 opcode, const QByteArray &payload, bool final = true)
{
    QByteArray out;
    out.append(char((final ? 0x80 : 0) | opcode));
    if (payload.size() < 126) {
        out.append(char(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        out.append(char(126));
        uchar size[2];
        qToBigEndian<quint16>(quint16(payload.size()), size);
        out.append(reinterpret_cast<const char *>(size), 2);
    } else {
        out.append(char(127));
        uchar size[8];
        qToBigEndian<quint64>(quint64(payload.size()), size);
        out.append(reinterpret_cast<const char *>(size), 8);
    }
    return out + payload;
}

// Answers the handshake, then echoes text; "fragment" comes back in three pieces, "ping" gets a ping first.
class EchoServer : public QObject {
public:
    QTcpServer server;
    QByteArray buffer;
    QList<Frame> received;
    bool badAccept = false;

    EchoServer()
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
        });
    }

    void read(QTcpSocket *socket)
    {
        buffer += socket->readAll();
        if (!socket->property("open").toBool()) {
            const qsizetype end = buffer.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            QByteArray key;
            for (const QByteArray &line : buffer.left(end).split('\n'))
                if (line.toLower().startsWith("sec-websocket-key:"))
                    key = line.mid(line.indexOf(':') + 1).trimmed();
            buffer.remove(0, end + 4);
            const QByteArray accept = badAccept ? QByteArray("nope") : WebSocketClient::acceptFor(key);
            socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
            socket->setProperty("open", true);
        }
        while (const auto frame = takeFrame(buffer)) {
            received << *frame;
            if (frame->opcode == 0x1) {
                if (frame->payload == "fragment") {
                    socket->write(serverFrame(0x1, "frag", false) + serverFrame(0x0, "men", false) + serverFrame(0x0, "ted", true));
                } else if (frame->payload == "ping") {
                    socket->write(serverFrame(0x9, "are you there") + serverFrame(0x1, "pinged"));
                } else if (frame->payload.startsWith("{")) {
                    // A DevTools-style answer, then an event.
                    const QJsonObject request = QJsonDocument::fromJson(frame->payload).object();
                    socket->write(serverFrame(0x1, QJsonDocument(QJsonObject{{"method", "Page.loadEventFired"}, {"params", QJsonObject{{"t", 1}}}, {"sessionId", "S"}}).toJson(QJsonDocument::Compact)));
                    const QJsonObject answer = request["method"] == QLatin1String("Bad.method")
                                                   ? QJsonObject{{"id", request["id"]}, {"error", QJsonObject{{"message", "no such method"}}}}
                                                   : QJsonObject{{"id", request["id"]}, {"result", QJsonObject{{"echo", request["params"]}}}};
                    socket->write(serverFrame(0x1, QJsonDocument(answer).toJson(QJsonDocument::Compact)));
                } else {
                    socket->write(serverFrame(0x1, frame->payload));
                }
            } else if (frame->opcode == 0x8) {
                socket->write(serverFrame(0x8, frame->payload));
                socket->disconnectFromHost();
            }
        }
    }

    QUrl url() const { return QUrl(QStringLiteral("ws://127.0.0.1:%1/devtools/browser/x").arg(server.serverPort())); }
};
}

class WebSocketTests : public QObject {
    Q_OBJECT

private slots:
    void acceptKeyMatchesTheRfc()
    {
        // RFC 6455's own example.
        QCOMPARE(WebSocketClient::acceptFor("dGhlIHNhbXBsZSBub25jZQ=="), QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    }

    void clientFramesAreMasked()
    {
        QByteArray frame = WebSocketClient::frame(0x1, "Hello", 0x37fa213d);
        QCOMPARE(frame, QByteArray::fromHex("818537fa213d7f9f4d5158"));
        const auto parsed = takeFrame(frame);
        QVERIFY(parsed && parsed->masked);
        QCOMPARE(parsed->payload, QByteArray("Hello"));
        // Lengths past 125 and 65535 take the longer size fields.
        for (const qsizetype size : {qsizetype(200), qsizetype(70'000)}) {
            QByteArray big = WebSocketClient::frame(0x1, QByteArray(size, 'x'), 1);
            QCOMPARE(takeFrame(big)->payload.size(), size);
        }
    }

    void talksToAServer()
    {
        EchoServer server;
        WebSocketClient client;
        QSignalSpy connected(&client, &WebSocketClient::connected);
        QSignalSpy texts(&client, &WebSocketClient::textReceived);
        client.open(server.url());
        QVERIFY(connected.wait(3000));
        client.sendText(QStringLiteral("héllo"));
        QVERIFY(texts.wait(3000));
        QCOMPARE(texts.last().front().toString(), QStringLiteral("héllo"));
        const QString large(100'000, QLatin1Char('z'));
        client.sendText(large);
        QTRY_COMPARE(texts.last().front().toString().size(), large.size());
        client.sendText(QStringLiteral("fragment"));
        QTRY_COMPARE(texts.last().front().toString(), QStringLiteral("fragmented"));
        // A ping is answered with a pong carrying its payload.
        client.sendText(QStringLiteral("ping"));
        QTRY_COMPARE(texts.last().front().toString(), QStringLiteral("pinged"));
        QTRY_VERIFY(std::any_of(server.received.begin(), server.received.end(),
                                [](const Frame &frame) { return frame.opcode == 0xA && frame.payload == "are you there"; }));
        for (const Frame &frame : std::as_const(server.received))
            QVERIFY(frame.masked);
        QSignalSpy disconnected(&client, &WebSocketClient::disconnected);
        client.close();
        QTRY_VERIFY(!disconnected.isEmpty());
    }

    void aWrongAcceptIsRefused()
    {
        EchoServer server;
        server.badAccept = true;
        WebSocketClient client;
        QSignalSpy failed(&client, &WebSocketClient::errorOccurred);
        client.open(server.url());
        QVERIFY(failed.wait(3000));
        QVERIFY(!client.isOpen());
    }

    void cdpMatchesAnswersToCalls()
    {
        EchoServer server;
        CdpConnection cdp;
        QString error;
        QVERIFY(cdp.openAndWait(server.url(), &error));
        QSignalSpy events(&cdp, &CdpConnection::event);
        const QJsonObject result = cdp.callAndWait(QStringLiteral("Runtime.evaluate"), {{"expression", "1+1"}}, QStringLiteral("S"), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(result["echo"].toObject()["expression"].toString(), QStringLiteral("1+1"));
        QVERIFY(!events.isEmpty());
        QCOMPARE(events.front()[0].toString(), QStringLiteral("Page.loadEventFired"));
        QCOMPARE(events.front()[2].toString(), QStringLiteral("S"));
        cdp.callAndWait(QStringLiteral("Bad.method"), {}, QString(), &error);
        QCOMPARE(error, QStringLiteral("no such method"));
        // Nobody answers a closed socket; the waiter hears so.
        server.server.close();
        cdp.close();
        cdp.callAndWait(QStringLiteral("Page.enable"), {}, QString(), &error, 500);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(WebSocketTests)
#include "WebSocketTests.moc"
