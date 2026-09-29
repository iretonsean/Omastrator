#include "UI/PageWorkspaces.h"
#include "Agent/Hyprland.h"
#include "Agent/WorkspaceClaims.h"
#include "Logging.h"
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonObject>
#include <QSettings>
#include <algorithm>

namespace {
const QString turnedOnKey = QStringLiteral("view/pageWorkspaces");
constexpr int partLimit = 32;
constexpr int refusalMs = 10000;

QList<PageWorkspaces *> &instances()
{
    static QList<PageWorkspaces *> all;
    return all;
}

QString declineKey(const QUuid &tab, const QUuid &page)
{
    return tab.toString() + page.toString();
}

QString documentBase(const ProjectTab &tab)
{
    return tab.title();
}
}

PageWorkspaces::PageWorkspaces(ProjectWorkspace &workspace, QWidget &editor) : QObject(&editor), m_workspace(workspace), m_editor(editor)
{
    m_focusProbe = [] { return QApplication::activeWindow() != nullptr; };
    instances().append(this);
    // A burst of edits reconciles once, on the next turn.
    m_reconcileTimer.setSingleShot(true);
    m_reconcileTimer.setInterval(0);
    connect(&m_reconcileTimer, &QTimer::timeout, this, &PageWorkspaces::reconcile);
    // Hyprland's own events for our windows come in bursts: place once they settle.
    m_placeTimer.setSingleShot(true);
    m_placeTimer.setInterval(50);
    connect(&m_placeTimer, &QTimer::timeout, this, &PageWorkspaces::place);
    m_arriveTimer.setSingleShot(true);
    m_arriveTimer.setInterval(50);
    connect(&m_arriveTimer, &QTimer::timeout, this, &PageWorkspaces::arrived);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &PageWorkspaces::schedule);
    connect(&m_events, &HyprlandEvents::closed, this, &PageWorkspaces::hyprlandLeft);
    connect(&m_events, &HyprlandEvents::event, this, [this](const HyprlandEvents::Event &event) {
        using Kind = HyprlandEvents::Event::Kind;
        if (event.kind == Kind::openWindow || event.kind == Kind::closeWindow || event.kind == Kind::moveWindow)
            placeSoon();
        else if (event.kind == Kind::workspace)
            workspaceEntered(event.workspaceName);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        m_lost = false;
        m_key.clear();
        // Handing everything back is what quitting means.
        setProperty("quitting", true);
        reconcile();
    });
    m_editor.installEventFilter(this);
    schedule();
}

PageWorkspaces::~PageWorkspaces()
{
    instances().removeAll(this);
    setProperty("quitting", true);
    m_key.clear();
    reconcile();
}

QString PageWorkspaces::label(const QString &part)
{
    QString clean;
    for (const QChar c : part) {
        if (c == QLatin1Char(',') || c == QLatin1Char('"') || c == QLatin1Char('\\'))
            continue;
        clean += c.isSpace() || c.category() == QChar::Other_Control ? QLatin1Char(' ') : c;
    }
    clean = clean.simplified();
    if (clean.size() > partLimit)
        clean = clean.left(partLimit - 1).trimmed() + QChar(0x2026);
    return clean;
}

QString PageWorkspaces::name(const QString &document, const QString &page)
{
    const QString doc = label(document), pageName = label(page);
    return QStringLiteral("design:%1 · %2").arg(doc.isEmpty() ? QStringLiteral("Untitled") : doc, pageName.isEmpty() ? QStringLiteral("Page") : pageName);
}

bool PageWorkspaces::isTurnedOn()
{
    return QSettings().value(turnedOnKey, false).toBool();
}

void PageWorkspaces::setTurnedOn(bool on)
{
    QSettings().setValue(turnedOnKey, on);
    for (PageWorkspaces *instance : instances()) {
        instance->m_unreachableNoticed = false;
        instance->m_lost = false;
        instance->refresh();
    }
}

