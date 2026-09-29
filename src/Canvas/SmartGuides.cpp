// Ported from omadesign by Michael C Hurley, MIT (src/snap.rs).
#include "Canvas/SmartGuides.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
struct Axes {
    double min, max, mid, crossMin, crossMax;
};

Axes axes(const QRectF &bounds, int axis)
{
    const QRectF r = bounds.normalized();
    if (axis == 0)
        return {r.left(), r.right(), r.center().x(), r.top(), r.bottom()};
    return {r.top(), r.bottom(), r.center().y(), r.left(), r.right()};
}

QPointF unaxes(int axis, double along, double across)
{
    return axis == 0 ? QPointF(along, across) : QPointF(across, along);
}

struct Candidate {
    double correction;
    std::optional<QLineF> line;
    std::optional<std::array<QLineF, 2>> gaps;
};
}

QPointF constrain45(QPointF delta)
{
    const double length = std::hypot(delta.x(), delta.y());
    if (length < 1e-9)
        return delta;
    constexpr double step = std::numbers::pi / 4;
    const double angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    const QPointF axis(std::cos(angle), std::sin(angle));
    // Projected, so the pointer's reach along the axis is kept.
    const double along = delta.x() * axis.x() + delta.y() * axis.y();
    return axis * along;
}

SmartGuides::SmartGuides(const VectorDocument &document, const std::vector<QUuid> &excluded, const QUuid &excludedBoard)
{
    const auto isExcluded = [&](const QUuid &id) {
        return std::any_of(excluded.begin(), excluded.end(), [&](const QUuid &gone) { return gone == id || document.isAncestor(gone, id); });
    };
    for (const VectorObject &object : document.objects) {
        if (object.kind == ObjectKind::layer || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id) || isExcluded(object.id))
            continue;
        // Groups snap by their whole box; their members by their own.
        const QRectF bounds = document.bounds(object.id);
        if (bounds.isNull() && bounds.topLeft().isNull())
            continue;
        m_objects.push_back(bounds);
    }
    for (const Artboard &board : document.allArtboards()) {
        if (board.id != excludedBoard)
            m_boards.push_back(board.rect);
    }
}

void SmartGuides::addGuides(const std::vector<Guide> &guides)
{
    m_guides.insert(m_guides.end(), guides.begin(), guides.end());
}

SmartGuides::Result SmartGuides::point(QPointF point, double scale, std::optional<QPointF> anchor, bool constrained) const
{
    const QPointF origin = anchor.value_or(point);
    Result result = movement(QRectF(origin, QSizeF(0, 0)), point - origin, scale, constrained && anchor.has_value());
    result.delta += origin;
    return result;
}

