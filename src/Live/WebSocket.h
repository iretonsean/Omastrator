#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QUrl>

// A small RFC 6455 client for ws:// on localhost, enough for the Chrome
// DevTools Protocol: text messages of any size, fragments, ping and close.
// Omastrator carries its own so qt6-websockets isn't a dependency.
class WebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit WebSocketClient(QObject *parent = nullptr);
    ~WebSocketClient() override;

    void open(const QUrl &url);
    bool isOpen() const { return m_state == State::open; }
    void sendText(const QString &text);
    void close();

    // One frame as a client sends it, masked with `mask`; exposed for tests.
    static QByteArray frame(quint8 opcode, const QByteArray &payload, quint32 mask, bool final = true);
    // The Sec-WebSocket-Accept a server must answer for `key`.
    static QByteArray acceptFor(const QByteArray &key);

signals:
    void connected();
    void textReceived(const QString &text);
    void disconnected();
    void errorOccurred(const QString &message);

private:
    enum class State { closed, handshaking, open, closing };
    void readHandshake();
    void readFrames();
    void send(quint8 opcode, const QByteArray &payload);
    void fail(const QString &message);

    QTcpSocket m_socket;
    State m_state = State::closed;
    QByteArray m_key;
    QByteArray m_buffer;
    // A message arriving in fragments.
    QByteArray m_message;
    quint8 m_messageOpcode = 0;
    QString m_resource;
    QString m_host;
};
