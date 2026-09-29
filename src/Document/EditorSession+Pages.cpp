#include "Document/EditorSession.h"
#include <algorithm>

// Pages (docs/PAGES.md section 3). Each operation is one named step; switching is not.

namespace {
QString trimmedName(const QString &name)
{
    return name.simplified();
}

// A layer for a page with nothing yet, the way a blank document starts.
VectorObject freshLayer(const QUuid &page)
{
    VectorObject layer = VectorDocument::blank({1, 1}).objects.front();
    layer.page = page;
    return layer;
}
}

QUuid EditorSession::currentPage() const
{
    return m_document ? m_document->currentPageId() : QUuid();
}

void EditorSession::rememberPageView()
{
    if (!m_document || m_shownPage.isNull())
        return;
    m_pageViews[m_shownPage] = PageView{viewport, m_activeArtboard, m_artboardSelected, m_selection};
}

bool EditorSession::enterPage()
{
    if (!m_document)
        return false;
    const QUuid page = m_document->currentPageId();
    if (page == m_shownPage)
        return false;
    m_shownPage = page;
    m_pickedNodes.clear();
    m_isolation.clear();
    m_keyObject.reset();
    m_textRange.reset();
    m_activeLayer.reset();
    const QSizeF size = m_document->viewSize();
    m_viewportDocumentSize = size;
    const auto remembered = m_pageViews.find(page);
    if (remembered != m_pageViews.end()) {
        CanvasViewport restored = remembered->second.viewport;
        restored.viewSize = viewport.viewSize;
        restored.backingScale = viewport.backingScale;
        viewport = restored;
        if (viewport.followsFit())
            viewport.fit(size);
        m_activeArtboard = remembered->second.activeArtboard;
        m_artboardSelected = remembered->second.artboardSelected;
        return true;
    }
    // A first visit fits the page's artboards, the first one active.
    m_activeArtboard = 0;
    m_artboardSelected = false;
    const QRectF bounds = m_document->artboardBounds();
    if (m_document->artboardCount() == 1 && bounds.topLeft() == QPointF(0, 0))
        viewport.fit(size);
    else if (!frameView(bounds))
        viewport.fit(size);
    return true;
}

QUuid EditorSession::addPage(const QString &name)
{
    if (!m_document)
        return {};
    rememberPageView();
    Page page;
    page.id = QUuid::createUuid();
    const QString wanted = trimmedName(name);
    page.name = m_document->uniquePageName(wanted.isEmpty() ? QStringLiteral("Page %1").arg(m_document->pageCount() + 1) : wanted);
    const QUuid id = page.id;
    edit(QStringLiteral("New Page"), [&](VectorDocument &document) {
        // The new page takes the current page's first artboard's size and paper.
        const Artboard first = document.artboard(0);
        document.ensurePages();
        const int at = document.pageIndex(document.currentPageId()) + 1;
        document.pages.insert(document.pages.begin() + at, page);
        document.appendLayer(freshLayer(page.id));
        Artboard board;
        board.name = QStringLiteral("Artboard 1");
        board.rect = QRectF(QPointF(0, 0), first.rect.size());
        board.background = first.background;
        board.page = page.id;
        document.artboards.push_back(board);
        document.currentPage = page.id;
        m_selection.clear();
    });
    return id;
}

QUuid EditorSession::duplicatePage(const QUuid &id)
{
    if (!m_document || m_document->pageIndex(id) < 0)
        return {};
    rememberPageView();
    const QUuid copyId = QUuid::createUuid();
    edit(QStringLiteral("Duplicate Page"), [&](VectorDocument &document) {
        document.ensurePages();
        const int index = document.pageIndex(id);
        Page copy;
        copy.id = copyId;
        copy.name = document.uniquePageName(QStringLiteral("%1 Copy").arg(document.pages[size_t(index)].name));
        std::vector<VectorObject> objects = document.copyLayers(document.layersOn(id));
        for (VectorObject &object : objects) {
            if (!object.parentID)
                object.page = copyId;
        }
        document.objects.insert(document.objects.end(), objects.begin(), objects.end());
        std::vector<Artboard> boards;
        for (const Artboard &board : document.artboards) {
            if (document.resolvePage(board.page) != id)
                continue;
            boards.push_back(board);
            boards.back().id = QUuid::createUuid();
            boards.back().page = copyId;
        }
        document.artboards.insert(document.artboards.end(), boards.begin(), boards.end());
        std::vector<Guide> guides;
        for (const Guide &guide : document.guides) {
            if (document.resolvePage(guide.page) != id)
                continue;
            guides.push_back(guide);
            guides.back().page = copyId;
        }
        document.guides.insert(document.guides.end(), guides.begin(), guides.end());
        document.pages.insert(document.pages.begin() + index + 1, copy);
        document.currentPage = copyId;
        m_selection.clear();
    });
    return copyId;
}

