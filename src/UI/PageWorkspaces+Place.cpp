#include "Agent/Hyprland.h"
#include "Logging.h"
#include "Rendering/VectorRenderer.h"
#include "UI/PageWorkspaces.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <algorithm>

namespace {
const QString standInPrefix = QStringLiteral("omastrator-standin-");
// Where the spare stand-in waits: special workspaces are never shown unless toggled and don't count in Super+Tab.
const QString spareWorkspace = QStringLiteral("special:omastrator-spare");
constexpr int retryMs = 100;
constexpr int retryLimit = 20;
QString pictureKey(const QUuid &tab, const QUuid &page)
{
    return tab.toString() + page.toString();
}
}

void PageWorkspaces::placeSoon()
{
    if (!m_claims.empty() && !m_placeTimer.isActive())
        m_placeTimer.start();
}

void PageWorkspaces::createStandIn(const QString &workspace)
{
    auto *widget = new PageStandIn(m_nextStandIn++);
    connect(widget, &PageStandIn::closedByUser, this, &PageWorkspaces::standInClosed);
    m_standIns.push_back({widget, QString(), workspace});
    widget->show();
}

void PageWorkspaces::dropStandIn(StandIn &standIn)
{
    if (!standIn.widget)
        return;
    // Deleted, not closed: closing would look like the user did it.
    standIn.widget->disconnect(this);
    delete standIn.widget;
}

void PageWorkspaces::standInClosed(PageStandIn *widget)
{
    const auto found = std::find_if(m_standIns.begin(), m_standIns.end(), [widget](const StandIn &s) { return s.widget == widget; });
    if (found == m_standIns.end())
        return;
    const QString workspace = found->workspace;
    widget->disconnect(this);
    widget->deleteLater();
    m_standIns.erase(found);
    for (const Claim &claim : m_claims) {
        if (claim.name == workspace)
            m_declined.insert(pictureKey(claim.tab, claim.page));
    }
    schedule();
}

QImage PageWorkspaces::pictureFor(const QUuid &tab, const QUuid &page)
{
    const QString key = pictureKey(tab, page);
    if (m_pictures.contains(key))
        return m_pictures.value(key);
    const std::shared_ptr<ProjectTab> owner = m_workspace.tab(tab);
    if (!owner || !owner->session.hasDocument())
        return {};
    // The page as it stands, when it has never been on screen to grab.
    VectorDocument copy = *owner->session.document();
    copy.currentPage = page;
    const QImage probe = VectorRenderer::render(copy, 0.1, false);
    if (probe.isNull())
        return {};
    const double scale = std::min(1.0, 1024.0 / (std::max(probe.width(), probe.height()) * 10.0));
    const QImage picture = VectorRenderer::render(copy, scale, false);
    m_pictures.insert(key, picture);
    return picture;
}

