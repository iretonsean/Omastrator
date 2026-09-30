#pragma once
#include <QPointer>
#include <QProcess>
#include <QWidget>

class AgentBridge;
class EditorSession;
class ProposalBar;
class QLabel;
class QLineEdit;
class QToolButton;

// The window's third row (docs/WINDOW-LAYOUT.md): what's selected, the agent's state, and Ask
// with a mic. Spoken words fill Ask; the local grammar runs at once, the rest waits for Enter.
class ContextBar : public QWidget {
    Q_OBJECT
public:
    // Without an agent bridge there's no Ask, no mic and no agent state.
    ContextBar(EditorSession &session, AgentBridge *agent, QWidget *parent = nullptr);
    ~ContextBar() override;

    ProposalBar *proposalBar() const { return m_proposal; }
    QLineEdit *askField() const { return m_ask; }
    // Push-to-talk, as the mic button and Super+Alt+V do it.
    void startListening();
    void stopListening();
    bool isListening() const;
    // What was heard: fills Ask, and runs a local-grammar command at once. Public for tests.
    void heard(const QString &words);
    void synchronize();

signals:
    // A line for the status bar ("Heard: … → Align left").
    void notice(const QString &text);

private:
    void ask();

    EditorSession &m_session;
    const QPointer<AgentBridge> m_agent;
    QLabel *const m_selection;
    ProposalBar *m_proposal = nullptr;
    QLineEdit *m_ask = nullptr;
    QToolButton *m_mic = nullptr;
    QProcess m_recorder;
    QString m_wav;
    bool m_transcribing = false;
};
