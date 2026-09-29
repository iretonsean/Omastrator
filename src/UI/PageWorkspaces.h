#pragma once
#include "Agent/HyprlandEvents.h"
#include "UI/PageStandIn.h"
#include "UI/ProjectWorkspace.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
#include <vector>

// Pages as Hyprland named workspaces (docs/WORKSPACES.md): a document with two or more pages claims
// `design:<document> · <page>` for each, the editor sits on the current page's workspace and stand-ins
// keep the others alive. Off Hyprland, or with the setting off, it does nothing.
class PageWorkspaces : public QObject {
    Q_OBJECT
public:
    // `editor` is the window that moves between workspaces.
    PageWorkspaces(ProjectWorkspace &workspace, QWidget &editor);
    ~PageWorkspaces() override;

    static constexpr int maxClaims = 24;

    // Hyprland's names can't hold what a dispatcher splits on: `,` `"` `\`, control characters and runs of spaces go, and it's cut at 32.
    static QString label(const QString &part);
    static QString name(const QString &document, const QString &page);
    // View ▸ Pages as Workspaces: app-wide, off by default.
    static bool isTurnedOn();
    static void setTurnedOn(bool on);
    // Hyprland answers: false off Hyprland (the command greys out). A refusal is remembered for a few seconds.
    static bool hyprlandReachable();
    static void forgetReachability();

    // On, reachable and the editor's window shown.
    bool isActive() const;
    // Reconciles now instead of on the next turn of the event loop.
    void refresh();
    QStringList claimedNames() const;
    QString nameOf(const QUuid &tab, const QUuid &page) const;
    // Addresses (with 0x) of the stand-ins that have been placed; design mode never treats them as an app's home.
    QStringList standInAddresses() const;
    int standInCount() const;
    // Every stand-in of every window, for design mode.
    static QStringList allStandInAddresses();
    // Asks whether an Omastrator window has focus; tests answer it.
    void setFocusProbe(std::function<bool()> probe) { m_focusProbe = std::move(probe); }

private:
    struct Claim {
        QUuid tab;
        QUuid page;
        QString name;
        // What the name came from, so a change is a rename.
        QString document;
        QString pageName;
    };
    struct StandIn {
        QPointer<PageStandIn> widget;
        QString address;
        QString workspace;
    };

    void schedule();
    void reconcile();
    void watch(const std::shared_ptr<ProjectTab> &tab);
    QString stateKey(bool active) const;
    std::vector<Claim> wants(bool active, const QSet<QString> &foreign);
    QString documentLabel(const ProjectTab &tab, const std::vector<Claim> &kept);
    void place();
    void placeSoon();
    bool concernsUs(const HyprlandEvents::Event &event) const;
    void stopAfterRefusedMove();
    void workspaceEntered(const QString &name);
    void arrived();
    void createStandIn(const QString &workspace);
    void dropStandIn(StandIn &standIn);
    void standInClosed(PageStandIn *widget);
    QImage pictureFor(const QUuid &tab, const QUuid &page);
    void writeClaims() const;
    void forgetReturn();
    void startEvents();
    void hyprlandLeft();
    bool followsFocus() const;
    void notify(const QString &text);
    bool eventFilter(QObject *watched, QEvent *event) override;

    ProjectWorkspace &m_workspace;
    QWidget &m_editor;
    std::vector<Claim> m_claims;
    std::vector<StandIn> m_standIns;
    QString m_editorAddress;
    // The workspace focused when the first claim was made, where the user's windows go back to (id and name: a
    // numbered one is selected by number). The editor goes back to the one it was on, which may differ.
    int m_returnId = 0;
    QString m_returnName;
    int m_editorReturnId = 0;
    QString m_editorReturnName;
    // Names given back or renamed since the last placement: what their leftover windows follow.
    QHash<QString, QString> m_renamed;
    QSet<QString> m_released;
    // Pages whose stand-in the user closed stay unclaimed until they are shown again.
    QSet<QString> m_declined;
    QHash<QString, QImage> m_pictures;
    QSet<QUuid> m_watched;
    QString m_key;
    bool m_followNext = false;
    // Stand-ins are wanted but weren't made because no Omastrator window had focus.
    bool m_wantsStandIns = false;
    bool m_capNoticed = false;
    bool m_unreachableNoticed = false;
    bool m_lost = false;
    int m_nextStandIn = 1;
    int m_retries = 0;
    QTimer m_reconcileTimer;
    QTimer m_placeTimer;
    // Workspace events within 50 ms act once, on the last.
    QTimer m_arriveTimer;
    QString m_arrivedAt;
    bool m_fromHyprland = false;
    HyprlandEvents m_events;
    std::function<bool()> m_focusProbe;
};
