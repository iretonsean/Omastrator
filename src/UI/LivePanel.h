#pragma once
#include <QPointer>
#include <QWidget>

class AgentBridge;
class QVBoxLayout;

// The Live panel: Deploy first, with what it's doing now; Save, GitHub and
// History beside it. The write-backs' diffs stay out of the way until Review
// changes is pressed, each with Discard.
class LivePanel : public QWidget {
    Q_OBJECT
public:
    explicit LivePanel(AgentBridge &bridge, QWidget *parent = nullptr);
    void showChanges(bool shown);
    bool changesShown() const { return m_changes; }

private:
    void rebuild();
    void report(const QString &failure);

    AgentBridge &m_bridge;
    QVBoxLayout *const m_outer;
    QPointer<QWidget> m_body;
    bool m_changes = false;
};
