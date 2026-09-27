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
#include <map>
#include <optional>
#include <vector>

// Layers are groups at the top of the tree; every other object sits in one.
enum class ObjectKind { layer, group, path, text, image };
QString rawValue(ObjectKind kind);
std::optional<ObjectKind> objectKind(const QString &rawValue);

// Justify leaves a paragraph's last line flush left; justifyAll stretches it too.
enum class TextAlignment { left, center, right, justify, justifyAll };
QString rawValue(TextAlignment alignment);
std::optional<TextAlignment> textAlignment(const QString &rawValue);

// Metrics uses the font's kern pairs; none sets every pair to 0.
enum class TextKerning { metrics, none };
QString rawValue(TextKerning kerning);
std::optional<TextKerning> textKerning(const QString &rawValue);

enum class TextCase { normal, allCaps, smallCaps };
QString rawValue(TextCase textCase);
std::optional<TextCase> textCase(const QString &rawValue);

// Point type: lines start at the object's origin, the first baseline at y 0.
// Area type: text wraps inside `area`, whose top-left is the origin.
struct TextContent {
    QString text;
    QString family = QStringLiteral("Sans Serif");
    // A face from QFontDatabase::styles(family), such as "Bold Italic".
    QString style = QStringLiteral("Regular");
    double size = 24;
    TextAlignment alignment = TextAlignment::left;
    // Baseline to baseline in pt; nullopt is Auto, 120 % of the size.
    std::optional<double> leading;
    // In 1/1000 em, added after every character.
    double tracking = 0;
    TextKerning kerning = TextKerning::metrics;
    // Manual kerning in 1/1000 em, keyed by the index of the character it moves.
    std::map<int, double> kerns;
    // Percent.
    double horizontalScale = 100;
    double verticalScale = 100;
    // pt, positive is up.
    double baselineShift = 0;
    TextCase textCase = TextCase::normal;
    bool underline = false;
    bool strikethrough = false;
    // Paragraph indents and spacing in pt; a negative first-line indent hangs.
    double leftIndent = 0;
    double rightIndent = 0;
    double firstLineIndent = 0;
    double spaceBefore = 0;
    double spaceAfter = 0;
    // Area type's box; a height of 0 grows with the text.
    std::optional<QSizeF> area;

    double effectiveLeading() const { return leading.value_or(size * 1.2); }
    bool isBold() const;
    bool isItalic() const;
    // The family's face nearest a weight and slant.
    static QString styleFor(const QString &family, int weight, bool italic);
    QFont font() const;
    // The glyph outlines, in the text's own coordinates.
    QPainterPath outline() const;
    // Area type with a fixed height: lines past it are hidden.
    bool overflows() const;
    // Area type's box, else the glyphs' bounds.
    QRectF frame() const;
    // Keeps manual kerns on their characters when `from`..`to` becomes `length` characters.
    void replaceKerns(int from, int to, int length);
    friend bool operator==(const TextContent &, const TextContent &) = default;
};

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
    // The same, with stroke widths scaled along (Scale Strokes & Effects) or kept.
    void transform(const QUuid &id, const QTransform &transform, bool scaleStrokes);
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
