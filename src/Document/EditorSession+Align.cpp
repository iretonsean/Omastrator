#include "Document/EditorSession.h"
#include <algorithm>

namespace {
// The edge's coordinate on its axis.
double edgeOf(const QRectF &bounds, AlignEdge edge)
{
    switch (edge) {
    case AlignEdge::left:
        return bounds.left();
    case AlignEdge::horizontalCenter:
        return bounds.center().x();
    case AlignEdge::right:
        return bounds.right();
    case AlignEdge::top:
        return bounds.top();
    case AlignEdge::verticalCenter:
        return bounds.center().y();
    case AlignEdge::bottom:
        return bounds.bottom();
    }
    return 0;
}

bool isHorizontal(AlignEdge edge)
{
    return edge == AlignEdge::left || edge == AlignEdge::horizontalCenter || edge == AlignEdge::right;
}

QTransform along(bool horizontal, double shift)
{
    return horizontal ? QTransform::fromTranslate(shift, 0) : QTransform::fromTranslate(0, shift);
}
}

void EditorSession::setKeyObject(std::optional<QUuid> id)
{
    if (id && (m_selection.size() < 2 || !isSelected(*id)))
        id.reset();
    if (id == m_keyObject)
        return;
    m_keyObject = id;
    notify(false);
}

void EditorSession::distribute(DistributeAxis axis)
{
    distribute(axis == DistributeAxis::horizontal ? AlignEdge::horizontalCenter : AlignEdge::verticalCenter);
}

void EditorSession::distribute(AlignEdge edge)
{
    if (!m_document || m_selection.size() < 3)
        return;
    edit(QStringLiteral("Distribute"), [&](VectorDocument &document) {
        std::vector<QUuid> ids = m_selection;
        const auto at = [&](const QUuid &id) { return edgeOf(document.bounds(id), edge); };
        std::stable_sort(ids.begin(), ids.end(), [&](const QUuid &a, const QUuid &b) { return at(a) < at(b); });
        // The outermost two stay; the rest space their edges evenly between them.
        const double first = at(ids.front()), last = at(ids.back());
        const double step = (last - first) / double(ids.size() - 1);
        for (size_t index = 1; index + 1 < ids.size(); ++index) {
            if (!document.isEffectivelyLocked(ids[index]))
                document.transform(ids[index], along(isHorizontal(edge), first + step * double(index) - at(ids[index])));
        }
    });
}

void EditorSession::distributeSpacing(DistributeAxis axis, std::optional<double> gap)
{
    const size_t needed = gap ? 2 : 3;
    if (!m_document || m_selection.size() < needed)
        return;
    const bool horizontal = axis == DistributeAxis::horizontal;
    edit(QStringLiteral("Distribute Spacing"), [&](VectorDocument &document) {
        std::vector<QUuid> ids = m_selection;
        const auto start = [&](const QUuid &id) { return horizontal ? document.bounds(id).left() : document.bounds(id).top(); };
        const auto extent = [&](const QUuid &id) { return horizontal ? document.bounds(id).width() : document.bounds(id).height(); };
        std::stable_sort(ids.begin(), ids.end(), [&](const QUuid &a, const QUuid &b) { return start(a) < start(b); });
        double space = 0;
        if (gap) {
            space = *gap;
        } else {
            double total = 0;
            for (const QUuid &id : ids)
                total += extent(id);
            space = (start(ids.back()) + extent(ids.back()) - start(ids.front()) - total) / double(ids.size() - 1);
        }
        // The key object holds still and the rest step out from it, as Illustrator does; else the first holds.
        const auto anchor = m_keyObject && gap ? std::find(ids.begin(), ids.end(), *m_keyObject) : ids.begin();
        const size_t pivot = size_t(anchor - ids.begin());
        double edge = start(ids[pivot]) + extent(ids[pivot]);
        for (size_t index = pivot + 1; index < ids.size(); ++index) {
            document.transform(ids[index], along(horizontal, edge + space - start(ids[index])));
            edge = start(ids[index]) + extent(ids[index]);
        }
        edge = start(ids[pivot]);
        for (size_t index = pivot; index-- > 0;) {
            document.transform(ids[index], along(horizontal, edge - space - extent(ids[index]) - start(ids[index])));
            edge = start(ids[index]);
        }
    });
}
