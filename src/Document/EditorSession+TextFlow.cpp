#include "Document/EditorSession.h"
#include "Document/TextLayout.h"
#include <algorithm>
#include <limits>
#include <set>

namespace {
// Walks threadNext backwards: the box nothing else points at, following `id`'s chain.
QUuid threadHeadOf(const VectorDocument &document, const QUuid &id)
{
    QUuid head = id;
    std::set<QUuid> seen{head};
    for (;;) {
        const auto found = std::find_if(document.objects.begin(), document.objects.end(),
                                        [&](const VectorObject &o) { return o.text.threadNext == head; });
        if (found == document.objects.end() || seen.count(found->id))
            return head;
        head = found->id;
        seen.insert(head);
    }
}

// `to` reachable by following threadNext from `from`: linking them would cycle.
bool reaches(const VectorDocument &document, QUuid from, const QUuid &to)
{
    std::set<QUuid> seen;
    while (!from.isNull() && seen.insert(from).second) {
        if (from == to)
            return true;
        const VectorObject *box = document.find(from);
        from = box ? box->text.threadNext : QUuid();
    }
    return false;
}
}

QUuid EditorSession::convertPathToTypeOnPath(const QUuid &id)
{
    if (!m_document)
        return {};
    const VectorObject *path = m_document->find(id);
    if (!path || path->kind != ObjectKind::path || path->path.isEmpty() || !path->parentID)
        return {};
    VectorObject made = textObject(QPointF(), QString());
    made.text.onPath = TextPath{path->path, 0, false};
    made.fill = path->fill;
    made.stroke.paint = Paint::none();
    made.name = QStringLiteral("Type on a Path");
    const QUuid parent = *path->parentID;
    const QUuid madeId = made.id;
    edit(QStringLiteral("Type on a Path"), [&](VectorDocument &document) {
        document.remove({id});
        document.insert(made, parent);
        m_selection = {madeId};
    });
    return madeId;
}

void EditorSession::flipTypeOnPath(const QUuid &id)
{
    if (!m_document)
        return;
    const VectorObject *object = m_document->find(id);
    if (!object || !object->text.onPath)
        return;
    VectorObject flipped = *object;
    flipped.text.onPath->flipped = !flipped.text.onPath->flipped;
    edit(QStringLiteral("Flip Type on a Path"), [&](VectorDocument &document) { *document.find(id) = flipped; });
}

void EditorSession::setTextWrap(const QUuid &id, std::optional<double> offset)
{
    if (!m_document || !m_document->find(id))
        return;
    VectorObject object = *m_document->find(id);
    object.textWrap = offset && *offset >= 0 ? offset : std::nullopt;
    updateObject(object, object.textWrap ? QStringLiteral("Text Wrap") : QStringLiteral("Release Text Wrap"));
}

void EditorSession::linkThread(const QUuid &fromId, const QUuid &toId)
{
    if (!m_document || fromId == toId)
        return;
    const VectorObject *from = m_document->find(fromId), *to = m_document->find(toId);
    if (!from || !to || from->kind != ObjectKind::text || to->kind != ObjectKind::text || !from->text.area || !to->text.area)
        return;
    if (reaches(*m_document, toId, fromId))
        return;
    edit(QStringLiteral("Thread Text"), [&](VectorDocument &document) {
        VectorObject *toObject = document.find(toId);
        if (!toObject->text.text.isEmpty()) {
            VectorObject *head = document.find(threadHeadOf(document, fromId));
            const QString sep = head->text.text.isEmpty() ? QString() : QStringLiteral("\n");
            const int at = head->text.text.size();
            head->text.replace(at, at, sep + toObject->text.text);
            toObject->text.text.clear();
            toObject->text.runs.clear();
            toObject->text.paragraphFormats.clear();
        }
        document.find(fromId)->text.threadNext = toId;
    });
}

QUuid EditorSession::linkNewThread(const QUuid &fromId, QPointF origin, QSizeF size)
{
    if (!m_document)
        return {};
    const VectorObject *from = m_document->find(fromId);
    if (!from || from->kind != ObjectKind::text || !from->text.area)
        return {};
    VectorObject box = textObject(origin, QString());
    box.text.area = QSizeF(std::max(1.0, size.width()), std::max(1.0, size.height()));
    box.transform = QTransform::fromTranslate(origin.x(), origin.y());
    box.fill = from->fill;
    box.name = QStringLiteral("Text");
    const QUuid boxId = box.id;
    edit(QStringLiteral("Thread Text"), [&](VectorDocument &document) {
        insertNew(document, box);
        document.find(fromId)->text.threadNext = boxId;
        m_selection = {boxId};
    });
    return boxId;
}

void EditorSession::removeThreading(const QUuid &id)
{
    if (!m_document)
        return;
    const QUuid headId = threadHeadOf(*m_document, id);
    const VectorObject *head = m_document->find(headId);
    if (!head || !head->text.flow.story)
        return;
    std::vector<QUuid> chain{headId};
    for (QUuid next = head->text.threadNext; !next.isNull(); ) {
        const VectorObject *box = m_document->find(next);
        if (!box || std::find(chain.begin(), chain.end(), next) != chain.end())
            break;
        chain.push_back(next);
        next = box->text.threadNext;
    }
    if (chain.size() < 2)
        return;
    const TextContent story = *head->text.flow.story;
    // Each box's current lines say what stretch of the story it shows now.
    std::vector<std::pair<int, int>> ranges;
    for (const QUuid &boxId : chain) {
        const TextLayout layout(m_document->find(boxId)->text);
        int lo = std::numeric_limits<int>::max(), hi = 0;
        for (const TextLayout::Line &line : layout.lines()) {
            if (line.hidden)
                continue;
            lo = std::min(lo, line.start);
            hi = std::max(hi, line.start + line.length);
        }
        ranges.push_back(lo == std::numeric_limits<int>::max() ? std::pair(0, 0) : std::pair(lo, hi));
    }
    edit(QStringLiteral("Remove Threading"), [&](VectorDocument &document) {
        for (size_t index = 0; index < chain.size(); ++index) {
            VectorObject *box = document.find(chain[index]);
            TextContent slice = story;
            const auto [lo, hi] = ranges[index];
            slice.replace(hi, slice.text.size(), QString());
            slice.replace(0, lo, QString());
            slice.area = box->text.area;
            slice.flow = TextFlow{};
            slice.threadNext = QUuid();
            box->text = slice;
        }
    });
}