void PageWorkspaces::place()
{
    QString error;
    const QJsonValue json = Hyprland::query(QStringLiteral("clients"), &error);
    if (!error.isEmpty())
        return;
    const std::vector<Hyprland::Window> clients = Hyprland::parseClients(json);
    const qint64 pid = QCoreApplication::applicationPid();
    auto find = [&](const QString &address) -> const Hyprland::Window * {
        if (address.isEmpty())
            return nullptr;
        const auto it = std::find_if(clients.begin(), clients.end(), [&](const Hyprland::Window &w) { return w.address == address; });
        return it == clients.end() ? nullptr : &*it;
    };
    const QString returnSelector = Hyprland::workspaceSelector(m_returnId, m_returnName);
    QHash<QString, int> occupancy;
    for (const Hyprland::Window &window : clients)
        ++occupancy[window.workspaceName];

    // Our windows: stand-ins by title until their address is known, the editor by its address, then its exact title.
    std::erase_if(m_standIns, [](const StandIn &s) { return s.widget.isNull(); });
    QSet<QString> ours;
    for (StandIn &standIn : m_standIns) {
        if (!find(standIn.address))
            standIn.address.clear();
        if (standIn.address.isEmpty()) {
            for (const Hyprland::Window &window : clients) {
                if (window.pid == pid && window.title == standIn.widget->firstTitle()) {
                    standIn.address = window.address;
                    break;
                }
            }
        }
        if (!standIn.address.isEmpty())
            ours.insert(standIn.address);
    }
    if (!find(m_editorAddress)) {
        m_editorAddress.clear();
        const QString title = m_editor.windowTitle().replace(QLatin1String("[*]"), m_editor.isWindowModified() ? QStringLiteral("*") : QString());
        std::vector<const Hyprland::Window *> candidates;
        for (const Hyprland::Window &window : clients) {
            if (window.pid == pid && !window.title.startsWith(standInPrefix) && !ours.contains(window.address))
                candidates.push_back(&window);
        }
        for (const Hyprland::Window *candidate : candidates) {
            if (candidate->title == title)
                m_editorAddress = candidate->address;
        }
        if (m_editorAddress.isEmpty() && candidates.size() == 1)
            m_editorAddress = candidates.front()->address;
    }
    ours.insert(m_editorAddress);
    const Hyprland::Window *editor = find(m_editorAddress);
    // Where the editor stands before it is first moved is where it goes back to; not a workspace of ours.
    if (editor && m_editorReturnName.isEmpty() && !m_claims.empty() && !editor->workspaceName.startsWith(QLatin1String("design:"))) {
        m_editorReturnName = editor->workspaceName;
        m_editorReturnId = editor->workspace;
    }

    const QString front = nameOf(m_workspace.selectedID(), m_workspace.current().session.currentPage());
    QString target = front;
    QString targetSelector = Hyprland::workspaceSelector(0, front);
    if (target.isEmpty() && editor && editor->workspaceName.startsWith(QLatin1String("design:"))) {
        const bool own = !m_editorReturnName.isEmpty();
        target = own ? m_editorReturnName : m_returnName;
        targetSelector = own ? Hyprland::workspaceSelector(m_editorReturnId, m_editorReturnName) : returnSelector;
    }
    QStringList needy;
    for (const Claim &claim : m_claims) {
        if (claim.name != front)
            needy << claim.name;
    }

    // Each needy workspace keeps the stand-in it holds. The free ones fill the rest, the parked spare first, and
    // one stays parked as the spare: a swap sends it to the old page's workspace before the editor leaves it, so
    // no workspace is ever empty (and deleted, with a new id) while its page is claimed.
    std::vector<StandIn> pool = std::move(m_standIns);
    std::vector<bool> taken(pool.size(), false);
    auto whereIs = [&](const StandIn &standIn) {
        const Hyprland::Window *window = find(standIn.address);
        return window ? window->workspaceName : standIn.workspace;
    };
    QHash<QString, int> holder;
    for (int i = 0; i < int(pool.size()); ++i) {
        const Hyprland::Window *window = find(pool[size_t(i)].address);
        const QString where = whereIs(pool[size_t(i)]);
        if ((window || pool[size_t(i)].address.isEmpty()) && needy.contains(where) && !holder.contains(where)) {
            holder.insert(where, i);
            taken[size_t(i)] = true;
        }
    }
    auto takeFree = [&]() {
        int pick = -1;
        for (int i = 0; i < int(pool.size()); ++i) {
            if (taken[size_t(i)])
                continue;
            if (pick < 0)
                pick = i;
            if (whereIs(pool[size_t(i)]) == spareWorkspace) {
                pick = i;
                break;
            }
        }
        if (pick >= 0)
            taken[size_t(pick)] = true;
        return pick;
    };
    // A new window maps on the focused workspace and takes focus, so one is made only while an Omastrator window has it.
    const bool mayCreate = followsFocus();
    m_wantsStandIns = false;
    m_standIns.clear();
    for (const QString &name : needy) {
        int i = holder.value(name, -1);
        if (i < 0)
            i = takeFree();
        if (i >= 0) {
            pool[size_t(i)].workspace = name;
            m_standIns.push_back(pool[size_t(i)]);
        } else if (mayCreate) {
            createStandIn(name);
        } else {
            m_wantsStandIns = true;
        }
    }
    if (!needy.isEmpty()) {
        if (const int i = takeFree(); i >= 0) {
            pool[size_t(i)].workspace = spareWorkspace;
            m_standIns.push_back(pool[size_t(i)]);
        } else if (mayCreate) {
            createStandIn(spareWorkspace);
        } else {
            m_wantsStandIns = true;
        }
    }
    for (int i = 0; i < int(pool.size()); ++i) {
        if (!taken[size_t(i)])
            dropStandIn(pool[size_t(i)]);
    }
    for (StandIn &standIn : m_standIns) {
        if (standIn.workspace.isEmpty() || !standIn.widget)
            continue;
        for (const Claim &claim : m_claims) {
            if (claim.name != standIn.workspace)
                continue;
            const QImage picture = pictureFor(claim.tab, claim.page);
            if (picture.cacheKey() != standIn.widget->picture().cacheKey())
                standIn.widget->setPicture(picture);
            // Only once the address is known: the first title is how the window is found until then.
            if (!standIn.address.isEmpty())
                standIn.widget->setLabel(claim.name.mid(int(QStringLiteral("design:").size())));
        }
        if (standIn.workspace == spareWorkspace && !standIn.address.isEmpty())
            standIn.widget->setLabel(QStringLiteral("Spare"));
    }

    // What has to move, in an order that doesn't empty a workspace before its next window arrives.
    struct Move {
        QString address;
        QString workspace;
        QString selector;
        QString from;
        bool editor = false;
    };
    std::vector<Move> moves;
    for (const StandIn &standIn : m_standIns) {
        const Hyprland::Window *window = find(standIn.address);
        if (window && window->workspaceName != standIn.workspace)
            moves.push_back({standIn.address, standIn.workspace, Hyprland::workspaceSelector(0, standIn.workspace), window->workspaceName, false});
    }
    if (editor && !target.isEmpty() && editor->workspaceName != target)
        moves.push_back({editor->address, target, targetSelector, editor->workspaceName, true});
    const QStringList claimed = claimedNames();
    for (const Hyprland::Window &window : clients) {
        if (ours.contains(window.address) || window.pid == pid)
            continue;
        if (m_renamed.contains(window.workspaceName) && claimed.contains(m_renamed.value(window.workspaceName)))
            moves.push_back({window.address, m_renamed.value(window.workspaceName), Hyprland::workspaceSelector(0, m_renamed.value(window.workspaceName)), window.workspaceName, false});
        else if (window.workspaceName.startsWith(QLatin1String("design:")) && !claimed.contains(window.workspaceName) && !m_returnName.isEmpty())
            moves.push_back({window.address, m_returnName, returnSelector, window.workspaceName, false});
    }
    while (!moves.empty()) {
        size_t pick = 0;
        for (size_t i = 0; i < moves.size(); ++i) {
            if (occupancy.value(moves[i].from) > 1 || !claimed.contains(moves[i].from)) {
                pick = i;
                break;
            }
        }
        const Move move = moves[pick];
        moves.erase(moves.begin() + long(pick));
        const bool follow = move.editor && m_followNext && followsFocus();
        const QString failure = Hyprland::moveWindow(move.address, move.selector, follow);
        if (!failure.isEmpty())
            qCDebug(lcApp).noquote() << "Pages as Workspaces: move failed:" << failure;
        --occupancy[move.from];
        ++occupancy[move.workspace];
    }
    // Following is for the switch that asked for it, never for a later one.
    m_followNext = false;

    // Someone standing on a workspace we gave back goes where their windows went.
    if (!m_released.isEmpty() && !m_returnName.isEmpty()) {
        const QJsonValue active = Hyprland::query(QStringLiteral("activeworkspace"), &error);
        const QString name = active.toObject()["name"].toString();
        if (m_released.contains(name) && !claimed.contains(name))
            Hyprland::focusWorkspace(returnSelector);
    }
    m_released.clear();
    m_renamed.clear();

    // A window Wayland hasn't mapped yet has no address: look again shortly.
    const bool waiting = std::any_of(m_standIns.begin(), m_standIns.end(), [](const StandIn &s) { return s.address.isEmpty(); })
                         || (m_editor.isVisible() && m_editorAddress.isEmpty() && !m_claims.empty());
    if (waiting && m_retries < retryLimit) {
        ++m_retries;
        m_placeTimer.start(retryMs);
    } else if (!waiting) {
        m_retries = 0;
    }
    writeClaims();
}
