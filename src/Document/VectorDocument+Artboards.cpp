#include "Document/VectorDocument.h"
#include <algorithm>

std::vector<Artboard> VectorDocument::allArtboards() const
{
    if (artboards.empty()) {
        Artboard board;
        board.name = QStringLiteral("Artboard 1");
        board.rect = QRectF(QPointF(0, 0), size);
        board.background = background;
        return {board};
    }
    std::vector<Artboard> boards = artboards;
    // The first artboard's size and paper always follow `size`/`background`.
    boards[0].rect.setSize(size);
    boards[0].background = background;
    return boards;
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
    if (boards.empty()) {
        artboards.clear();
        return;
    }
    size = boards[0].rect.size();
    background = boards[0].background;
    artboards = std::move(boards);
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

VectorDocument VectorDocument::artboardDocument(int index) const
{
    VectorDocument result = *this;
    const Artboard board = artboard(index);
    if (artboardCount() > 1) {
        const std::vector<QUuid> keep = objectsOn(index);
        std::vector<QUuid> doomed;
        for (const QUuid &layer : layers()) {
            for (const QUuid &child : children(layer)) {
                if (std::find(keep.begin(), keep.end(), child) == keep.end())
                    doomed.push_back(child);
            }
        }
        result.remove(doomed);
    }
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