namespace {
struct Reach {
    bool ok = false;
    bool known = false;
    QElapsedTimer age;
} reach;
}

bool PageWorkspaces::hyprlandReachable()
{
    if (qEnvironmentVariableIsEmpty("OMASTRATOR_HYPRCTL") && qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return false;
    if (reach.known && (reach.ok || !reach.age.hasExpired(refusalMs)))
        return reach.ok;
    QString error;
    Hyprland::query(QStringLiteral("workspaces"), &error);
    reach.ok = error.isEmpty();
    reach.known = true;
    reach.age.start();
    return reach.ok;
}

void PageWorkspaces::forgetReachability()
{
    reach.known = false;
}

bool PageWorkspaces::isActive() const
{
    return isTurnedOn() && !m_lost && m_editor.isVisible() && !property("quitting").toBool() && hyprlandReachable();
}

void PageWorkspaces::refresh()
{
    m_key.clear();
    m_reconcileTimer.start();
}

void PageWorkspaces::schedule()
{
    if (!m_reconcileTimer.isActive())
        m_reconcileTimer.start();
}

bool PageWorkspaces::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == &m_editor && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        refresh();
    return QObject::eventFilter(watched, event);
}

QStringList PageWorkspaces::claimedNames() const
{
    QStringList names;
    for (const Claim &claim : m_claims)
        names << claim.name;
    return names;
}

QString PageWorkspaces::nameOf(const QUuid &tab, const QUuid &page) const
{
    for (const Claim &claim : m_claims) {
        if (claim.tab == tab && claim.page == page)
            return claim.name;
    }
    return {};
}

QStringList PageWorkspaces::standInAddresses() const
{
    QStringList addresses;
    for (const StandIn &standIn : m_standIns) {
        if (!standIn.address.isEmpty())
            addresses << standIn.address;
    }
    return addresses;
}

QStringList PageWorkspaces::allStandInAddresses()
{
    QStringList addresses;
    for (const PageWorkspaces *instance : instances())
        addresses << instance->standInAddresses();
    return addresses;
}

int PageWorkspaces::standInCount() const
{
    return int(std::count_if(m_standIns.begin(), m_standIns.end(), [](const StandIn &standIn) { return !standIn.widget.isNull(); }));
}

bool PageWorkspaces::followsFocus() const
{
    return m_focusProbe && m_focusProbe();
}

void PageWorkspaces::notify(const QString &text)
{
    m_workspace.showNotice(text);
}

void PageWorkspaces::watch(const std::shared_ptr<ProjectTab> &tab)
{
    if (m_watched.contains(tab->id))
        return;
    m_watched.insert(tab->id);
    const QUuid id = tab->id;
    EditorSession *session = &tab->session;
    connect(session, &EditorSession::changed, this, &PageWorkspaces::schedule);
    // Leaving a page: its stand-in will show the editor as it was.
    connect(session, &EditorSession::aboutToChangePage, this, [this, id, session] {
        if (m_claims.empty() || m_workspace.selectedID() != id || !m_editor.isVisible())
            return;
        QImage grab = m_editor.grab().toImage();
        if (grab.width() > 1280)
            grab = grab.scaledToWidth(1280, Qt::SmoothTransformation);
        m_pictures.insert(declineKey(id, session->currentPage()), grab);
    });
    connect(session, &EditorSession::currentPageChanged, this, [this, id](const QUuid &page) {
        // Going to a page whose stand-in was closed reclaims it.
        m_declined.remove(declineKey(id, page));
        // A page the user walked to is already on screen: nothing follows them, focus is handled on arrival.
        m_followNext = !m_fromHyprland && followsFocus();
        schedule();
    });
}

