#include "Live/Cdp.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

CdpConnection::CdpConnection(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &WebSocketClient::textReceived, this, &CdpConnection::receive);
    connect(&m_socket, &WebSocketClient::disconnected, this, [this] {
        // Whoever waits hears that nothing is coming.
        const auto waiting = std::exchange(m_replies, {});
        for (const Reply &reply : waiting) {
            if (reply)
                reply({}, QStringLiteral("The browser closed the connection."));
        }
        emit closed();
    });
}

void CdpConnection::open(const QUrl &url)
{
    m_socket.open(url);
}

void CdpConnection::close()
{
    m_socket.close();
}

bool CdpConnection::openAndWait(const QUrl &url, QString *error, int timeoutMs)
{
    QEventLoop loop;
    QString failure = QStringLiteral("The browser's DevTools did not answer in time.");
    bool ok = false;
    const auto connected = connect(&m_socket, &WebSocketClient::connected, &loop, [&] {
        ok = true;
        loop.quit();
    });
    const auto failed = connect(&m_socket, &WebSocketClient::errorOccurred, &loop, [&](const QString &message) {
        failure = message;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    m_socket.open(url);
    loop.exec();
    disconnect(connected);
    disconnect(failed);
    if (!ok && error)
        *error = failure;
    return ok;
}

void CdpConnection::call(const QString &method, const QJsonObject &params, const QString &sessionId, Reply reply)
{
    const int id = m_nextId++;
    QJsonObject message{{"id", id}, {"method", method}, {"params", params}};
    if (!sessionId.isEmpty())
        message["sessionId"] = sessionId;
    if (reply)
        m_replies.insert(id, std::move(reply));
    m_socket.sendText(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

QJsonObject CdpConnection::callAndWait(const QString &method, const QJsonObject &params, const QString &sessionId, QString *error, int timeoutMs)
{
    QEventLoop loop;
    QJsonObject answer;
    QString failure = QStringLiteral("The browser did not answer %1 in time.").arg(method);
    bool answered = false;
    // An answer after the timeout finds the loop gone and touches nothing.
    QPointer<QEventLoop> alive(&loop);
    call(method, params, sessionId, [&, alive](const QJsonObject &result, const QString &message) {
        if (!alive)
            return;
        answered = true;
        answer = result;
        failure = message;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    if (error)
        *error = failure;
    return answered && failure.isEmpty() ? answer : QJsonObject();
}

void CdpConnection::receive(const QString &text)
{
    const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
    if (message.contains("id")) {
        const Reply reply = m_replies.take(message["id"].toInt());
        if (!reply)
            return;
        if (message.contains("error"))
            reply({}, message["error"].toObject()["message"].toString());
        else
            reply(message["result"].toObject(), QString());
        return;
    }
    emit event(message["method"].toString(), message["params"].toObject(), message["sessionId"].toString());
}