void EditorSession::renamePage(const QUuid &id, const QString &name)
{
    if (!m_document || m_document->pageIndex(id) < 0)
        return;
    const QString wanted = trimmedName(name);
    if (wanted.isEmpty())
        return;
    const std::vector<Page> pages = m_document->allPages();
    const auto taken = [&](const QString &candidate) {
        return std::any_of(pages.begin(), pages.end(), [&](const Page &page) { return page.id != id && page.name == candidate; });
    };
    QString result = wanted;
    for (int number = 2; taken(result); ++number)
        result = QStringLiteral("%1 %2").arg(wanted).arg(number);
    if (pages[size_t(m_document->pageIndex(id))].name == result)
        return;
    edit(QStringLiteral("Rename Page"), [&](VectorDocument &document) {
        document.ensurePages();
        document.pages[size_t(document.pageIndex(id))].name = result;
    });
}

bool EditorSession::deletePage(const QUuid &id)
{
    if (!m_document || m_document->pageCount() < 2 || m_document->pageIndex(id) < 0)
        return false;
    rememberPageView();
    const bool wasCurrent = m_document->currentPageId() == id;
    edit(QStringLiteral("Delete Page"), [&](VectorDocument &document) {
        document.remove(document.layersOn(id));
        std::erase_if(document.artboards, [&](const Artboard &board) { return document.resolvePage(board.page) == id; });
        std::erase_if(document.guides, [&](const Guide &guide) { return document.resolvePage(guide.page) == id; });
        std::erase_if(document.exportAssets, [&](const QUuid &asset) { return !document.find(asset); });
        const int index = document.pageIndex(id);
        document.pages.erase(document.pages.begin() + index);
        // The document's own size and paper follow its first artboard.
        if (!document.artboards.empty()) {
            document.size = document.artboards.front().rect.size();
            document.background = document.artboards.front().background;
        }
        if (!wasCurrent)
            return;
        // The next page, else the one before.
        const QUuid next = document.pages[size_t(std::min<int>(index, int(document.pages.size()) - 1))].id;
        document.currentPage = next;
        const auto remembered = m_pageViews.find(next);
        m_selection = remembered == m_pageViews.end() ? std::vector<QUuid>() : remembered->second.selection;
    });
    m_pageViews.erase(id);
    return true;
}

void EditorSession::movePage(const QUuid &id, int index)
{
    if (!m_document || m_document->pageCount() < 2)
        return;
    const int from = m_document->pageIndex(id);
    const int to = std::clamp(index, 0, m_document->pageCount() - 1);
    if (from < 0 || from == to)
        return;
    edit(QStringLiteral("Reorder Pages"), [&](VectorDocument &document) {
        const Page page = document.pages[size_t(from)];
        document.pages.erase(document.pages.begin() + from);
        document.pages.insert(document.pages.begin() + to, page);
    });
}

void EditorSession::moveSelectionToPage(const QUuid &id)
{
    if (!m_document || m_selection.empty() || m_document->pageIndex(id) < 0 || id == m_document->currentPageId())
        return;
    const std::vector<QUuid> selected = selectionInOrder();
    edit(QStringLiteral("Move to Page"), [&](VectorDocument &document) {
        // Layers can't be selected, so it's always objects: each goes to the target page's layer
        // named like its own, else that page's top open one, else a new "Layer 1".
        std::vector<QUuid> loose;
        for (const QUuid &object : selected) {
            const VectorObject *found = document.find(object);
            if (!found)
                continue;
            // Inside a selected group it goes with it.
            bool carried = false;
            for (std::optional<QUuid> up = found->parentID; up && !carried; up = document.find(*up)->parentID)
                carried = std::find(selected.begin(), selected.end(), *up) != selected.end();
            if (!carried)
                loose.push_back(object);
        }
        for (const QUuid &object : loose) {
            const std::optional<QUuid> from = document.layerOf(object);
            const QString wanted = from ? document.find(*from)->name : QString();
            std::optional<QUuid> target;
            std::optional<QUuid> open;
            for (const QUuid &layer : document.layersOn(id)) {
                const VectorObject *candidate = document.find(layer);
                if (candidate->name == wanted && !target)
                    target = layer;
                if (!candidate->isLocked)
                    open = layer;
            }
            if (!target)
                target = open;
            if (!target) {
                VectorObject layer = freshLayer(id);
                layer.name = QStringLiteral("Layer 1");
                target = layer.id;
                document.appendLayer(layer);
            }
            document.move(object, *target, -1);
        }
        m_selection.clear();
    });
    emit movedToPage(m_document->allPages()[size_t(m_document->pageIndex(id))].name);
}

void EditorSession::setCurrentPage(const QUuid &id)
{
    if (!m_document || m_document->pageIndex(id) < 0 || id == m_document->currentPageId())
        return;
    if (m_interaction)
        commitInteraction();
    rememberPageView();
    m_document->currentPage = id;
    const auto remembered = m_pageViews.find(id);
    m_selection = remembered == m_pageViews.end() ? std::vector<QUuid>() : remembered->second.selection;
    pruneSelection();
    notify(false);
}

void EditorSession::showPage(bool next)
{
    if (!m_document || m_document->pageCount() < 2)
        return;
    const int count = m_document->pageCount();
    const int index = m_document->pageIndex(m_document->currentPageId());
    setCurrentPage(m_document->allPages()[size_t(((index + (next ? 1 : -1)) % count + count) % count)].id);
}
