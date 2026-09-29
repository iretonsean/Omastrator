#include "Document/VectorDocument.h"
#include <algorithm>

Artboard VectorDocument::implicitArtboard() const
{
    Artboard board;
    // A fixed id (not a fresh random one each call, and not the null id `QUuid()` some
    // code uses for "no artboard"), so code that looks it up between calls (an
    // interactive drag, for one) keeps finding the same artboard.
    board.id = QUuid(QStringLiteral("{a64944e6-2b31-4a97-8a97-1b3f6e4c9c01}"));
    board.name = QStringLiteral("Artboard 1");
    board.rect = QRectF(QPointF(0, 0), size);
    board.background = background;
    return board;
}

std::vector<Artboard> VectorDocument::allArtboards() const
{
    return artboardsOn(currentPageId());
}

std::vector<Artboard> VectorDocument::artboardsOn(const QUuid &page) const
{
    const QUuid target = resolvePage(page);
    if (artboards.empty()) {
        if (target != firstPageId())
            return {};
        return {implicitArtboard()};
    }
    std::vector<Artboard> boards = artboards;
    // The first artboard's size and paper always follow `size`/`background`.
    boards[0].rect.setSize(size);
    boards[0].background = background;
    if (!pages.empty())
        std::erase_if(boards, [&](const Artboard &board) { return resolvePage(board.page) != target; });
    if (boards.empty()) {
        // A page keeps at least one artboard, even in a document built by hand.
        Artboard board;
        board.id = QUuid::createUuidV5(target, QByteArrayLiteral("artboard"));
        board.name = QStringLiteral("Artboard 1");
        board.rect = QRectF(QPointF(0, 0), size);
        board.page = target;
        boards.push_back(board);
    }
    return boards;
}

int VectorDocument::artboardCount() const
{
    if (pages.empty())
        return artboards.empty() ? 1 : int(artboards.size());
    return int(allArtboards().size());
}

Artboard VectorDocument::artboard(int index) const
{
    const std::vector<Artboard> boards = allArtboards();
    if (boards.empty())
        return {};
    return boards[size_t(std::clamp(index, 0, int(boards.size()) - 1))];
}

void VectorDocument::setArtboards(std::vector<Artboard> boards)
{
    if (pages.empty()) {
        if (boards.empty()) {
            artboards.clear();
            return;
        }
        size = boards[0].rect.size();
        background = boards[0].background;
        artboards = std::move(boards);
        return;
    }
    if (boards.empty())
        return;
    // Only the current page's artboards change: they take the slots the old ones held.
    const QUuid current = currentPageId();
    for (Artboard &board : boards)
        board.page = current;
    std::vector<bool> isSlot(artboards.size(), false);
    size_t lastSlot = 0;
    bool anySlot = false;
    for (size_t index = 0; index < artboards.size(); ++index) {
        if (resolvePage(artboards[index].page) == current) {
            isSlot[index] = true;
            lastSlot = index;
            anySlot = true;
        }
    }
    std::vector<Artboard> merged;
    size_t taken = 0;
    for (size_t index = 0; index < artboards.size(); ++index) {
        if (!isSlot[index]) {
            merged.push_back(artboards[index]);
            continue;
        }
        if (taken < boards.size())
            merged.push_back(boards[taken++]);
        // More boards than slots: the extras follow the page's last one.
        if (anySlot && index == lastSlot) {
            while (taken < boards.size())
                merged.push_back(boards[taken++]);
        }
    }
    while (taken < boards.size())
        merged.push_back(boards[taken++]);
    artboards = std::move(merged);
    size = artboards[0].rect.size();
    background = artboards[0].background;
}

int VectorDocument::artboardAt(QPointF point) const
{
    const std::vector<Artboard> boards = allArtboards();
    for (int index = int(boards.size()) - 1; index >= 0; --index) {
        if (boards[size_t(index)].rect.contains(point))
            return index;
    }
    return -1;
}

int VectorDocument::artboardIndex(const QUuid &id) const
{
    const std::vector<Artboard> boards = allArtboards();
    for (int index = 0; index < int(boards.size()); ++index) {
        if (boards[size_t(index)].id == id)
            return index;
    }
    return -1;
}

QRectF VectorDocument::artboardBounds() const
{
    QRectF result;
    for (const Artboard &board : allArtboards())
        result = result.isNull() ? board.rect : result.united(board.rect);
    return result;
}

std::vector<QUuid> VectorDocument::objectsOn(int index) const
{
    std::vector<QUuid> result;
    const bool single = artboardCount() == 1;
    const QRectF rect = single ? QRectF() : artboard(index).rect;
    for (const QUuid &layer : layers()) {
        for (const QUuid &child : children(layer)) {
            if (single || bounds(child, true).intersects(rect))
                result.push_back(child);
        }
    }
    return result;
}

std::vector<QUuid> VectorDocument::artCenteredIn(const QRectF &rect) const
{
    std::vector<QUuid> result;
    for (const QUuid &layer : layers()) {
        for (const QUuid &child : children(layer)) {
            if (rect.contains(bounds(child).center()))
                result.push_back(child);
        }
    }
    return result;
}

VectorDocument VectorDocument::artboardDocument(int index) const
{
    VectorDocument result = *this;
    const Artboard board = artboard(index);
    // The result is one page: the art and guides of the others go.
    const std::vector<QUuid> onPage = layers();
    std::vector<QUuid> doomed;
    for (const QUuid &layer : allLayers()) {
        if (std::find(onPage.begin(), onPage.end(), layer) == onPage.end())
            doomed.push_back(layer);
    }
    if (artboardCount() > 1) {
        const std::vector<QUuid> keep = objectsOn(index);
        for (const QUuid &layer : onPage) {
            for (const QUuid &child : children(layer)) {
                if (std::find(keep.begin(), keep.end(), child) == keep.end())
                    doomed.push_back(child);
            }
        }
    }
    result.remove(doomed);
    std::erase_if(result.guides, [&](const Guide &guide) { return !isOnCurrentPage(guide); });
    result.pages.clear();
    result.currentPage = QUuid();
    for (VectorObject &object : result.objects)
        object.page = QUuid();
    for (Guide &guide : result.guides)
        guide.page = QUuid();
    const QTransform shift = QTransform::fromTranslate(-board.rect.left(), -board.rect.top());
    for (const QUuid &layer : result.layers())
        result.transform(layer, shift);
    for (Guide &guide : result.guides)
        guide.position -= guide.orientation == Qt::Horizontal ? board.rect.top() : board.rect.left();
    result.size = board.rect.size();
    result.background = board.background;
    result.artboards.clear();
    result.exportAssets.clear();
    return result;
}

QString VectorDocument::uniqueArtboardName(const QString &base) const
{
    const std::vector<Artboard> boards = allArtboards();
    for (int number = 1;; ++number) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(number);
        const bool taken = std::any_of(boards.begin(), boards.end(), [&](const Artboard &board) { return board.name == candidate; });
        if (!taken)
            return candidate;
    }
}
