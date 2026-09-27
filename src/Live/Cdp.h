#pragma once
#include "Live/WebSocket.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>

// The Chrome DevTools Protocol over one WebSocket: commands answered by id,
// events signalled. Sessions are flat: a page's commands carry its sessionId.
class CdpConnection : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject &result, const QString &error)>;

    explicit CdpConnection(QObject *parent = nullptr);
    void open(const QUrl &url);
    bool isOpen() const { return m_socket.isOpen(); }
    void close();

    void call(const QString &method, const QJsonObject &params, const QString &sessionId, Reply reply = {});
    // Waits in a local event loop; `error` gets the reason when it fails.
    QJsonObject callAndWait(const QString &method, const QJsonObject &params, const QString &sessionId, QString *error = nullptr,
                            int timeoutMs = 15'000);
    // Opens the socket and waits for it.
    bool openAndWait(const QUrl &url, QString *error, int timeoutMs = 10'000);

signals:
    void event(const QString &method, const QJsonObject &params, const QString &sessionId);
    void closed();

private:
    void receive(const QString &text);

    WebSocketClient m_socket;
    int m_nextId = 1;
    QHash<int, Reply> m_replies;
};
