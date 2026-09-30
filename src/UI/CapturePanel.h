#pragma once
#include "Agent/Hyprland.h"
#include <QFutureWatcher>
#include <QImage>
#include <QProcess>
#include <QStringList>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;

// The dock's Capture tab (docs/WINDOW-LAYOUT.md): a region to trace, a colour from the screen, the
// clipboard's SVG and the theme's swatches. Each runs `omastrator island capture …`, which calls back into
// this app; a region or a colour first steps the window aside so what's behind it can be picked. Under them,
// the open windows as thumbnails: one click brings that window onto the canvas.
class CapturePanel : public QWidget {
    Q_OBJECT
public:
    explicit CapturePanel(QWidget *parent = nullptr);
    ~CapturePanel() override;

    // `omastrator island capture <args>`; public for tests.
    void capture(const QStringList &args, bool stepAside);
    bool isCapturing() const { return m_grabbing || m_process.state() != QProcess::NotRunning; }
    // Reads Hyprland's windows again and redraws the grid; thumbnails follow when grim is done.
    void refresh();
    // Brings one window onto the canvas (its grid cell's click); public for tests.
    void captureWindow(const Hyprland::Window &window);

signals:
    // The outcome, for the status bar.
    void notice(const QString &text);

protected:
    // The grid is read when the tab shows, never polled.
    void showEvent(QShowEvent *event) override;

private:
    struct Grab {
        QString path, error;
    };
    void finished();
    void windowGrabbed();
    void thumbnailsReady();
    void setBusy(bool busy);

    QProcess m_process;
    // The workspace to come back to after stepping aside, as a dispatch selector.
    QString m_returnTo;
    QList<QPushButton *> m_buttons;
    QList<QToolButton *> m_cells;
    QGridLayout *m_windowGrid = nullptr;
    QLabel *m_empty = nullptr;
    QPushButton *m_refresh = nullptr;
    QFutureWatcher<QList<QImage>> m_thumbnails;
    QFutureWatcher<Grab> m_grab;
    // Dropped when the grid was redrawn while grim ran.
    int m_generation = 0;
    int m_thumbnailGeneration = 0;
    QStringList m_thumbnailAddresses;
    bool m_grabbing = false;
    QString m_grabName;
    QLabel *const m_status;
};
