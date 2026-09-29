#include "Document/VectorDocument.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Auto layout (docs/AUTO-LAYOUT.md): frames that place their children in a row
// or column and can size themselves to them.

namespace {
const std::array<std::pair<LayoutDirection, const char *>, 2> directionNames{{
    {LayoutDirection::horizontal, "horizontal"}, {LayoutDirection::vertical, "vertical"}}};
const std::array<std::pair<LayoutAlign, const char *>, 3> alignNames{{
    {LayoutAlign::start, "start"}, {LayoutAlign::center, "center"}, {LayoutAlign::end, "end"}}};
const std::array<std::pair<LayoutSizing, const char *>, 3> sizingNames{{
    {LayoutSizing::fixed, "fixed"}, {LayoutSizing::hug, "hug"}, {LayoutSizing::fill, "fill"}}};
const std::array<std::pair<LayoutConstraint, const char *>, 5> constraintNames{{
    {LayoutConstraint::start, "start"}, {LayoutConstraint::end, "end"}, {LayoutConstraint::both, "both"},
    {LayoutConstraint::center, "center"}, {LayoutConstraint::scale, "scale"}}};

template <typename Enum, size_t Count> QString nameOf(const std::array<std::pair<Enum, const char *>, Count> &names, Enum value)
{
    for (const auto &[each, name] : names) {
        if (each == value)
            return QString::fromLatin1(name);
    }
    return {};
}

template <typename Enum, size_t Count> std::optional<Enum> valueOf(const std::array<std::pair<Enum, const char *>, Count> &names, const QString &raw)
{
    for (const auto &[each, name] : names) {
        if (raw == QLatin1String(name))
            return each;
    }
    return std::nullopt;
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

// Where an item of `size` starts in `room`.
double aligned(LayoutAlign align, double room)
{
    switch (align) {
    case LayoutAlign::start:
        return 0;
    case LayoutAlign::center:
        return room / 2;
    case LayoutAlign::end:
        return room;
    }
    return 0;
}

// Along the layout's direction (primary) or across it (counter).
struct Axes {
    bool horizontal;
    double primary(QSizeF size) const { return horizontal ? size.width() : size.height(); }
    double counter(QSizeF size) const { return horizontal ? size.height() : size.width(); }
    QSizeF size(double primary, double counter) const { return horizontal ? QSizeF(primary, counter) : QSizeF(counter, primary); }
    QPointF point(double primary, double counter) const { return horizontal ? QPointF(primary, counter) : QPointF(counter, primary); }
    LayoutSizing primarySizing(const LayoutItem &item) const { return horizontal ? item.width : item.height; }
    LayoutSizing counterSizing(const LayoutItem &item) const { return horizontal ? item.height : item.width; }
};

class Layouter {
public:
    explicit Layouter(VectorDocument &document) : m_document(document) {}

    // Lays out one frame; true when anything moved or changed size.
    bool layOut(const QUuid &id)
    {
        VectorObject *frame = m_document.find(id);
        if (!frame || !frame->autoLayout || !frame->shape || !frame->shape->placement.isIdentity())
            return false;
        const AutoLayout layout = *frame->autoLayout;
        const Axes axes{layout.direction == LayoutDirection::horizontal};
        std::vector<QUuid> items;
        for (const QUuid &child : m_document.children(id)) {
            const VectorObject *object = m_document.find(child);
            if (object && object->isVisible && !object->layout.absolute)
                items.push_back(child);
        }
        const QRectF box = frame->shape->rect.normalized();
        const QSizeF padStart(layout.paddingLeft, layout.paddingTop), padEnd(layout.paddingRight, layout.paddingBottom);
        const double padPrimary = axes.primary(padStart) + axes.primary(padEnd), padCounter = axes.counter(padStart) + axes.counter(padEnd);
        // A frame hugs along an axis only when it lays its content out that way.
        const bool hugPrimary = axes.primarySizing(frame->layout) == LayoutSizing::hug;
        const bool hugCounter = axes.counterSizing(frame->layout) == LayoutSizing::hug;
        std::vector<QSizeF> sizes;
        for (const QUuid &item : items)
            sizes.push_back(m_document.bounds(item).size());
        if (layout.wrap && axes.horizontal)
            return wrap(id, layout, items, sizes, box, padStart, padEnd, hugPrimary, hugCounter);

        const size_t count = items.size();
        const double gaps = count > 1 && !layout.spaceBetween ? layout.gap * double(count - 1) : 0;
        // Along the flow, filling needs room the frame decides, and a hugging frame has none to give.
        // Across it, a filling item stretches to the frame, or in a hugging one to the deepest of the rest.
        std::vector<bool> fillPrimary(count), fillCounter(count);
        double settled = 0, deepest = 0;
        size_t fills = 0;
        bool anyDepth = false;
        for (size_t index = 0; index < count; ++index) {
            const LayoutItem &item = m_document.find(items[index])->layout;
            fillPrimary[index] = !hugPrimary && axes.primarySizing(item) == LayoutSizing::fill;
            fillCounter[index] = axes.counterSizing(item) == LayoutSizing::fill;
            anyDepth = anyDepth || !fillCounter[index];
            if (fillPrimary[index])
                ++fills;
            else
                settled += axes.primary(sizes[index]);
            if (!fillCounter[index])
                deepest = std::max(deepest, axes.counter(sizes[index]));
        }
        const double innerPrimary = hugPrimary ? settled + gaps : std::max(0.0, axes.primary(box.size()) - padPrimary);
        const double innerCounter = hugCounter && anyDepth ? deepest : std::max(0.0, axes.counter(box.size()) - padCounter);
        bool changed = resizeBox(id, axes.size(hugPrimary ? innerPrimary + padPrimary : axes.primary(box.size()),
                                               hugCounter ? innerCounter + padCounter : axes.counter(box.size())));
        const double free = innerPrimary - settled - gaps;
        const double fillSize = fills ? std::max(1.0, free / double(fills)) : 0;
        double total = gaps;
        for (size_t index = 0; index < count; ++index)
            total += fillPrimary[index] ? fillSize : axes.primary(sizes[index]);
        double gap = layout.gap, at = 0;
        if (layout.spaceBetween && count > 1)
            gap = std::max(0.0, (innerPrimary - (total - gaps)) / double(count - 1));
        else if (!fills)
            at = aligned(layout.primary, innerPrimary - total);
        const QPointF origin = box.topLeft() + QPointF(layout.paddingLeft, layout.paddingTop);
        for (size_t index = 0; index < count; ++index) {
            const double primary = fillPrimary[index] ? fillSize : axes.primary(sizes[index]);
            const double counter = fillCounter[index] ? innerCounter : axes.counter(sizes[index]);
            const QPointF corner = origin + axes.point(at, aligned(layout.counter, innerCounter - counter));
            changed = place(items[index], QRectF(corner, axes.size(primary, counter))) || changed;
            at += primary + gap;
        }
        return changed;
    }

private:
    // Rows for a horizontal frame that wraps: items run on while they fit its inner width.
    bool wrap(const QUuid &id, const AutoLayout &layout, const std::vector<QUuid> &items, const std::vector<QSizeF> &sizes, const QRectF &box,
              QSizeF padStart, QSizeF padEnd, bool hugWidth, bool hugHeight)
    {
        const double width = hugWidth ? std::numeric_limits<double>::infinity() : std::max(0.0, box.width() - padStart.width() - padEnd.width());
        struct Row {
            size_t first = 0, last = 0;
            double width = 0, height = 0;
        };
        std::vector<Row> rows;
        for (size_t index = 0; index < items.size(); ++index) {
            const double itemWidth = sizes[index].width();
            if (rows.empty() || (rows.back().last > rows.back().first && rows.back().width + layout.gap + itemWidth > width + 1e-6))
                rows.push_back({index, index, 0, 0});
            Row &row = rows.back();
            row.width += (row.last > row.first ? layout.gap : 0) + itemWidth;
            row.height = std::max(row.height, sizes[index].height());
            row.last = index + 1;
        }
        double content = rows.empty() ? 0 : layout.counterGap * double(rows.size() - 1), widest = 0;
        for (const Row &row : rows) {
            content += row.height;
            widest = std::max(widest, row.width);
        }
        const double innerWidth = hugWidth ? widest : width;
        const double innerHeight = hugHeight ? content : std::max(0.0, box.height() - padStart.height() - padEnd.height());
        bool changed = resizeBox(id, QSizeF(hugWidth ? innerWidth + padStart.width() + padEnd.width() : box.width(),
                                            hugHeight ? innerHeight + padStart.height() + padEnd.height() : box.height()));
        double y = box.top() + padStart.height() + aligned(layout.counter, innerHeight - content);
        for (const Row &row : rows) {
            double x = box.left() + padStart.width() + aligned(layout.primary, innerWidth - row.width);
            for (size_t index = row.first; index < row.last; ++index) {
                changed = place(items[index], QRectF(QPointF(x, y + aligned(layout.counter, row.height - sizes[index].height())), sizes[index]))
                    || changed;
                x += sizes[index].width() + layout.gap;
            }
            y += row.height + layout.counterGap;
        }
        return changed;
    }

    // The frame's box to `size`, its corner kept; its children stay put.
    bool resizeBox(const QUuid &id, QSizeF size)
    {
        VectorObject *frame = m_document.find(id);
        const QRectF box = frame->shape->rect.normalized();
        if (near(box.width(), size.width()) && near(box.height(), size.height()))
            return false;
        frame->shape->rect = QRectF(box.topLeft(), QSizeF(std::max(0.01, size.width()), std::max(0.01, size.height())));
        frame->path = frame->shape->path();
        return true;
    }

    // One item to `target`: resized when it can take that size, then moved so its corner is the target's.
    bool place(const QUuid &id, const QRectF &target)
    {
        bool changed = resize(id, target.size());
        const QRectF now = m_document.bounds(id);
        const QPointF shift = target.topLeft() - now.topLeft();
        if (near(shift.x(), 0) && near(shift.y(), 0))
            return changed;
        m_document.transform(id, QTransform::fromTranslate(shift.x(), shift.y()));
        return true;
    }

    bool resize(const QUuid &id, QSizeF size)
    {
        VectorObject *object = m_document.find(id);
        const QRectF now = m_document.bounds(id);
        if (near(now.width(), size.width()) && near(now.height(), size.height()))
            return false;
        size = QSizeF(std::max(0.01, size.width()), std::max(0.01, size.height()));
        if (object->kind == ObjectKind::frame && object->shape && object->shape->placement.isIdentity()) {
            // Its own children follow their constraints; a hugging frame keeps hugging.
            const LayoutItem sizing = object->layout;
            m_document.resizeFrame(id, QRectF(object->shape->rect.normalized().topLeft(), size));
            m_document.find(id)->layout = sizing;
            return true;
        }
        if (object->kind == ObjectKind::path && object->liveShape() && object->shape->placement.isIdentity()) {
            const Qt::FillRule rule = object->path.fillRule;
            object->shape->rect = QRectF(object->shape->rect.normalized().topLeft(), size);
            object->path = object->shape->path();
            object->path.fillRule = rule;
            return true;
        }
        if (object->kind == ObjectKind::text) {
            // Area type rewraps to the width (and takes a fixed height's too); point type keeps its own size.
            if (!object->text.area || object->transform.type() > QTransform::TxTranslate)
                return false;
            const QSizeF area = *object->text.area;
            object->text.area = QSizeF(size.width(), area.height() > 0 ? size.height() : 0);
            return area != *object->text.area;
        }
        if (now.width() <= 0 || now.height() <= 0)
            return false;
        // Anything else stretches from its corner.
        const QTransform stretch = QTransform::fromTranslate(-now.left(), -now.top()) * QTransform::fromScale(size.width() / now.width(), size.height() / now.height())
            * QTransform::fromTranslate(now.left(), now.top());
        m_document.transform(id, stretch, false);
        return true;
    }

    VectorDocument &m_document;

public:
    // Public for resizeFrame: one object to `target`, resized if it can be, then moved there.
    bool fit(const QUuid &id, const QRectF &target) { return place(id, target); }
};

// One axis of a constraint: where [from, to] of the old frame [start, end] lands in the new one.
std::pair<double, double> constrained(LayoutConstraint constraint, double from, double to, double start, double end, double newStart, double newEnd)
{
    switch (constraint) {
    case LayoutConstraint::start:
        return {from + newStart - start, to + newStart - start};
    case LayoutConstraint::end:
        return {from + newEnd - end, to + newEnd - end};
    case LayoutConstraint::both:
        return {from + newStart - start, std::max(from + newStart - start + 0.01, to + newEnd - end)};
    case LayoutConstraint::center: {
        const double shift = (newStart + newEnd) / 2 - (start + end) / 2;
        return {from + shift, to + shift};
    }
    case LayoutConstraint::scale: {
        const double ratio = end - start > 1e-9 ? (newEnd - newStart) / (end - start) : 1;
        return {newStart + (from - start) * ratio, newStart + (to - start) * ratio};
    }
    }
    return {from, to};
}
}

QString rawValue(LayoutConstraint constraint)
{
    return nameOf(constraintNames, constraint);
}

std::optional<LayoutConstraint> layoutConstraint(const QString &raw)
{
    return valueOf(constraintNames, raw);
}

QString rawValue(PreviewRule rule)
{
    return rule == PreviewRule::fixed ? QStringLiteral("fixed") : QStringLiteral("constraints");
}

std::optional<PreviewRule> previewRule(const QString &raw)
{
    if (raw == QLatin1String("fixed"))
        return PreviewRule::fixed;
    if (raw == QLatin1String("constraints"))
        return PreviewRule::constraints;
    return std::nullopt;
}

void VectorDocument::resizeFrame(const QUuid &id, const QRectF &box, bool preview)
{
    VectorObject *frame = find(id);
    if (!frame || frame->kind != ObjectKind::frame || !frame->shape || !frame->shape->placement.isIdentity())
        return;
    const QRectF old = frame->shape->rect.normalized();
    const QRectF fresh = box.normalized();
    if (!near(fresh.width(), old.width()))
        frame->layout.width = frame->layout.width == LayoutSizing::hug ? LayoutSizing::fixed : frame->layout.width;
    if (!near(fresh.height(), old.height()))
        frame->layout.height = frame->layout.height == LayoutSizing::hug ? LayoutSizing::fixed : frame->layout.height;
    frame->shape->rect = QRectF(fresh.topLeft(), QSizeF(std::max(0.01, fresh.width()), std::max(0.01, fresh.height())));
    frame->path = frame->shape->path();
    const bool flows = frame->autoLayout.has_value();
    std::vector<QUuid> moving;
    for (const QUuid &child : children(id)) {
        VectorObject *object = find(child);
        if (object && preview && object->layout.previewRule == PreviewRule::fixed) {
            // Absolute takes it out of the flow, so it stays where the design put it.
            object->layout.absolute = true;
            continue;
        }
        // Children in a flow are placed by the layout, which runs after.
        if (object && (!flows || object->layout.absolute))
            moving.push_back(child);
    }
    constrainToBox(moving, old, fresh);
}

void VectorDocument::constrainToBox(const std::vector<QUuid> &ids, const QRectF &old, const QRectF &fresh)
{
    Layouter layouter(*this);
    for (const QUuid &id : ids) {
        const VectorObject *object = find(id);
        if (!object)
            continue;
        const QRectF was = bounds(id);
        const auto [left, right] = constrained(object->layout.horizontal, was.left(), was.right(), old.left(), old.right(), fresh.left(), fresh.right());
        const auto [top, bottom] = constrained(object->layout.vertical, was.top(), was.bottom(), old.top(), old.bottom(), fresh.top(), fresh.bottom());
        layouter.fit(id, QRectF(QPointF(left, top), QPointF(right, bottom)));
    }
}

QString rawValue(LayoutDirection direction)
{
    return nameOf(directionNames, direction);
}

QString rawValue(LayoutAlign align)
{
    return nameOf(alignNames, align);
}

QString rawValue(LayoutSizing sizing)
{
    return nameOf(sizingNames, sizing);
}

std::optional<LayoutDirection> layoutDirection(const QString &raw)
{
    return valueOf(directionNames, raw);
}

std::optional<LayoutAlign> layoutAlign(const QString &raw)
{
    return valueOf(alignNames, raw);
}

std::optional<LayoutSizing> layoutSizing(const QString &raw)
{
    return valueOf(sizingNames, raw);
}

void VectorDocument::applyAutoLayout()
{
    std::vector<std::pair<int, QUuid>> frames;
    for (const VectorObject &object : objects) {
        if (object.kind != ObjectKind::frame || !object.autoLayout)
            continue;
        int depth = 0;
        for (const VectorObject *parent = object.parentID ? find(*object.parentID) : nullptr; parent;
             parent = parent->parentID ? find(*parent->parentID) : nullptr)
            ++depth;
        frames.emplace_back(depth, object.id);
    }
    if (frames.empty())
        return;
    // Innermost first, so a hugging frame has its size before its parent measures it; a fill size
    // handed down changes the child's own layout, so go again until nothing moves.
    std::stable_sort(frames.begin(), frames.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    Layouter layouter(*this);
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (const auto &[depth, id] : frames)
            changed = layouter.layOut(id) || changed;
        if (!changed)
            return;
    }
}
