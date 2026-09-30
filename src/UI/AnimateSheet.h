#pragma once
#include <QFrame>
#include <QStringList>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// The Animate sheet under the element bar (docs/MOTION.md, section 4): what motion to write, with suggestions as chips, the
// agent that will write it, and Cancel and Generate. While the agent writes it says "Writing…" with the steps, and Stop.
// Esc closes it before anything else does: the first Esc discards the sheet, the second leaves Edit Page.
class AnimateSheet : public QFrame {
    Q_OBJECT
public:
    explicit AnimateSheet(QWidget *parent);

    // Shows it empty, for `elements` picked elements written by `agent` ("Claude"). Several elements are one group.
    void open(const QString &agent, int elements);
    // Hidden again, and its text kept for the next time only while it is running.
    void close();
    bool isRunning() const { return m_running; }
    QString text() const;
    bool reducedMotion() const { return m_reduced; }
    // The run began or ended: Generate becomes "Writing…" with Stop, and the steps show ("✓ …", "… …", "· …").
    void setRunning(bool running);
    void setSteps(const QStringList &lines);
    static QStringList suggestions();

signals:
    void generate(const QString &instruction, bool reducedMotion);
    // Cancel, and Esc while nothing runs.
    void cancelled();
    // Stop, and Esc while it runs.
    void stopped();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // Return in the field writes and Esc closes: the field would take both as its own.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    QPlainTextEdit *m_field = nullptr;
    QLabel *m_agent = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_steps = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_generate = nullptr;
    QToolButton *m_more = nullptr;
    QWidget *m_chips = nullptr;
    bool m_running = false;
    bool m_reduced = true;
    QString m_name;
    int m_elements = 1;
};
