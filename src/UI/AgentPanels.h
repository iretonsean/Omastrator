#pragma once
#include <QImage>
#include <QString>
#include <QWidget>

class AgentBridge;
class EditorSession;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// Window ▸ Variations: each round's options as thumbnails, newest first. A
// click inserts one as a proposal; Refine asks for another round from it.
class VariationsPanel : public QWidget {
    Q_OBJECT
public:
    explicit VariationsPanel(AgentBridge &bridge, QWidget *parent = nullptr);
    // An SVG drawn through VectorRenderer to fit `side` pixels; null when it doesn't parse.
    static QImage thumbnail(const QString &svg, int side);

private:
    void rebuild();
    void synchronizeWaiting();

    AgentBridge &m_bridge;
    QLabel *const m_status;
    QPushButton *const m_cancel;
    QLabel *const m_message;
    QVBoxLayout *m_rounds = nullptr;
    QLineEdit *const m_refine;
    QPushButton *const m_refineButton;
};

// Roast My Design's results: the roast, then the sincere feedback, then one
// click to generate from it.
class RoastPanel : public QWidget {
    Q_OBJECT
public:
    explicit RoastPanel(AgentBridge &bridge, QWidget *parent = nullptr);
    int page() const { return m_page; }
    void showPage(int page);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuild();
    void buildBody(QWidget &body, QVBoxLayout &column);

    AgentBridge &m_bridge;
    QVBoxLayout *const m_column;
    int m_page = 0;
};

// Above the canvas while an agent works on it: waiting, then its proposal
// with Keep (Enter) and Discard (Esc).
class ProposalBar : public QWidget {
    Q_OBJECT
public:
    ProposalBar(AgentBridge &bridge, EditorSession &session, QWidget *parent = nullptr);
    void synchronize();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    AgentBridge &m_bridge;
    EditorSession &m_session;
    QLabel *const m_text;
    QLabel *const m_summary;
    QPushButton *const m_keep;
    QPushButton *const m_discard;
    QPushButton *const m_cancel;
};
