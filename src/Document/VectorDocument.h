#pragma once
#include "Document/LayerAppearance.h"
#include "Document/Paint.h"
#include "Document/VectorPath.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <cstdint>
#include <optional>
#include <vector>

// Layers are groups at the top of the tree; every other object sits in one.
enum class ObjectKind { layer, group, path, text, image };
QString rawValue(ObjectKind kind);
std::optional<ObjectKind> objectKind(const QString &rawValue);

enum class TextAlignment { left, center, right };
QString rawValue(TextAlignment alignment);
std::optional<TextAlignment> textAlignment(const QString &rawValue);

// Point type: lines start at the object's origin, the first baseline at y 0.
struct TextContent {
    QString text;
    QString family = QStringLiteral("Sans Serif");
    double size = 24;
    bool bold = false;
    bool italic = false;
    TextAlignment alignment = TextAlignment::left;
    // Line spacing as a multiple of the size.
    double leading = 1.2;
    double tracking = 0;

    QFont font() const;
    // The glyph outlines, in the text's own coordinates.
    QPainterPath outline() const;
    friend bool operator==(const TextContent &, const TextContent &) = default;
};

// Select ▸ Same: what an object must share with the one picked.
enum class SameAttribute { fillColor, strokeColor, fillAndStroke, strokeWeight, opacity, blendMode, fontFamily, fontFamilyStyleSize };
// Select ▸ Object: kinds of object picked across the document.
enum class ObjectFilter { textObjects, images, clippingMasks, openPaths, strayPoints };

struct VectorObject {
    QUuid id = QUuid::createUuid();
    ObjectKind kind = ObjectKind::path;
    QString name;
    std::optional<QUuid> parentID;
    bool isVisible = true;
    bool isLocked = false;
    // Groups and layers: their row is open in the Layers panel.
    bool isExpanded = true;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::normal;
    // Layers: the colour of their selection outlines.
    QColor layerColor;

    // Paths: document coordinates. Text and images: local, placed by `transform`.
    VectorPath path;
    Paint fill = Paint::none();
    StrokeStyle stroke;
    TextContent text;
    QImage image;
    QTransform transform;
    // Groups: the first child clips the rest.
    bool isClipGroup = false;

    bool isContainer() const { return kind == ObjectKind::layer || kind == ObjectKind::group; }
    bool hasPaint() const { return kind == ObjectKind::path || kind == ObjectKind::text; }
    // The object's own shape in document coordinates (containers: empty).
    QPainterPath outline() const;
    friend bool operator==(const VectorObject &, const VectorObject &) = default;
};

// The artboard and its objects, bottom to top; children follow their parent's
// order in `objects`, so z-order is the vector's order within each parent.
struct VectorDocument {
    QSizeF size{800, 600};
    // The artboard's paper; transparent exports leave it out.
    QColor background = Qt::white;
    std::vector<VectorObject> objects;

    // A document with one empty layer.
    static VectorDocument blank(QSizeF size);

    const VectorObject *find(const QUuid &id) const;
    VectorObject *find(const QUuid &id);
    int indexOf(const QUuid &id) const;
    // Direct children, bottom to top.
    std::vector<QUuid> children(const std::optional<QUuid> &parent) const;
    std::vector<QUuid> descendants(const QUuid &id) const;
    std::vector<QUuid> layers() const;
    bool isAncestor(const QUuid &ancestor, const QUuid &of) const;
    // The layer an object sits in (a layer is its own).
    std::optional<QUuid> layerOf(const QUuid &id) const;
    // The child of a layer that holds `id`, which a click selects.
    std::optional<QUuid> topLevelObject(const QUuid &id) const;
    // Visible and unlocked, counting every ancestor.
    bool isEffectivelyVisible(const QUuid &id) const;
    bool isEffectivelyLocked(const QUuid &id) const;
    // Groups count their children.
    QRectF bounds(const QUuid &id, bool includeStroke = false) const;
    QRectF bounds(const std::vector<QUuid> &ids, bool includeStroke = false) const;
    // Every object's outline under `id`, for hit tests and boolean operations.
    QPainterPath outline(const QUuid &id) const;
    // The topmost selectable object under `point` within `tolerance`.
    std::optional<QUuid> hitTest(QPointF point, double tolerance) const;
    // Every selectable leaf under `point`, topmost first, up to `limit`.
    std::vector<QUuid> hitTestAll(QPointF point, double tolerance, size_t limit = SIZE_MAX) const;
    // Visible, unlocked leaves that share `attribute` with `like`, `like` included.
    std::vector<QUuid> matching(const QUuid &like, SameAttribute attribute) const;
    std::vector<QUuid> matching(ObjectFilter filter) const;
    // Inserts above `below` within `parent`, or on top of it.
    void insert(VectorObject object, const QUuid &parent, std::optional<QUuid> above = std::nullopt);
    // Removes objects and all their descendants.
    void remove(const std::vector<QUuid> &ids);
    // Moves one object and its subtree under `parent` at child `index`.
    bool move(const QUuid &id, const QUuid &parent, int index);
    // Moves a layer and its contents to `index` among the layers, bottom-up.
    bool moveLayer(const QUuid &id, int index);
    // Applies `transform` to an object and its descendants.
    void transform(const QUuid &id, const QTransform &transform);
    // A copy of an object's subtree with new ids.
    std::vector<VectorObject> copySubtree(const QUuid &id) const;
    QString uniqueName(const QString &base) const;
    friend bool operator==(const VectorDocument &, const VectorDocument &) = default;

private:
    int subtreeEnd(int index) const;
    bool moveUnder(const QUuid &id, const std::optional<QUuid> &parent, int index);
};

// The colour each new layer takes in turn.
QColor nextLayerColor(int index);
