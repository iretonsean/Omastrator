#pragma once
#include <QPointer>
#include <QWidget>

class AgentBridge;
class QVBoxLayout;

// Live Review: each write-back as a diff with Keep and Discard, then Write
// Back, Save and Publish. Discard puts back exactly the files that changed.
class LiveReviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit LiveReviewPanel(AgentBridge &bridge, QWidget *parent = nullptr);

private:
    void rebuild();
    void report(const QString &failure);

    AgentBridge &m_bridge;
    QVBoxLayout *const m_outer;
    QPointer<QWidget> m_body;
};
