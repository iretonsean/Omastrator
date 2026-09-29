#include "Document/VectorDocument.h"
#include <algorithm>
#include <set>

namespace {
// The wrap objects above `box` in paint order, as exclusions in its own local coordinates.
std::vector<QRectF> exclusionsFor(const VectorDocument &document, const VectorObject &box, int boxIndex)
{
    std::vector<QRectF> exclusions;
    const QTransform toLocal = box.transform.inverted();
    const bool paged = document.pages.size() > 1;
    const QUuid boxPage = paged ? document.pageOf(box.id) : QUuid();
    for (int index = boxIndex + 1; index < int(document.objects.size()); ++index) {
        const VectorObject &wrap = document.objects[size_t(index)];
        if (!wrap.textWrap)
            continue;
        // A wrap on another page is nowhere near this text, whatever its coordinates say.
        if (paged && document.pageOf(wrap.id) != boxPage)
            continue;
        const QRectF grown = document.bounds(wrap.id).adjusted(-*wrap.textWrap, -*wrap.textWrap, *wrap.textWrap, *wrap.textWrap);
        exclusions.push_back(toLocal.mapRect(grown));
    }
    return exclusions;
}

// A grow box (height 0) never overflows on its own; a big frame keeps the wrap-only path honest.
TextFrame frameFor(const VectorDocument &document, const VectorObject &box, int boxIndex)
{
    QSizeF size = *box.text.area;
    if (size.height() <= 0)
        size.setHeight(1e6);
    return {size, exclusionsFor(document, box, boxIndex), box.transform};
}
}

void VectorDocument::reflowText()
{
    for (VectorObject &object : objects) {
        if (object.kind == ObjectKind::text)
            object.text.flow = TextFlow{};
    }
    const bool anyWrap = std::any_of(objects.begin(), objects.end(), [](const VectorObject &o) { return o.textWrap.has_value(); });
    const bool anyThread = std::any_of(objects.begin(), objects.end(), [](const VectorObject &o) { return !o.text.threadNext.isNull(); });
    if (!anyWrap && !anyThread)
        return;
    std::set<QUuid> followers;
    for (const VectorObject &object : objects) {
        if (!object.text.threadNext.isNull())
            followers.insert(object.text.threadNext);
    }
    for (size_t headIndex = 0; headIndex < objects.size(); ++headIndex) {
        const VectorObject &head = objects[headIndex];
        if (head.kind != ObjectKind::text || !head.text.area || followers.count(head.id))
            continue;
        // Walk threadNext into a chain of area-type boxes; a cycle or a bad link ends it early.
        std::vector<QUuid> chain{head.id};
        std::set<QUuid> chainSeen{head.id};
        QUuid next = head.text.threadNext;
        while (!next.isNull() && !chainSeen.count(next)) {
            const VectorObject *box = find(next);
            if (!box || box->kind != ObjectKind::text || !box->text.area)
                break;
            chain.push_back(next);
            chainSeen.insert(next);
            next = box->text.threadNext;
        }
        if (chain.size() == 1) {
            // Not threaded: a flow only when something actually wraps around it.
            const int index = indexOf(head.id);
            std::vector<QRectF> exclusions = exclusionsFor(*this, head, index);
            if (exclusions.empty())
                continue;
            VectorObject *mutableHead = find(head.id);
            mutableHead->text.flow.frames = {frameFor(*this, *mutableHead, index)};
            mutableHead->text.flow.head = head.id;
            mutableHead->text.flow.frame = 0;
            continue;
        }
        std::vector<TextFrame> frames;
        frames.reserve(chain.size());
        for (const QUuid &id : chain) {
            const int index = indexOf(id);
            frames.push_back(frameFor(*this, *find(id), index));
        }
        TextContent storyContent = find(head.id)->text;
        storyContent.flow = TextFlow{};
        storyContent.threadNext = QUuid();
        const auto story = std::make_shared<const TextContent>(std::move(storyContent));
        for (size_t index = 0; index < chain.size(); ++index) {
            VectorObject *box = find(chain[index]);
            box->text.flow.story = story;
            box->text.flow.head = head.id;
            box->text.flow.frames = frames;
            box->text.flow.frame = int(index);
        }
    }
}
