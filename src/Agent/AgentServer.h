#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class AgentTools;
class QLocalServer;
class QLocalSocket;

// Listens on AgentProtocol::socketPath() for the CLI and the MCP bridge and
// answers each request line through AgentTools, on this object's thread.
class AgentServer : public QObject {
    Q_OBJECT
public:
    explicit AgentServer(AgentTools &tools, QObject *parent = nullptr);
    ~AgentServer() override;

    // Removes a stale socket first; another live Omastrator keeps its own. Returns why it failed, or empty.
    QString listen(const QString &path);
    QString listen();
    void close();
    bool isListening() const;
    QString path() const { return m_path; }

    // Something status_get reports may have changed; followers hear once the event loop settles, if it did.
    void statusMayHaveChanged();
    // Sends the status now to each follower it changed for.
    void publishStatus();

private:
    void accept();
    void read(QLocalSocket *socket);

    AgentTools &m_tools;
    QLocalServer *m_server;
    QString m_path;
    // Bytes of a request line still arriving, per client.
    QHash<QLocalSocket *, QByteArray> m_pending;
    // Clients that called status_follow, and the status each was last sent.
    QHash<QLocalSocket *, QJsonObject> m_followers;
    QTimer m_statusTimer;
};
