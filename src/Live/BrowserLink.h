#pragma once
#include "Live/Cdp.h"
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

class QLocalServer;

// The user's own Chromium (docs/OS-SUITE.md, "Live in your own browser"):
// Omastrator's extension talks to `omastrator browser-host`, which Chromium
// starts as a native messaging host and which connects here. One JSON object
// per line, each with a "type":
//   hello   host → here: {pid: Chromium's browser process, version}
//   cdp     both ways: a DevTools message. The extension runs it with
//           chrome.debugger on a tab; sessions are "tab-<id>". Its own methods:
//           Omastrator.activeTab, Omastrator.attach {tabId}, Omastrator.detach
//           {sessionId}, Omastrator.inspect {title, x, y, width, height} (Inspect
//           in the tab a window shows, with chrome.scripting), and the event
//           Omastrator.detached {sessionId, reason}.
//   call    extension → here: {id, method, params}, the agent socket's methods,
//           answered by a reply {id, result} or {id, error}.
//   status  here → extension: the status stream's object, as it changes.
// The newest host wins; the socket is the user's only (0600).
class BrowserLink : public QObject {
    Q_OBJECT
public:
    using Caller = std::function<QJsonObject(const QString &method, const QJsonObject &params, QString *error)>;

    explicit BrowserLink(QObject *parent = nullptr);
    ~BrowserLink() override;

    // $XDG_RUNTIME_DIR/omastrator-browser.sock, or $OMASTRATOR_BROWSER_SOCKET.
    static QString socketPath();
    QString listen(const QString &path = socketPath());
    void close();

    bool isConnected() const { return m_host && m_hello; }
    // Chromium's browser process, which owns its windows; 0 until the host says hello.
    qint64 chromiumPid() const { return m_pid; }
    QString extensionVersion() const { return m_version; }
    CdpConnection &cdp() { return m_cdp; }

    // What the extension's calls run, and what its status says.
    void setCaller(Caller caller) { m_caller = std::move(caller); }
    void setStatus(std::function<QJsonObject()> status) { m_status = std::move(status); }
    // Something the status shows may have changed; the extension hears the settled state.
    void statusMayHaveChanged();

signals:
    void connectedChanged();

private:
    void accept();
    void read();
    void handle(const QJsonObject &message);
    void drop();
    void write(const QJsonObject &message);
    void publishStatus();

    QLocalServer *m_server = nullptr;
    QPointer<QLocalSocket> m_host;
    QByteArray m_buffer;
    bool m_hello = false;
    qint64 m_pid = 0;
    QString m_version;
    CdpConnection m_cdp;
    Caller m_caller;
    std::function<QJsonObject()> m_status;
    QJsonObject m_lastStatus;
    QTimer m_statusTimer;
};
