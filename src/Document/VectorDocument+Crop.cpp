#include "Document/VectorDocument.h"
#include <set>

// Shared by Share's "just this selection" export and Export for Screens' per-asset crop.
VectorDocument VectorDocument::croppedTo(const std::vector<QUuid> &ids) const
{
    std::set<QUuid> keep;
    for (const QUuid &id : ids) {
        if (!find(id))
            continue;
        keep.insert(id);
        for (const QUuid &inner : descendants(id))
            keep.insert(inner);
        // Its layer and groups, and any clipping path above it, so it looks as it does on the artboard.
        for (const VectorObject *up = find(id); up && up->parentID; up = find(*up->parentID)) {
            keep.insert(*up->parentID);
            if (const VectorObject *parent = find(*up->parentID); parent && parent->isClipGroup) {
                const std::vector<QUuid> kids = children(parent->id);
                if (!kids.empty()) {
                    keep.insert(kids.front());
                    for (const QUuid &inner : descendants(kids.front()))
                        keep.insert(inner);
                }
            }
        }
    }
    const QRectF box = bounds(std::vector<QUuid>(ids.begin(), ids.end()), true);
    if (keep.empty() || box.isEmpty())
        return *this;
    VectorDocument cropped = *this;
    std::vector<QUuid> drop;
    for (const VectorObject &object : cropped.objects) {
        if (!keep.count(object.id))
            drop.push_back(object.id);
    }
    cropped.remove(drop);
    for (const QUuid &layer : cropped.layers())
        cropped.transform(layer, QTransform::fromTranslate(-box.left(), -box.top()), false, false);
    cropped.size = box.size();
    cropped.background = Qt::transparent;
    cropped.artboards.clear();
    cropped.exportAssets.clear();
    return cropped;
}