SmartGuides::Result SmartGuides::movement(const QRectF &bounds, QPointF delta, double scale, bool constrained) const
{
    if (constrained)
        delta = constrain45(delta);
    Result result;
    result.delta = delta;
    const double tolerance = threshold / std::max(scale, 0.01);
    const QRectF moved = bounds.normalized().translated(delta);
    std::array<std::optional<Candidate>, 2> best;
    for (int axis = 0; axis < 2; ++axis) {
        const Axes m = axes(moved, axis);
        const std::array<double, 3> probes{m.min, m.mid, m.max};
        std::optional<Candidate> &slot = best[size_t(axis)];
        const auto offer = [&](Candidate candidate) {
            if (std::abs(candidate.correction) <= tolerance && (!slot || std::abs(candidate.correction) < std::abs(slot->correction)))
                slot = std::move(candidate);
        };
        // A guide across this axis: the probes snap to its line.
        for (const Guide &guide : m_guides) {
            if ((guide.orientation == Qt::Vertical) != (axis == 0))
                continue;
            for (const double probe : probes)
                offer({guide.position - probe, QLineF(unaxes(axis, guide.position, m.crossMin), unaxes(axis, guide.position, m.crossMax)), std::nullopt});
        }
        for (const std::vector<QRectF> *targets : {&m_boards, &m_objects}) {
            for (const QRectF &target : *targets) {
                const Axes t = axes(target, axis);
                for (const double along : {t.min, t.mid, t.max}) {
                    for (const double probe : probes)
                        offer({along - probe, QLineF(unaxes(axis, along, std::min(t.crossMin, m.crossMin)),
                                                     unaxes(axis, along, std::max(t.crossMax, m.crossMax))), std::nullopt});
                }
            }
        }
        // Equal spacing: only neighbours in the same row or column take part.
        std::vector<QRectF> neighbours;
        for (const QRectF &target : m_objects) {
            const Axes t = axes(target, axis);
            if (t.crossMin <= m.crossMax + tolerance && t.crossMax >= m.crossMin - tolerance)
                neighbours.push_back(target);
        }
        std::sort(neighbours.begin(), neighbours.end(), [&](const QRectF &a, const QRectF &b) { return axes(a, axis).min < axes(b, axis).min; });
        const double across = (m.crossMin + m.crossMax) / 2;
        const auto span = [&](double a, double b) { return QLineF(unaxes(axis, a, across), unaxes(axis, b, across)); };
        for (size_t index = 0; index + 1 < neighbours.size(); ++index) {
            const Axes a = axes(neighbours[index], axis), b = axes(neighbours[index + 1], axis);
            const double gap = b.min - a.max;
            if (gap <= 0)
                continue;
            const double width = m.max - m.min;
            const double right = b.max + gap;
            const double left = a.min - gap - width;
            offer({right - m.min, std::nullopt, std::array{span(a.max, b.min), span(b.max, right)}});
            offer({left - m.min, std::nullopt, std::array{span(left + width, a.min), span(a.max, b.min)}});
            if (gap >= width) {
                const double between = (a.max + b.min - width) / 2;
                offer({between - m.min, std::nullopt, std::array{span(a.max, between), span(between + width, b.min)}});
            }
        }
    }
    const auto take = [&](const Candidate &candidate) {
        if (candidate.line)
            result.lines.push_back(*candidate.line);
        if (candidate.gaps)
            result.gaps.insert(result.gaps.end(), candidate.gaps->begin(), candidate.gaps->end());
    };
    if (constrained) {
        // Slide along the constrained direction to whichever axis snaps nearer.
        const double length = std::hypot(delta.x(), delta.y());
        if (length > 1e-9) {
            const QPointF direction = delta / length;
            std::optional<std::pair<double, int>> choice;
            for (int axis = 0; axis < 2; ++axis) {
                const double component = axis == 0 ? direction.x() : direction.y();
                if (std::abs(component) > 0.1 && best[size_t(axis)]) {
                    const double distance = best[size_t(axis)]->correction / component;
                    if (std::abs(distance) <= tolerance && (!choice || std::abs(distance) < std::abs(choice->first)))
                        choice = std::pair(distance, axis);
                }
            }
            if (choice) {
                result.delta += direction * choice->first;
                take(*best[size_t(choice->second)]);
                result.snappedX = std::abs(direction.x()) > 0.1;
                result.snappedY = std::abs(direction.y()) > 0.1;
            }
        }
        return result;
    }
    if (best[0]) {
        result.delta.rx() += best[0]->correction;
        result.snappedX = true;
        take(*best[0]);
    }
    if (best[1]) {
        result.delta.ry() += best[1]->correction;
        result.snappedY = true;
        take(*best[1]);
    }
    // Lines found before the other axis moved: their ends follow the final box.
    const QRectF final = bounds.normalized().translated(result.delta);
    for (QLineF &line : result.lines) {
        if (line.x1() == line.x2()) {
            line.setP1({line.x1(), std::min(line.y1(), final.top())});
            line.setP2({line.x2(), std::max(line.y2(), final.bottom())});
        } else {
            line.setP1({std::min(line.x1(), final.left()), line.y1()});
            line.setP2({std::max(line.x2(), final.right()), line.y2()});
        }
    }
    return result;
}
