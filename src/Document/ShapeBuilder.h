#pragma once
#include "Document/Paint.h"
#include "Document/VectorDocument.h"
#include <QPainterPath>
#include <QUuid>
#include <array>
#include <optional>
#include <vector>

// Shape Builder's settings, as Illustrator's Shape Builder Tool Options.
struct ShapeBuilderOptions {
    // Open paths whose ends are this close count as closed, and open ends reach this far to cut.
    bool gapDetection = false;
    double gapLength = 3;
    // Merge mode: a click on an open path's edge splits the path there.
    bool clickingStrokeSplits = false;
    // Artwork: a merge takes the style of the object the drag began in. Otherwise the current fill and stroke.
    bool colorFromArtwork = true;
    // Freeform touches what the pointer passes over; a straight line what lies between the press and the pointer.
    bool freeformSelection = true;
    // The hovered region shows a mesh fill; its outline and edges highlight.
    bool highlightFill = true;
    bool highlightStroke = true;
    friend bool operator==(const ShapeBuilderOptions &, const ShapeBuilderOptions &) = default;
};

namespace ShapeBuilder {
// One face of the arrangement: a connected area no selected outline crosses.
struct Region {
    // Flattened, odd-even filled: Qt's boolean operations give polygons.
    QPainterPath area;
    // The closed paths that cover it, as indices into `sources`, bottom to top.
    std::vector<int> owners;
    double size = 0;
};

// A piece of an open path between the outlines it crosses.
struct Edge {
    int source = 0;
    int contour = 0;
    int piece = 0;
    Contour path;
};

struct Arrangement {
    // The selected paths, bottom to top.
    std::vector<QUuid> sources;
    std::vector<Region> regions;
    std::vector<Edge> edges;
    // Too many paths or faces to build with: nothing is offered.
    bool truncated = false;
    // Every source segment as a cubic, to turn flattened results back into curves.
    std::vector<std::array<QPointF, 4>> segments;

    std::optional<int> regionAt(QPointF point) const;
    std::optional<int> edgeAt(QPointF point, double tolerance) const;
    // Appends what a pointer moving from `from` to `to` passes over, first touched first.
    void touchAlong(QPointF from, QPointF to, double tolerance, std::vector<int> &regions, std::vector<int> &edges) const;
    // What a Shift-drag marquee touches.
    void touchIn(const QRectF &rect, std::vector<int> &regions, std::vector<int> &edges) const;
    // A flattened area as a path whose pieces follow the sources' own curves.
    VectorPath shape(const QPainterPath &area) const;
};

// Caps that keep a drag responsive; past them the arrangement is truncated.
inline constexpr int maximumSources = 24;
inline constexpr int maximumRegions = 400;

// A contour that bounds regions: closed, or open with its ends within the gap length.
bool bindsArea(const Contour &contour, const ShapeBuilderOptions &options);

// The paths among `leaves` Shape Builder works on: visible, unlocked, not empty.
std::vector<QUuid> sourcesOf(const VectorDocument &document, const std::vector<QUuid> &leaves);
Arrangement arrange(const VectorDocument &document, const std::vector<QUuid> &leaves, const ShapeBuilderOptions &options);

struct Gesture {
    // Touched regions; the first is where the drag began, which styles a merge.
    std::vector<int> regions;
    std::vector<int> edges;
    // Alt: touched regions and edges are deleted.
    bool erase = false;
    // A press and release without a drag.
    bool click = false;
};

// Rebuilds the touched objects in `document`: a merge unites the touched regions into one path,
// an erase removes them; the other regions of those objects become separate paths with their own style.
// `fill` and `stroke` style a merge when colour comes from swatches. False when nothing changes.
bool build(VectorDocument &document, const Arrangement &arrangement, const Gesture &gesture, const ShapeBuilderOptions &options,
           const Paint &fill, const StrokeStyle &stroke, std::vector<QUuid> *created = nullptr);
}
