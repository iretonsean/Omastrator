#include "Document/EditorSession.h"
#include <QSettings>
#include <algorithm>

namespace {
const QString historyLimitKey = QStringLiteral("historyLimit");
// Illustrator keeps 100 states by default.
constexpr int defaultHistoryLimit = 100;
int cachedHistoryLimit = 0;
}

// Isolation --------------------------------------------------------------------

std::optional<QUuid> EditorSession::isolatedGroup() const
{
    if (m_isolation.empty())
        return std::nullopt;
    return m_isolation.back();
}

void EditorSession::isolate(const QUuid &group)
{
    const VectorObject *object = m_document ? m_document->find(group) : nullptr;
    if (!object || object->kind != ObjectKind::group)
        return;
    // Every group from the layer down to this one, so the breadcrumb can step back through them.
    std::vector<QUuid> chain;
    for (std::optional<QUuid> at = group; at; at = m_document->find(*at)->parentID) {
        if (m_document->find(*at)->kind == ObjectKind::group)
            chain.insert(chain.begin(), *at);
    }
    m_isolation = std::move(chain);
    if (m_interaction)
        commitInteraction();
    m_pickedNodes.clear();
    m_selection.clear();
    m_keyObject.reset();
    notify(false);
}

void EditorSession::exitIsolation(int depth)
{
    depth = std::max(0, depth);
    if (int(m_isolation.size()) <= depth)
        return;
    const QUuid left = m_isolation[size_t(depth)];
    m_isolation.resize(size_t(depth));
    m_pickedNodes.clear();
    m_keyObject.reset();
    m_selection.clear();
    if (m_document && m_document->find(left))
        m_selection = {left};
    notify(false);
}

// History ------------------------------------------------------------------------

void EditorSession::stepHistory(int steps)
{
    for (; steps < 0 && canUndo(); ++steps)
        undo();
    for (; steps > 0 && canRedo(); --steps)
        redo();
}

int EditorSession::historyLimit()
{
    if (cachedHistoryLimit <= 0)
        cachedHistoryLimit = std::clamp(QSettings().value(historyLimitKey, defaultHistoryLimit).toInt(), 1, 1000);
    return cachedHistoryLimit;
}

void EditorSession::reloadHistoryLimit()
{
    cachedHistoryLimit = 0;
}

void EditorSession::setHistoryLimit(int steps)
{
    cachedHistoryLimit = std::clamp(steps, 1, 1000);
    QSettings().setValue(historyLimitKey, cachedHistoryLimit);
}

// View toggles --------------------------------------------------------------------

void EditorSession::setSnapsToPixel(bool snaps)
{
    snapsToPixel = snaps;
    notify(false);
}

void EditorSession::setShowsPixelGrid(bool shown)
{
    showsPixelGrid = shown;
    notify(false);
}

void EditorSession::setShowsRulers(bool shown)
{
    showsRulers = shown;
    notify(false);
}

void EditorSession::setShowsGuides(bool shown)
{
    showsGuides = shown;
    notify(false);
}

void EditorSession::setGuidesLocked(bool locked)
{
    guidesLocked = locked;
    notify(false);
}

// Guides ------------------------------------------------------------------------------

void EditorSession::addGuide(const Guide &guide)
{
    edit(QStringLiteral("Add Guide"), [&](VectorDocument &document) {
        document.guides.push_back(guide);
        if (!document.pages.empty() && guide.page.isNull())
            document.guides.back().page = document.currentPageId();
    });
}

void EditorSession::moveGuide(int index, double position)
{
    if (!m_document || index < 0 || index >= int(m_document->guides.size()) || m_document->guides[size_t(index)].position == position)
        return;
    edit(QStringLiteral("Move Guide"), [&](VectorDocument &document) { document.guides[size_t(index)].position = position; });
}

void EditorSession::removeGuide(int index)
{
    if (!m_document || index < 0 || index >= int(m_document->guides.size()))
        return;
    edit(QStringLiteral("Delete Guide"), [&](VectorDocument &document) { document.guides.erase(document.guides.begin() + index); });
}

void EditorSession::clearGuides()
{
    if (!m_document || m_document->guidesOnCurrentPage().empty())
        return;
    edit(QStringLiteral("Clear Guides"), [&](VectorDocument &document) {
        std::erase_if(document.guides, [&](const Guide &guide) { return document.isOnCurrentPage(guide); });
    });
}

bool EditorSession::canMakeGuides() const
{
    const std::vector<QUuid> leaves = selectedLeaves();
    return std::any_of(leaves.begin(), leaves.end(), [&](const QUuid &id) {
        return m_document->find(id)->kind == ObjectKind::path && !m_document->isEffectivelyLocked(id);
    });
}

void EditorSession::makeGuides()
{
    if (!m_document)
        return;
    std::vector<QUuid> paths;
    for (const QUuid &id : selectedLeaves()) {
        if (m_document->find(id)->kind == ObjectKind::path && !m_document->isEffectivelyLocked(id))
            paths.push_back(id);
    }
    if (paths.empty())
        return;
    edit(QStringLiteral("Make Guides"), [&](VectorDocument &document) {
        const auto addGuide = [&](Qt::Orientation orientation, double position) {
            document.guides.push_back({orientation, position, document.pages.empty() ? QUuid() : document.currentPageId()});
        };
        for (const QUuid &id : paths) {
            const VectorPath &path = document.find(id)->path;
            const QRectF bounds = path.bounds();
            // A straight line across one axis is that one guide; anything else gives its box's four.
            const bool line = path.nodeCount() == 2 && path.contours.size() == 1 && !path.contours.front().closed;
            if (line && std::abs(bounds.height()) < 1e-6) {
                addGuide(Qt::Horizontal, bounds.top());
            } else if (line && std::abs(bounds.width()) < 1e-6) {
                addGuide(Qt::Vertical, bounds.left());
            } else {
                addGuide(Qt::Vertical, bounds.left());
                addGuide(Qt::Vertical, bounds.right());
                addGuide(Qt::Horizontal, bounds.top());
                addGuide(Qt::Horizontal, bounds.bottom());
            }
        }
        document.remove(paths);
        m_selection.clear();
    });
}

void EditorSession::releaseGuides()
{
    if (!m_document || m_document->guidesOnCurrentPage().empty())
        return;
    edit(QStringLiteral("Release Guides"), [&](VectorDocument &document) {
        std::vector<QUuid> made;
        const std::vector<Guide> guides = document.guidesOnCurrentPage();
        std::erase_if(document.guides, [&](const Guide &guide) { return document.isOnCurrentPage(guide); });
        const QRectF extent = paperRect();
        m_selection.clear();
        for (const Guide &guide : guides) {
            const QPointF from = guide.orientation == Qt::Horizontal ? QPointF(extent.left(), guide.position) : QPointF(guide.position, extent.top());
            const QPointF to = guide.orientation == Qt::Horizontal ? QPointF(extent.right(), guide.position)
                                                                   : QPointF(guide.position, extent.bottom());
            VectorObject object = pathObject(Shapes::line(from, to), QStringLiteral("Guide"));
            object.fill = Paint::none();
            made.push_back(object.id);
            insertNew(document, std::move(object));
        }
        m_selection = made;
    });
}
