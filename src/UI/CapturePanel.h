#pragma once
#include <QProcess>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;

// The dock's Capture tab (docs/WINDOW-LAYOUT.md): a region to trace, a colour from the screen, the
// clipboard's SVG and the theme's swatches. Each runs `omastrator island capture …`, which calls back into
// this app; a region or a colour first steps the window aside so what's behind it can be picked.
class CapturePanel : public QWidget {
    Q_OBJECT
public:
    explicit CapturePanel(QWidget *parent = nullptr);
    ~CapturePanel() override;

    // `omastrator island capture <args>`; public for tests.
    void capture(const QStringList &args, bool stepAside);
    bool isCapturing() const { return m_process.state() != QProcess::NotRunning; }

signals:
    // The outcome, for the status bar.
    void notice(const QString &text);

private:
    void finished();
    void setBusy(bool busy);

    QProcess m_process;
    // The workspace to come back to after stepping aside, as a dispatch selector.
    QString m_returnTo;
    QList<QPushButton *> m_buttons;
    QLabel *const m_status;
};
