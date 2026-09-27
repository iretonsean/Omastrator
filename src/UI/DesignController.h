#pragma once
#include "Anywhere/DesignMode.h"
#include "Anywhere/Desk.h"
#include "Anywhere/Overlays.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>

class AgentBridge;
class ProjectTab;
class ProjectWorkspace;
class QWidget;

// Design mode everywhere, in the app process (docs/ANYWHERE.md): the mode and
// its inspection, the overlays and their art, the floating bar's actions,
// Ask, onboarding and the Desk. The island and the overlay in omarchy-shell
// read its state from the status stream and act through the `design` method.
class DesignController : public QObject {
    Q_OBJECT
public:
    DesignController(AgentBridge &bridge, ProjectWorkspace &workspace, QWidget &window);
    ~DesignController() override;

    // Tests give a fake desktop; the app reads the real one. Call before start().
    void setSource(std::unique_ptr<DesktopSource> source);
    // Loads the overlays and follows the island's mode.
    void start();

    DesignMode &mode() { return *m_mode; }
    OverlayStore &overlays() { return m_overlays; }
    DesktopSource &source() { return *m_source; }

    // The `design` method's actions; `result` gets what each returns. Returns why it failed, or empty.
    QString run(const QString &action, const QJsonObject &params, QJsonObject &result);
    // The status stream's "design" key.
    QJsonObject status();

    // The Desk's tab, opened (without showing the window) when needed. Null with `error` set if the file can't be read.
    ProjectTab *deskTab(QString *error = nullptr);
    // "show" (its workspace), "window" (a normal window where you are), "toggle" or "hide".
    QString desk(const QString &how);

    // The last line said about design mode, shown on the bar.
    QString message() const { return m_message; }

signals:
    void changed();

private:
    struct Target {
        // An inspection, or the art selected on a surface.
        std::optional<Inspection> inspection;
        QString artSurface;
    };
    // `params.target` (an inspection id), else the selected art, else what's selected or hovered.
    std::optional<Target> target(const QJsonObject &params, QString *error);
    QString kindOf(const Target &target) const;
    QString surfaceKeyOf(const Target &target) const;
    Surface surfaceOf(const Target &target) const;

    QString draw(const QJsonObject &params, QJsonObject &result);
    QString action(const QString &id, const Target &target, QJsonObject &result);
    QString artAction(const QString &id, const QString &surface);
    QString ask(const QString &prompt, const Target &target, QJsonObject &result);
    QString send(const QString &destination, const Target &target, const QString &prompt, QJsonObject &result);
    QString captureToDesk(const Target &target, QJsonObject &result);
    // The surface as a frame: its screenshot, where it can be taken, with its art (or the selected part) over it.
    Desk::Frame frameFor(const Target &target, bool screenshot, QString *error);
    QString onboarding(const QJsonObject &params, QJsonObject &result);
    // Where each surface with art sits on screen now; web pages are read for their scroll.
    void updatePlacements();
    // The surface's screen rectangle now, if it's shown.
    std::optional<Surface> locate(const QString &key);
    // A screenshot of `rect` kept in the captures folder; empty when grim can't.
    QString keepScreenshot(const QRect &rect, QImage *image = nullptr);
    void say(const QString &line);
    // `mark`: the tab shows as saved (not while the window is being torn down).
    void autosaveDesk(bool mark = true);

    AgentBridge &m_bridge;
    ProjectWorkspace &m_workspace;
    QWidget &m_window;
    std::unique_ptr<DesktopSource> m_source;
    std::unique_ptr<DesignMode> m_mode;
    OverlayStore m_overlays;
    QTimer m_placementTimer;
    QJsonArray m_placements;
    QString m_message;
    bool m_onboardingOpen = false;
    std::optional<Inspection> m_detail;
    QTimer m_deskSave;
    QMetaObject::Connection m_deskWatch;
    bool m_started = false;
    // anywhere.json as last read or written.
    QJsonObject m_settings;
};