// The document part of a name: its own, with " (2)" while another open document has it.
QString PageWorkspaces::documentLabel(const ProjectTab &tab, const std::vector<Claim> &kept)
{
    const QString base = label(documentBase(tab));
    const QString plain = base.isEmpty() ? QStringLiteral("Untitled") : base;
    auto taken = [&](const QString &candidate) {
        return std::any_of(kept.begin(), kept.end(), [&](const Claim &claim) { return claim.tab != tab.id && claim.document == candidate; });
    };
    // The suffix a claimed tab already carries stays with it.
    for (const Claim &claim : m_claims) {
        if (claim.tab == tab.id && claim.document.startsWith(plain) && (claim.document == plain || claim.document.startsWith(plain + QStringLiteral(" ("))) && !taken(claim.document))
            return claim.document;
    }
    QString candidate = plain;
    for (int number = 2; taken(candidate); ++number)
        candidate = QStringLiteral("%1 (%2)").arg(plain).arg(number);
    return candidate;
}

std::vector<PageWorkspaces::Claim> PageWorkspaces::wants(bool active, const QSet<QString> &foreign)
{
    std::vector<Claim> result;
    if (!active)
        return result;
    const std::shared_ptr<ProjectTab> front = m_workspace.tab(m_workspace.selectedID());
    // Tabs already claimed first, so a new one can't take a suffix from them.
    std::vector<std::shared_ptr<ProjectTab>> ordered;
    for (const Claim &claim : m_claims) {
        const std::shared_ptr<ProjectTab> tab = m_workspace.tab(claim.tab);
        if (tab && std::find(ordered.begin(), ordered.end(), tab) == ordered.end())
            ordered.push_back(tab);
    }
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        if (std::find(ordered.begin(), ordered.end(), tab) == ordered.end())
            ordered.push_back(tab);
    }
    bool capped = false;
    // The editor's page always has a workspace, so it holds a place under the cap.
    bool frontPending = front && front->session.hasDocument() && front->session.document()->allPages().size() >= 2;
    for (const std::shared_ptr<ProjectTab> &tab : ordered) {
        if (!tab->session.hasDocument())
            continue;
        const std::vector<Page> pages = tab->session.document()->allPages();
        if (pages.size() < 2)
            continue;
        const QString document = documentLabel(*tab, result);
        QStringList used;
        for (const Page &page : pages) {
            if (m_declined.contains(declineKey(tab->id, page.id)) && !(tab == front && page.id == tab->session.currentPage()))
                continue;
            const bool editorsPage = tab == front && page.id == tab->session.currentPage();
            if (editorsPage)
                frontPending = false;
            else if (int(result.size()) + (frontPending ? 1 : 0) >= maxClaims) {
                capped = true;
                continue;
            }
            const QString existing = nameOf(tab->id, page.id);
            QString part = label(page.name);
            if (part.isEmpty())
                part = QStringLiteral("Page");
            QString candidate = part;
            auto full = [&](const QString &p) { return QStringLiteral("design:%1 · %2").arg(document, p); };
            auto clashes = [&](const QString &p) {
                // A workspace of that name that holds windows we didn't put there is someone else's.
                return used.contains(p) || (foreign.contains(full(p)) && full(p) != existing);
            };
            for (int number = 2; clashes(candidate); ++number)
                candidate = QStringLiteral("%1 (%2)").arg(part).arg(number);
            used << candidate;
            result.push_back({tab->id, page.id, full(candidate), document, page.name});
        }
    }
    if (capped && !m_capNoticed) {
        m_capNoticed = true;
        notify(QStringLiteral("Only %1 pages get workspaces; the rest are in the Pages list.").arg(maxClaims));
    }
    if (!capped)
        m_capNoticed = false;
    return result;
}

QString PageWorkspaces::stateKey(bool active) const
{
    QString key = active ? QStringLiteral("on") : QStringLiteral("off");
    key += m_workspace.selectedID().toString();
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs()) {
        if (!tab->session.hasDocument())
            continue;
        key += QLatin1Char('|') + tab->id.toString() + documentBase(*tab) + tab->session.currentPage().toString();
        for (const Page &page : tab->session.document()->allPages())
            key += page.id.toString() + page.name + QLatin1Char('/');
    }
    return key + QString::number(m_declined.size()) + QString::number(m_standIns.size());
}

