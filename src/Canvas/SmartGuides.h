#pragma once
#include "Document/VectorDocument.h"
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

// Smart guides: moving bounds and drawn points snap to other objects' edges and
// centres, the artboard's, and equal spacing between neighbours.
// Ported from omadesign by Michael C Hurley, MIT (src/snap.rs).
class SmartGuides {
public:
    // Screen points within which a guide takes hold.
    static constexpr double threshold = 6;

    struct Result {
        QPointF delta;
        // Alignment lines and equal-spacing gaps, in document coordinates.
        std::vector<QLineF> lines;
        std::vector<QLineF> gaps;
        bool snappedX = false;
        bool snappedY = false;
    };

    SmartGuides() = default;
    // Targets freeze when a drag begins: every visible object but `excluded` and
    // their subtrees, so the moving objects never snap to themselves.
    SmartGuides(const VectorDocument &document, const std::vector<QUuid> &excluded);

    // `bounds` moved by `delta`, pulled onto the nearest target on each axis.
    // `scale` is view points per document unit; `constrained` keeps 45° steps.
    Result movement(const QRectF &bounds, QPointF delta, double scale, bool constrained) const;
    // One point; with an anchor, the motion from it (Shift constrains that).
    Result point(QPointF point, double scale, std::optional<QPointF> anchor = std::nullopt, bool constrained = false) const;

    const std::vector<QRectF> &objects() const { return m_objects; }

    // Alt-hover measuring: a line per gap between `from` and `to` on each axis, as long as the gap.
    static std::vector<QLineF> distances(const QRectF &from, const QRectF &to);
    // A distance as the labels print it: at most two decimals, no trailing zeros.
    static QString label(double points);

private:
    std::vector<QRectF> m_objects;
    std::vector<QRectF> m_boards;
};

// The delta turned to the nearest 45° and projected onto it.
QPointF constrain45(QPointF delta);
