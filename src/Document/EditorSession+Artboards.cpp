#include "Document/EditorSession.h"
#include <algorithm>

int EditorSession::activeArtboard() const
{
    if (!m_document)
        return 0;
    return std::clamp(m_activeArtboard, 0, m_document->artboardCount() - 1);
}

void EditorSession::setActiveArtboard(int index)
{
    if (!m_document)
        return;
    const int clamped = std::clamp(index, 0, m_document->artboardCount() - 1);
    if (clamped == m_activeArtboard)
        return;
    m_activeArtboard = clamped;
    notify(false);
}

QUuid EditorSession::addArtboard(QRectF rect)
{
    if (!m_document)
        return {};
    if (rect.isEmpty()) {
        const QRectF from = m_document->artboard(activeArtboard()).rect;
        rect = QRectF(from.right() + 20, from.top(), from.width(), from.height());
    }
    Artboard board;
    board.name = m_document->uniqueArtboardName();
    board.rect = rect;
    const QUuid id = board.id;
    const int newIndex = m_document->artboardCount();
    edit(QStringLiteral("New Artboard"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards.push_back(board);
        document.setArtboards(boards);
    });
    m_activeArtboard = newIndex;
    notify(false);
    return id;
}

QUuid EditorSession::duplicateArtboard(int index)
{
    if (!m_document)
        return {};
    const std::vector<Artboard> current = m_document->allArtboards();
    if (index < 0 || index >= int(current.size()))
        return {};
    Artboard board = current[size_t(index)];
    board.id = QUuid::createUuid();
    board.name = m_document->uniqueArtboardName();
    board.rect.moveLeft(board.rect.left() + board.rect.width() + 20);
    const double shift = board.rect.left() - current[size_t(index)].rect.left();
    const std::vector<QUuid> art = m_document->objectsOn(index);
    const QUuid id = board.id;
    const int newIndex = int(current.size());
    edit(QStringLiteral("Duplicate Artboard"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards.push_back(board);
        document.setArtboards(boards);
        // Copy the art that sat on the original, moved the same way the duplicate is.
        for (const QUuid &object : art) {
            std::vector<VectorObject> subtree = document.copySubtree(object);
            if (subtree.empty())
                continue;
            const QUuid parent = *document.find(object)->parentID;
            const QUuid root = subtree.front().id;
            document.insert(subtree.front(), parent, object);
            for (size_t i = 1; i < subtree.size(); ++i)
                document.insert(subtree[i], *subtree[i].parentID);
            document.transform(root, QTransform::fromTranslate(shift, 0));
        }
    });
    m_activeArtboard = newIndex;
    notify(false);
    return id;
}

void EditorSession::renameArtboard(int index, const QString &name)
{
    if (!m_document || index < 0 || index >= m_document->artboardCount() || name.isEmpty())
        return;
    if (m_document->artboard(index).name == name)
        return;
    edit(QStringLiteral("Rename Artboard"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards[size_t(index)].name = name;
        document.setArtboards(boards);
    });
}

void EditorSession::deleteArtboard(int index)
{
    if (!m_document || m_document->artboardCount() <= 1 || index < 0 || index >= m_document->artboardCount())
        return;
    edit(QStringLiteral("Delete Artboard"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards.erase(boards.begin() + index);
        document.setArtboards(boards);
    });
    if (m_document)
        m_activeArtboard = std::clamp(m_activeArtboard, 0, m_document->artboardCount() - 1);
    notify(false);
}

void EditorSession::previewArtboardRect(int index, QRectF rect)
{
    if (!m_document || !m_interaction || index < 0 || index >= m_interaction->base.artboardCount()
        || !(rect.width() > 0 && rect.height() > 0))
        return;
    VectorDocument document = m_interaction->base;
    std::vector<Artboard> boards = document.allArtboards();
    const QRectF from = boards[size_t(index)].rect;
    const QPointF delta = rect.topLeft() - from.topLeft();
    boards[size_t(index)].rect = rect;
    document.setArtboards(boards);
    if (artboardMovesArt && !delta.isNull()) {
        // Art whose centre sat on the artboard before the drag moves the same way.
        const VectorDocument &before = m_interaction->base;
        for (const QUuid &layer : before.layers()) {
            for (const QUuid &child : before.children(layer)) {
                if (from.contains(before.bounds(child).center()))
                    document.transform(child, QTransform::fromTranslate(delta.x(), delta.y()));
            }
        }
    }
    m_document = std::move(document);
    notify();
}

void EditorSession::fitArtboardToArtwork(int index)
{
    if (!m_document || index < 0 || index >= m_document->artboardCount())
        return;
    std::vector<QUuid> ids = m_document->objectsOn(index);
    if (ids.empty()) {
        // Nothing overlaps it: hug every visible object instead.
        for (const QUuid &layer : m_document->layers()) {
            if (!m_document->isEffectivelyVisible(layer))
                continue;
            for (const QUuid &child : m_document->children(layer)) {
                if (m_document->isEffectivelyVisible(child))
                    ids.push_back(child);
            }
        }
    }
    if (ids.empty())
        return;
    const QRectF bounds = m_document->bounds(ids, true);
    if (bounds.isEmpty())
        return;
    edit(QStringLiteral("Fit Artboard to Artwork"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards[size_t(index)].rect = bounds;
        document.setArtboards(boards);
    });
}

void EditorSession::switchArtboardOrientation(int index)
{
    if (!m_document || index < 0 || index >= m_document->artboardCount())
        return;
    edit(QStringLiteral("Switch Orientation"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        QRectF &rect = boards[size_t(index)].rect;
        rect.setSize(QSizeF(rect.height(), rect.width()));
        document.setArtboards(boards);
    });
}

void EditorSession::showArtboard(bool next)
{
    if (!m_document || m_document->artboardCount() < 2)
        return;
    const int count = m_document->artboardCount();
    m_activeArtboard = ((activeArtboard() + (next ? 1 : -1)) % count + count) % count;
    zoomToRect(m_document->artboard(m_activeArtboard).rect);
}

void EditorSession::fitAllArtboards()
{
    if (!m_document)
        return;
    zoomToRect(m_document->artboardBounds());
}

void EditorSession::collectForExport(const std::vector<QUuid> &ids)
{
    if (!m_document || ids.empty())
        return;
    std::vector<QUuid> add;
    for (const QUuid &id : ids) {
        if (m_document->find(id) && std::find(m_document->exportAssets.begin(), m_document->exportAssets.end(), id) == m_document->exportAssets.end())
            add.push_back(id);
    }
    if (add.empty())
        return;
    edit(QStringLiteral("Collect for Export"), [&](VectorDocument &document) {
        document.exportAssets.insert(document.exportAssets.end(), add.begin(), add.end());
    });
}

void EditorSession::removeFromExport(const std::vector<QUuid> &ids)
{
    if (!m_document || m_document->exportAssets.empty())
        return;
    edit(QStringLiteral("Remove from Export"), [&](VectorDocument &document) {
        std::erase_if(document.exportAssets, [&](const QUuid &id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); });
    });
}