void PageWorkspaces::reconcile()
{
    for (const std::shared_ptr<ProjectTab> &tab : m_workspace.tabs())
        watch(tab);
    const bool turnedOn = isTurnedOn() && !property("quitting").toBool();
    const bool active = isActive();
    if (turnedOn && m_editor.isVisible() && !m_lost && !active && !m_unreachableNoticed) {
        m_unreachableNoticed = true;
        notify(QStringLiteral("Pages as Workspaces needs Hyprland."));
    }
    const QString key = stateKey(active);
    if (key == m_key)
        return;
    m_key = key;
    // The front tab's current page is never left declined: it has the editor.
    QSet<QString> foreign;
    if (active) {
        QString error;
        const std::vector<Hyprland::Window> windows = Hyprland::parseClients(Hyprland::query(QStringLiteral("clients"), &error));
        if (!error.isEmpty()) {
            m_key.clear();
            return;
        }
        QSet<QString> ours;
        for (const StandIn &standIn : m_standIns)
            ours.insert(standIn.address);
        ours.insert(m_editorAddress);
        for (const Hyprland::Window &window : windows) {
            if (window.pid != QCoreApplication::applicationPid() && !ours.contains(window.address))
                foreign.insert(window.workspaceName);
        }
    }
    const std::vector<Claim> next = wants(active, foreign);
    const bool wasClaiming = !m_claims.empty();
    if (!wasClaiming && next.empty())
        return;
    if (!wasClaiming) {
        QString error;
        const QJsonValue current = Hyprland::query(QStringLiteral("activeworkspace"), &error);
        m_return = current.toObject()["name"].toString();
        if (m_return.isEmpty()) {
            m_key.clear();
            return;
        }
    }
    for (const Claim &old : m_claims) {
        const auto same = std::find_if(next.begin(), next.end(), [&](const Claim &c) { return c.tab == old.tab && c.page == old.page; });
        if (same == next.end())
            m_released.insert(old.name);
        else if (same->name != old.name)
            m_renamed.insert(old.name, same->name);
    }
    m_claims = next;
    m_followNext = m_followNext || (!m_fromHyprland && followsFocus());
    if (m_claims.empty())
        m_declined.clear();
    place();
    if (m_claims.empty()) {
        m_events.stop();
        m_pictures.clear();
        m_return.clear();
    } else {
        startEvents();
    }
    writeClaims();
}

void PageWorkspaces::startEvents()
{
    if (m_events.isRunning())
        return;
    const QString failure = m_events.start();
    if (!failure.isEmpty())
        qCDebug(lcApp).noquote() << "Pages as Workspaces: no event stream:" << failure;
}

void PageWorkspaces::hyprlandLeft()
{
    // Hyprland went away: nothing to move any more, so drop our windows and the file and carry on as a plain app.
    m_lost = true;
    m_claims.clear();
    for (StandIn &standIn : m_standIns)
        dropStandIn(standIn);
    m_standIns.clear();
    m_declined.clear();
    m_key.clear();
    writeClaims();
}

void PageWorkspaces::writeClaims() const
{
    WorkspaceClaims::State state;
    if (!m_claims.empty()) {
        state.signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
        state.pid = QCoreApplication::applicationPid();
        state.returnWorkspace = m_return;
        for (const Claim &claim : m_claims) {
            WorkspaceClaims::Claim entry{claim.name, claim.tab.toString(QUuid::WithoutBraces), claim.page.toString(QUuid::WithoutBraces), {}};
            for (const StandIn &standIn : m_standIns) {
                if (!standIn.address.isEmpty() && standIn.workspace == claim.name)
                    entry.windows << standIn.address;
            }
            if (nameOf(m_workspace.selectedID(), m_workspace.current().session.currentPage()) == claim.name && !m_editorAddress.isEmpty())
                entry.windows << m_editorAddress;
            state.claims.append(entry);
        }
    }
    WorkspaceClaims::write(state);
}
