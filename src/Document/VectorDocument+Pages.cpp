#include "Document/VectorDocument.h"
#include <algorithm>

QUuid VectorDocument::implicitPageId()
{
    // Fixed, like the implicit artboard's, so a page reference survives between calls.
    return QUuid(QStringLiteral("{5b0e6d1c-84f3-4a27-b9d2-3c7a91e0f5a4}"));
}

std::vector<Page> VectorDocument::allPages() const
{
    if (pages.empty())
        return {Page{implicitPageId(), QStringLiteral("Page 1")}};
    return pages;
}

QUuid VectorDocument::firstPageId() const
{
    return pages.empty() ? implicitPageId() : pages.front().id;
}

int VectorDocument::pageIndex(const QUuid &id) const
{
    if (pages.empty())
        return id == implicitPageId() ? 0 : -1;
    for (size_t index = 0; index < pages.size(); ++index) {
        if (pages[index].id == id)
            return int(index);
    }
    return -1;
}

QUuid VectorDocument::resolvePage(const QUuid &tag) const
{
    return !tag.isNull() && pageIndex(tag) >= 0 ? tag : firstPageId();
}

QUuid VectorDocument::currentPageId() const
{
    return resolvePage(currentPage);
}

QUuid VectorDocument::pageOf(const QUuid &id) const
{
    const std::optional<QUuid> layer = layerOf(id);
    const VectorObject *object = layer ? find(*layer) : nullptr;
    return object ? resolvePage(object->page) : QUuid();
}

bool VectorDocument::isOnCurrentPage(const QUuid &id) const
{
    // One page holds everything.
    if (pages.size() < 2)
        return true;
    return pageOf(id) == currentPageId();
}

std::vector<QUuid> VectorDocument::allLayers() const
{
    return children(std::nullopt);
}

std::vector<QUuid> VectorDocument::layersOn(const QUuid &page) const
{
    if (pages.size() < 2)
        return allLayers();
    const QUuid target = resolvePage(page);
    std::vector<QUuid> result;
    for (const VectorObject &object : objects) {
        if (!object.parentID && resolvePage(object.page) == target)
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> VectorDocument::layers() const
{
    return layersOn(currentPageId());
}

void VectorDocument::repairPageNames()
{
    for (size_t index = 0; index < pages.size(); ++index) {
        QString name = pages[index].name.simplified();
        if (name.isEmpty())
            name = QStringLiteral("Page %1").arg(index + 1);
        const QString base = name;
        const auto taken = [&](const QString &candidate) {
            return std::any_of(pages.begin(), pages.begin() + index, [&](const Page &other) { return other.name == candidate; });
        };
        for (int number = 2; taken(name); ++number)
            name = QStringLiteral("%1 %2").arg(base).arg(number);
        pages[index].name = name;
    }
}

QString VectorDocument::uniquePageName(const QString &base) const
{
    const std::vector<Page> all = allPages();
    const auto taken = [&](const QString &name) {
        return std::any_of(all.begin(), all.end(), [&](const Page &page) { return page.name == name; });
    };
    if (!taken(base))
        return base;
    for (int number = 2;; ++number) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(number);
        if (!taken(candidate))
            return candidate;
    }
}

void VectorDocument::ensurePages()
{
    const bool wasImplicit = pages.empty();
    if (wasImplicit)
        pages.push_back(Page{implicitPageId(), QStringLiteral("Page 1")});
    // Explicit artboards, so a second page has somewhere to keep its own.
    if (artboards.empty())
        artboards.push_back(implicitArtboard());
    for (VectorObject &object : objects) {
        if (!object.parentID)
            object.page = resolvePage(object.page);
    }
    for (Guide &guide : guides)
        guide.page = resolvePage(guide.page);
    for (Artboard &board : artboards)
        board.page = resolvePage(board.page);
    currentPage = resolvePage(currentPage);
    // Every page has a layer and an artboard.
    for (const Page &page : pages) {
        const bool hasLayer = std::any_of(objects.begin(), objects.end(), [&](const VectorObject &object) { return !object.parentID && object.page == page.id; });
        if (!hasLayer) {
            VectorObject layer = blank({1, 1}).objects.front();
            layer.page = page.id;
            objects.push_back(layer);
        }
        const bool hasBoard = std::any_of(artboards.begin(), artboards.end(), [&](const Artboard &board) { return board.page == page.id; });
        if (!hasBoard) {
            Artboard board;
            board.name = QStringLiteral("Artboard 1");
            board.rect = QRectF(QPointF(0, 0), artboards.front().rect.size());
            board.page = page.id;
            artboards.push_back(board);
        }
    }
}

std::vector<Guide> VectorDocument::guidesOnCurrentPage() const
{
    std::vector<Guide> result;
    for (const Guide &guide : guides) {
        if (isOnCurrentPage(guide))
            result.push_back(guide);
    }
    return result;
}

std::vector<VectorObject> VectorDocument::copyLayers(const std::vector<QUuid> &layerIds) const
{
    std::vector<QUuid> wanted;
    for (const QUuid &layer : layerIds) {
        wanted.push_back(layer);
        const std::vector<QUuid> nested = descendants(layer);
        wanted.insert(wanted.end(), nested.begin(), nested.end());
    }
    std::vector<VectorObject> copies;
    std::vector<std::pair<QUuid, QUuid>> renamed;
    for (const VectorObject &object : objects) {
        if (std::find(wanted.begin(), wanted.end(), object.id) == wanted.end())
            continue;
        copies.push_back(object);
        renamed.emplace_back(object.id, QUuid::createUuid());
        copies.back().id = renamed.back().second;
    }
    const auto fresh = [&](const QUuid &old) {
        const auto found = std::find_if(renamed.begin(), renamed.end(), [&](const auto &pair) { return pair.first == old; });
        return found == renamed.end() ? QUuid() : found->second;
    };
    for (size_t index = 0; index < copies.size(); ++index) {
        VectorObject &copy = copies[index];
        if (copy.parentID)
            copy.parentID = fresh(*copy.parentID);
        if (!copy.text.threadNext.isNull())
            copy.text.threadNext = fresh(copy.text.threadNext);
        if (copy.component) {
            copy.instance = InstanceInfo{renamed[index].first, copy.component->placement, {}, {}};
            copy.component.reset();
        }
    }
    return copies;
}
