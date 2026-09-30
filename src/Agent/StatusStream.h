#pragma once
#include "Agent/Island.h"
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTextStream>
#include <QTimer>

// `omastrator status [--follow]`: the app's status_get merged with the
// mode and activity state, one compact JSON line each time either changes.
// The shell plugins (design mode's overlay, the tray light) read it with
// quickshell's Process. See docs/OS-SUITE.md.
namespace StatusStream {
// Every key is present, with the app's own values when `app` is non-empty.
QJsonObject compose(const QJsonObject &app, const Island::State &island);

class Follower : public QObject {
    Q_OBJECT
public:
    explicit Follower(QTextStream &out, QObject *parent = nullptr);
    ~Follower() override;
    void start();
    const QJsonObject &last() const { return m_last; }

signals:
    void printed(const QJsonObject &status);

private:
    void connectToApp();
    void readApp();
    void appGone();
    void print();

    QTextStream &m_out;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QFileSystemWatcher m_watcher;
    QTimer m_retry;
    QJsonObject m_app;
    QJsonObject m_last;
};

// `status` prints one line; `status --follow` runs until killed.
int runCli(const QStringList &args, QTextStream &out, QTextStream &err);
}
