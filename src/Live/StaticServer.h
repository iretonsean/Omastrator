#pragma once
#include <QString>
#include <QTcpServer>
#include <QUrl>

// Serves a folder over HTTP on localhost, for sites with no dev script.
// GET and HEAD only; paths never leave the folder.
class StaticServer : public QObject {
    Q_OBJECT
public:
    explicit StaticServer(QObject *parent = nullptr);
    // Returns why it could not listen, or empty.
    QString serve(const QString &folder);
    void stop();
    // Frozen: connections wait unanswered until it resumes (Browser View off).
    void setPaused(bool paused);
    QUrl url() const;
    static QByteArray mimeType(const QString &path);

private:
    void answer(QTcpSocket *socket);

    QTcpServer m_server;
    QString m_folder;
};
