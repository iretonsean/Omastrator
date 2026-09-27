#pragma once
#include <QPointer>
#include <QWidget>

class AgentBridge;
class QVBoxLayout;

// History: the project's commits, newest first, with what each changed, who
// made it, which were deployed and where, Restore and Open on GitHub.
class LiveHistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit LiveHistoryPanel(AgentBridge &bridge, QWidget *parent = nullptr);

private:
    void rebuild();

    AgentBridge &m_bridge;
    QVBoxLayout *const m_outer;
    QPointer<QWidget> m_body;
    QString m_message;
};
