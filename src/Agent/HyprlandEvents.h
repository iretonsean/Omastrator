#pragma once
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <optional>

// Hyprland's event stream (`.socket2.sock`), for docs/WORKSPACES.md. Only the events pages need are kept.
class HyprlandEvents : public QObject {
    Q_OBJECT
public:
    struct Event {
        enum class Kind { workspace, openWindow, closeWindow, moveWindow, destroyWorkspace, configReloaded };
        Kind kind = Kind::workspace;
        // With the 0x that `clients` uses, though the stream leaves it off.
        QString address;
        int workspaceId = 0;
        QString workspaceName;
        QString title;
        bool operator==(const Event &) const = default;
    };

    explicit HyprlandEvents(QObject *parent = nullptr);

    // One line of the stream; nothing for events we don't use. Names may hold commas and `>>`: only the
    // first `>>` splits, and a name is what's left of the line (openwindow's workspace name, split from the left, can't hold one).
    static std::optional<Event> parse(const QString &line);
    // $OMASTRATOR_HYPRLAND_EVENTS, else $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock; empty when there's neither.
    static QString socketPath();

    // Connects; returns why not, or empty.
    QString start();
    void stop();
    bool isRunning() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    // Splits bytes from the socket into lines and emits `event` for each we use; a line can arrive in pieces.
    void feed(const QByteArray &bytes);

signals:
    void event(const HyprlandEvents::Event &event);
    // Hyprland went away; not emitted by stop().
    void closed();

private:
    QLocalSocket m_socket;
    QByteArray m_pending;
    bool m_stopping = false;
};
