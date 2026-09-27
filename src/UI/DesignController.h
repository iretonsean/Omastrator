#pragma once
#include "Anywhere/DesignMode.h"
#include "Anywhere/Desk.h"
#include "Anywhere/Lift.h"
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
    // The lift under way, if any.
    LiftJob *liftJob() const { return m_lift.get(); }

    // A site that isn't yours: the page lifted without its edits, then with them, as two frames on the Desk in one
    // undo step. Returns why it couldn't start, or empty; it lands in the background.
    QString beforeAfter(QJsonObject &result);
    // Hand to Agent with the page in Omastrator's browser: its edits, art and screenshots. Without `folder` in
    // `params`, the Hand to Agent sheet asks (offering the folder used last for this site).
    QString handOffPage(const QJsonObject &params, QJsonObject &result);
    // lifted.json beside the overlays: each lifted object as it landed, for Apply to Source.
    QString liftedPath() const;

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
    // Hand to Agent… from any surface: its art, what it was lifted from, a page's edits and a screenshot.
    QString handOff(const Target &target, const QJsonObject &params, QJsonObject &result);
    // Apply to Source for lifted art on your own page: its changes become page edits, then Live's write-back.
    // Returns why it couldn't, or empty; `applied` is false when there were no lifted changes to apply.
    QString applyLifted(const QString &key, const std::vector<QUuid> &roots, bool *applied, QJsonObject &result);
    void beforeAfterLifted();
    QString captureToDesk(const Target &target, QJsonObject &result);
    // The surface as a frame: its screenshot, where it can be taken, with its art (or the selected part) over it.
    Desk::Frame frameFor(const Target &target, bool screenshot, QString *error);
    QString onboarding(const QJsonObject &params, QJsonObject &result);
    // Lift into vectors: what's pointed at (or `params.region` on screen) becomes shapes where the user chose.
    QString startLift(const QJsonObject &params, QJsonObject &result);
    void landLift();
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
    std::unique_ptr<LiftJob> m_lift;
    Surface m_liftSurface;
    QString m_liftDestination;
    // Before and After: the page's label, and the lift without its edits once it's done.
    QString m_beforeAfter;
    std::optional<Lift::Result> m_before;
};
