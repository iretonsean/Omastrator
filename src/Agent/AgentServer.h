#pragma once
#include <QHash>
#include <QObject>
#include <QString>

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

private:
    void accept();
    void read(QLocalSocket *socket);

    AgentTools &m_tools;
    QLocalServer *m_server;
    QString m_path;
    // Bytes of a request line still arriving, per client.
    QHash<QLocalSocket *, QByteArray> m_pending;
};
