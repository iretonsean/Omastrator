#pragma once
#include "Document/Components.h"
#include "Document/DesignTokens.h"
#include "Document/LayerAppearance.h"
#include "Document/Paint.h"
#include "Document/VectorPath.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QUuid>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// Layers are groups at the top of the tree; every other object sits in one.
// A frame is a container with a box of its own (Figma's frame): fills and strokes,
// corner radii, and its children clipped to it unless clipping is off.
enum class ObjectKind { layer, group, path, text, image, frame };
QString rawValue(ObjectKind kind);
std::optional<ObjectKind> objectKind(const QString &rawValue);

// Justify leaves a paragraph's last line flush left, centred or right; justifyAll stretches it too.
enum class TextAlignment { left, center, right, justify, justifyAll, justifyCenter, justifyRight };
QString rawValue(TextAlignment alignment);
std::optional<TextAlignment> textAlignment(const QString &rawValue);
bool isJustified(TextAlignment alignment);

// Metrics uses the font's kern pairs; none sets every pair to 0.
enum class TextKerning { metrics, none };
QString rawValue(TextKerning kerning);
std::optional<TextKerning> textKerning(const QString &rawValue);

enum class TextCase { normal, allCaps, smallCaps };
QString rawValue(TextCase textCase);
std::optional<TextCase> textCase(const QString &rawValue);

// How characters look. A text object's own format covers every character a run doesn't.
struct CharacterFormat {
    QString family = QStringLiteral("Sans Serif");
    // A face from QFontDatabase::styles(family), such as "Bold Italic".
    QString style = QStringLiteral("Regular");
    double size = 24;
    // In 1/1000 em, added after every character.
    double tracking = 0;
    // pt, positive is up.
    double baselineShift = 0;
    TextCase textCase = TextCase::normal;
    bool underline = false;
    bool strikethrough = false;
    // OpenType features by tag ("liga", "ss01"): 0 is off, 1 on; a tag left out follows the font.
    std::map<QString, int> features;
    // A run's own colour; unset, the characters take the object's fill.
    std::optional<QColor> fill;
    // The character style these characters were given, if any.
    QUuid characterStyle;

    bool isBold() const;
    bool isItalic() const;
    // The font at `pixelsPerPoint`, as layout uses it.
    QFont font(double pixelsPerPoint, TextKerning kerning = TextKerning::metrics) const;
    friend bool operator==(const CharacterFormat &, const CharacterFormat &) = default;
};

// How a paragraph sits: alignment, leading, indents and spacing.
struct ParagraphFormat {
    TextAlignment alignment = TextAlignment::left;
    // Baseline to baseline in pt; nullopt is Auto, 120 % of the line's largest size.
    std::optional<double> leading;
    // Indents and spacing in pt; a negative first-line indent hangs.
    double leftIndent = 0;
    double rightIndent = 0;
    double firstLineIndent = 0;
    double spaceBefore = 0;
    double spaceAfter = 0;
    // The paragraph style these paragraphs were given, if any.
    QUuid paragraphStyle;
    // Automatic hyphenation (Hyphenator), and its margins: a word shorter than
    // hyphenMinWord, or a break within hyphenMinBefore/hyphenMinAfter letters of
    // either end, is left whole. A soft hyphen the designer typed always works,
    // whether or not this is on.
    bool hyphenate = false;
    int hyphenMinWord = 6;
    int hyphenMinBefore = 2;
    int hyphenMinAfter = 3;
    friend bool operator==(const ParagraphFormat &, const ParagraphFormat &) = default;
};

// Characters start..start+length formatted apart from their object.
struct TextRun {
    int start = 0;
    int length = 0;
    CharacterFormat format;
    friend bool operator==(const TextRun &, const TextRun &) = default;
};

// A named format kept in the document. A paragraph style carries a character
// format too, which its paragraphs' unstyled characters take.
enum class TextStyleKind { character, paragraph };
struct TextStyle {
    QUuid id = QUuid::createUuid();
    QString name;
    TextStyleKind kind = TextStyleKind::character;
    CharacterFormat character;
    ParagraphFormat paragraph;
    // A type token the style follows, if any.
    QString typeToken;
    friend bool operator==(const TextStyle &, const TextStyle &) = default;
};

struct TextContent;

// Type on a Path: `path` is in the text's own (local) coordinates. `start` is 0..1
// along the path's own direction; flipping reverses the path and keeps the visible
// start in place by reading the other way (effective start 1 - start).
struct TextPath {
    VectorPath path;
    double start = 0;
    bool flipped = false;
    friend bool operator==(const TextPath &, const TextPath &) = default;
};

// One area-type box the flow lays rows into, in the order text reaches it.
struct TextFrame {
    QSizeF size;
    // Wrap objects' bounds grown by their offset, in this frame's own coordinates.
    std::vector<QRectF> exclusions;
    // Frame-local to document.
    QTransform transform;
    friend bool operator==(const TextFrame &, const TextFrame &) = default;
};

// Derived by VectorDocument::reflowText(), never saved: what a threaded or
// wrap-avoiding text object lays out into. Every box in one thread shares `story`
// (the head's own content) and `frames`; each keeps its own `frame` index, the
// slice of the story it shows.
struct TextFlow {
    std::shared_ptr<const TextContent> story;
    QUuid head;
    std::vector<TextFrame> frames;
    int frame = 0;
    // The story compares by value: two flows over equal text are equal. Defined after
    // TextContent, below, since comparing *story needs its complete type.
    friend bool operator==(const TextFlow &a, const TextFlow &b);
};

// Point type: lines start at the object's origin, the first baseline at y 0.
// Area type: text wraps inside `area`, whose top-left is the origin.
// The object's own character and paragraph formats cover whatever `runs` and
// `paragraphFormats` leave alone.
struct TextContent : CharacterFormat, ParagraphFormat {
    QString text;
    TextKerning kerning = TextKerning::metrics;
    // Manual kerning in 1/1000 em, keyed by the index of the character it moves.
    std::map<int, double> kerns;
    // Percent.
    double horizontalScale = 100;
    double verticalScale = 100;
    // Area type's box; a height of 0 grows with the text.
    std::optional<QSizeF> area;
    // Ranges formatted apart from the object's own format: in order, apart and never empty.
    std::vector<TextRun> runs;
    // Paragraphs formatted apart from the object's own format, by paragraph index.
    std::map<int, ParagraphFormat> paragraphFormats;
    // Type on a Path (P2-3): set by the Type on a Path tool, converting a path.
    std::optional<TextPath> onPath;
    // Threaded text (P2-4): the area-type box this one's overflow continues into.
    QUuid threadNext;
    // Set by VectorDocument::reflowText(); not part of the saved document.
    TextFlow flow;

    CharacterFormat &character() { return *this; }
    const CharacterFormat &character() const { return *this; }
    ParagraphFormat &paragraph() { return *this; }
    const ParagraphFormat &paragraph() const { return *this; }
    double effectiveLeading() const { return leading.value_or(size * 1.2); }
    // The family's face nearest a weight and slant.
    static QString styleFor(const QString &family, int weight, bool italic);
    // The object's own format as a font, at its whole-pixel design size.
    QFont font() const;
    // The glyph outlines, in the text's own coordinates.
    QPainterPath outline() const;
    // The same, split by the colour runs give them; nullopt is the object's fill.
    std::vector<std::pair<std::optional<QColor>, QPainterPath>> fills() const;
    // Area type with a fixed height: lines past it are hidden.
    bool overflows() const;
    // Area type's box, else the glyphs' bounds.
    QRectF frame() const;
    // Keeps manual kerns on their characters when `from`..`to` becomes `length` characters.
    void replaceKerns(int from, int to, int length);

    // The format at a character; the end of the text reads the last one's.
    CharacterFormat formatAt(int index) const;
    int paragraphCount() const;
    int paragraphOf(int position) const;
    // A paragraph's first character.
    int paragraphStart(int paragraph) const;
    ParagraphFormat paragraphAt(int paragraph) const;
    // Replaces `from`..`to` with `with`; runs, kerns and paragraphs stay with their characters.
    // What's typed takes the format of what it replaces, else of the character before.
    void replace(int from, int to, const QString &with);
    // Changes the format of characters `from`..`to`, run by run.
    void formatCharacters(int from, int to, const std::function<void(CharacterFormat &)> &change);
    // Changes paragraphs `first`..`last`, inclusive, one by one.
    void formatParagraphs(int first, int last, const std::function<void(ParagraphFormat &)> &change);
    // Runs merged where equal, dropped where the object's own format says the same.
    void normalize();
    // The object as each distinct stretch of `from`..`to` sees it: that
    // stretch's character and paragraph formats in place of the object's own.
    std::vector<TextContent> facets(int from, int to) const;
    // Every family used, the object's own first.
    QStringList families() const;
    friend bool operator==(const TextContent &, const TextContent &) = default;
};

inline bool operator==(const TextFlow &a, const TextFlow &b)
{
    const bool sameStory = (a.story == b.story) || (a.story && b.story && *a.story == *b.story);
    return sameStory && a.head == b.head && a.frames == b.frames && a.frame == b.frame;
}

// Live Corners: how a rectangle's corner turns.
enum class CornerStyle { round, inverted, chamfer };
QString rawValue(CornerStyle style);
std::optional<CornerStyle> cornerStyle(const QString &rawValue);

// A rectangle kept live: its path is rebuilt from these until an anchor is edited.
// Corners run top left, top right, bottom right, bottom left in the shape's own frame.
struct LiveRectangle {
    QRectF rect;
    // Rotation, reflection and translation only: the shape's frame in document coordinates.
    QTransform placement;
    std::array<double, 4> radii{};
    std::array<CornerStyle, 4> styles{CornerStyle::round, CornerStyle::round, CornerStyle::round, CornerStyle::round};

    // Radii past half the shorter side are clamped when drawn.
    double effectiveRadius(int corner) const;
    VectorPath path() const;
    // The corner's point and its inward diagonal (unit), in document coordinates.
    QPointF corner(int corner) const;
    QPointF inward(int corner) const;
    // The same shape after `transform`; nullopt when it's no longer a rectangle.
    std::optional<LiveRectangle> transformed(const QTransform &transform, bool scaleCorners = true) const;
    friend bool operator==(const LiveRectangle &, const LiveRectangle &) = default;
};

// A ruler guide: a horizontal one runs along y = position, a vertical one along x.
struct Guide {
    Qt::Orientation orientation = Qt::Horizontal;
    double position = 0;
    // The page it belongs to; null is the first (docs/PAGES.md).
    QUuid page = QUuid();
    friend bool operator==(const Guide &, const Guide &) = default;
};

// A page on the canvas, exported on its own (Illustrator's artboards, Figma's frames).
struct Artboard {
    QUuid id = QUuid::createUuid();
    QString name;
    QRectF rect;
    // Its paper; transparent exports leave it out.
    QColor background = Qt::white;
    // The page it sits on; null is the first.
    QUuid page = QUuid();
    friend bool operator==(const Artboard &, const Artboard &) = default;
};

// A page is its own canvas of layers, artboards and guides (docs/PAGES.md). Its id never changes;
// its name is unique in the document.
struct Page {
    QUuid id = QUuid::createUuid();
    QString name;
    friend bool operator==(const Page &, const Page &) = default;
};

// Select ▸ Same: what an object must share with the one picked.
enum class SameAttribute { fillColor, strokeColor, fillAndStroke, strokeWeight, opacity, blendMode, fontFamily, fontFamilyStyleSize };
// Select ▸ Object: kinds of object picked across the document.
enum class ObjectFilter { textObjects, images, clippingMasks, openPaths, strayPoints };

// Auto layout (docs/AUTO-LAYOUT.md): a frame that places its children in a row or
// column, with a gap, padding and alignment, and can size itself to them.
enum class LayoutDirection { horizontal, vertical };
enum class LayoutAlign { start, center, end };
// Fixed keeps a size; hug takes the content's (auto-layout frames, text); fill takes
// the room an auto-layout parent leaves.
enum class LayoutSizing { fixed, hug, fill };
QString rawValue(LayoutDirection direction);
QString rawValue(LayoutAlign align);
QString rawValue(LayoutSizing sizing);
std::optional<LayoutDirection> layoutDirection(const QString &rawValue);
std::optional<LayoutAlign> layoutAlign(const QString &rawValue);
std::optional<LayoutSizing> layoutSizing(const QString &rawValue);

struct AutoLayout {
    LayoutDirection direction = LayoutDirection::horizontal;
    double gap = 10;
    // Figma's Auto spacing: the free room goes between the items instead of `gap`.
    bool spaceBetween = false;
    double paddingLeft = 10;
    double paddingTop = 10;
    double paddingRight = 10;
    double paddingBottom = 10;
    // Along the direction, and across it.
    LayoutAlign primary = LayoutAlign::start;
    LayoutAlign counter = LayoutAlign::start;
    // Horizontal only: items run on into new rows, `counterGap` apart.
    bool wrap = false;
    double counterGap = 10;
    friend bool operator==(const AutoLayout &, const AutoLayout &) = default;
};

// Constraints: what a child keeps when its frame is resized. Start is the left or
// top edge, end the right or bottom, both keeps both (it stretches), center its
// centre, scale its place and size in proportion.
enum class LayoutConstraint { start, end, both, center, scale };
QString rawValue(LayoutConstraint constraint);
std::optional<LayoutConstraint> layoutConstraint(const QString &rawValue);

// How an object sizes itself, and whether an auto-layout parent flows it.
struct LayoutItem {
    LayoutSizing width = LayoutSizing::fixed;
    LayoutSizing height = LayoutSizing::fixed;
    // Absolute position: kept where it is, out of the flow.
    bool absolute = false;
    // Outside a flow (a plain frame, or Absolute), how it follows its frame's resize.
    LayoutConstraint horizontal = LayoutConstraint::start;
    LayoutConstraint vertical = LayoutConstraint::start;
    friend bool operator==(const LayoutItem &, const LayoutItem &) = default;
};

// Groups (P2-9): the top child's luminance masks the rest. Clip hides whatever
// falls outside the mask's own rendered coverage; off, that area stays visible.
struct OpacityMask {
    bool clip = true;
    bool inverted = false;
    friend bool operator==(const OpacityMask &, const OpacityMask &) = default;
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
    // Layers: the page they're on; null is the first. Children are on their layer's page.
    QUuid page;

    // Paths: document coordinates. Text and images: local, placed by `transform`.
    VectorPath path;
    Paint fill = Paint::none();
    StrokeStyle stroke;
    // The appearance stack past one fill and one stroke, bottom to top: extra fills
    // draw over `fill`, extra strokes over `stroke`, and every fill under every stroke.
    std::vector<Paint> extraFills;
    std::vector<StrokeStyle> extraStrokes;
    TextContent text;
    QImage image;
    QTransform transform;
    // Groups: the first child clips the rest.
    bool isClipGroup = false;
    // Groups: set makes this an opacity mask group (P2-9); the top child is the mask.
    std::optional<OpacityMask> mask;
    // Rectangles: the live shape, while the path is still what it makes. Frames: their
    // box, always set, with `path` kept to what it makes.
    std::optional<LiveRectangle> shape;
    // Frames: children show only inside the box.
    bool clipsContent = true;
    // Frames: auto layout, when on.
    std::optional<AutoLayout> autoLayout;
    // Its own sizing, and its place in an auto-layout parent's flow.
    LayoutItem layout;
    // Lifted objects: where they came from (a page element's CSS selector, an app widget's accessible path), for
    // applying changes back to the source.
    QString liftedFrom;
    // Object ▸ Text Wrap ▸ Make: area type below this in paint order flows around its
    // bounds grown by this offset (pt), when it sits above the text's own layer stack.
    std::optional<double> textWrap;
    // Scalar properties bound to design tokens, by TokenRef key: {"radius": id}.
    std::map<QString, QString> tokenRefs;
    // Groups: a main component, or an instance of one.
    std::optional<ComponentInfo> component;
    std::optional<InstanceInfo> instance;

    bool isContainer() const { return kind == ObjectKind::layer || kind == ObjectKind::group || kind == ObjectKind::frame; }
    bool hasPaint() const { return kind == ObjectKind::path || kind == ObjectKind::text || kind == ObjectKind::frame; }
    // A frame around `rect`, in document coordinates, with a white fill as Figma's new frames have.
    static VectorObject frame(const QRectF &rect, const QString &name = QStringLiteral("Frame"));
    // The whole stack, bottom to top; setting an empty list leaves one none.
    std::vector<Paint> fills() const;
    std::vector<StrokeStyle> strokes() const;
    void setFills(std::vector<Paint> fills);
    void setStrokes(std::vector<StrokeStyle> strokes);
    bool hasVisibleFill() const;
    bool hasVisibleStroke() const;
    // One fill and one plain stroke, each shown at full strength: what a single QPainterPath draw covers.
    bool hasSimpleAppearance() const;
    // Fills, strokes, opacity and blend from `other`.
    void copyAppearance(const VectorObject &other);
    // The live rectangle, when its path hasn't been edited since.
    const LiveRectangle *liveShape() const;
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
    std::vector<Guide> guides;
    // Character and paragraph styles, in the order they were made.
    std::vector<TextStyle> textStyles;
    // Every artboard in order; empty is one at the origin. The first one's size and
    // paper are always `size` and `background`, so code that knows one page still works.
    std::vector<Artboard> artboards;
    // Export for Screens: objects collected as assets, in the order they were added.
    std::vector<QUuid> exportAssets;
    // The design system's tokens, its modes ("light", "dark"; the first is each token's
    // own value) and the mode shown. No modes: empty.
    std::vector<DesignToken> tokens;
    QStringList tokenModes;
    QString tokenMode;
    // Pages in order; empty is one implicit page, "Page 1" (docs/PAGES.md).
    std::vector<Page> pages;
    // The page shown; null, or one that names no page, is the first. View state, not an undo step.
    QUuid currentPage;

    // A document with one empty layer.
    static VectorDocument blank(QSizeF size);

    const VectorObject *find(const QUuid &id) const;
    VectorObject *find(const QUuid &id);
    int indexOf(const QUuid &id) const;
    // Direct children, bottom to top.
    std::vector<QUuid> children(const std::optional<QUuid> &parent) const;
    std::vector<QUuid> descendants(const QUuid &id) const;
    // The current page's layers; allLayers() is every page's.
    std::vector<QUuid> layers() const;
    std::vector<QUuid> allLayers() const;
    bool isAncestor(const QUuid &ancestor, const QUuid &of) const;
    // The layer an object sits in (a layer is its own).
    std::optional<QUuid> layerOf(const QUuid &id) const;
    // The child of a layer that holds `id`.
    std::optional<QUuid> topLevelObject(const QUuid &id) const;
    // What a click on `id` selects: the top-level object, except that a top-level frame
    // lets the click through to its own child that holds `id`, as Figma's do.
    std::optional<QUuid> selectableObject(const QUuid &id) const;
    // Every frame above `id` that clips its content, nearest first.
    std::vector<QUuid> clippingFrames(const QUuid &id) const;
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
    // Visible, unlocked leaves on the current page that share `attribute` with `like`, `like` included.
    std::vector<QUuid> matching(const QUuid &like, SameAttribute attribute) const;
    std::vector<QUuid> matching(ObjectFilter filter) const;
    // Inserts above `below` within `parent`, or on top of it. A layer with no page takes the current one.
    void insert(VectorObject object, const QUuid &parent, std::optional<QUuid> above = std::nullopt);
    // Adds a layer at the top of the current page's stack (or of the whole stack when it has no pages).
    void appendLayer(VectorObject layer);
    // Removes objects and all their descendants.
    void remove(const std::vector<QUuid> &ids);
    // Moves one object and its subtree under `parent` at child `index`.
    bool move(const QUuid &id, const QUuid &parent, int index);
    // Moves a layer and its contents to `index` among its page's layers, bottom-up.
    bool moveLayer(const QUuid &id, int index);
    // Applies `transform` to an object and its descendants. With Scale Corners off, a live
    // rectangle's radii stay put (clamped to the new rect); on, they scale with it.
    void transform(const QUuid &id, const QTransform &transform, bool scaleCorners = true);
    // The same, with stroke widths scaled along (Scale Strokes & Effects) or kept. With
    // `reflowAreaText`, an upright scale resizes area type's box and leaves its glyphs alone.
    void transform(const QUuid &id, const QTransform &transform, bool scaleStrokes, bool reflowAreaText, bool scaleCorners = true);
    // A copy of an object's subtree with new ids.
    std::vector<VectorObject> copySubtree(const QUuid &id) const;
    // `ids`, their descendants, and the layers, groups and clip masks above them, moved so
    // their bounds' corner is the origin; background cleared, artboards and export assets too.
    VectorDocument croppedTo(const std::vector<QUuid> &ids) const;
    QString uniqueName(const QString &base) const;
    // Rectangles whose anchors were edited become plain paths.
    void expandEditedShapes();
    // Lays out every auto-layout frame, innermost first, until nothing moves
    // (docs/AUTO-LAYOUT.md). Rotated frames are left as they are.
    void applyAutoLayout();
    // Figma's resize: the frame's box to `box`, its children outside a flow moved and sized by their
    // constraints (and theirs, in child frames, in turn). Sized by hand, a hugging frame becomes fixed.
    void resizeFrame(const QUuid &id, const QRectF &box);
    // The same for loose art: each of `ids` follows its constraints as the box `old` becomes `fresh`
    // (an artboard's art, whose box isn't an object). Left and Top, the default, follow the top left.
    void constrainToBox(const std::vector<QUuid> &ids, const QRectF &old, const QRectF &fresh);
    // Fills every text's `flow` (P2-4): wrap objects above area type in paint order
    // become exclusions, and threadNext chains become frames sharing one story. A
    // no-op, clearing any stale flow, when nothing wraps or threads.
    void reflowText();

    // Pages (VectorDocument+Pages.cpp) --------------------------------------------
    // The id of the implicit page a document without `pages` has.
    static QUuid implicitPageId();
    // The pages as listed, or the implicit "Page 1".
    std::vector<Page> allPages() const;
    int pageCount() const { return pages.empty() ? 1 : int(pages.size()); }
    QUuid firstPageId() const;
    // The page shown: `currentPage` when it names one, else the first.
    QUuid currentPageId() const;
    // A page tag as it reads: null, or one naming no page, is the first page.
    QUuid resolvePage(const QUuid &tag) const;
    // Index among allPages(), -1 for none.
    int pageIndex(const QUuid &id) const;
    // The page an object is on (its layer's); null for no such object.
    QUuid pageOf(const QUuid &id) const;
    bool isOnCurrentPage(const QUuid &id) const;
    bool isOnCurrentPage(const Guide &guide) const { return resolvePage(guide.page) == currentPageId(); }
    std::vector<Guide> guidesOnCurrentPage() const;
    // The size the viewport centres on: the current page's first artboard (`size` with one page).
    QSizeF viewSize() const { return artboard(0).rect.size(); }
    // The layers of one page, bottom to top.
    std::vector<QUuid> layersOn(const QUuid &page) const;
    // `base` if no page has it, else `base 2`, `base 3`...
    QString uniquePageName(const QString &base) const;
    // Fresh copies of these layers and everything under them, in document order, for Duplicate Page.
    // Parents and text threads are remapped among the copies; a component copied becomes an
    // instance of its original, and an instance keeps its main.
    std::vector<VectorObject> copyLayers(const std::vector<QUuid> &layers) const;
    // Makes the implicit page and artboard explicit, and stamps every untagged (or stray) layer,
    // artboard and guide with its page, so reordering pages can't move art. Also fixes `currentPage`.
    void ensurePages();

    // Artboards (VectorDocument+Artboards.cpp) ----------------------------------
    // The current page's artboards as listed, or the one `size` makes, named "Artboard 1".
    // Every artboard call below counts and indexes the current page's.
    std::vector<Artboard> allArtboards() const;
    std::vector<Artboard> artboardsOn(const QUuid &page) const;
    int artboardCount() const;
    Artboard artboard(int index) const;
    // Replaces the current page's artboards; the first one listed on the first artboard's
    // slot sets `size` and `background`. Empty leaves one.
    void setArtboards(std::vector<Artboard> boards);
    // The artboard under `point`, the last listed first; -1 over none.
    int artboardAt(QPointF point) const;
    int artboardIndex(const QUuid &id) const;
    // Every artboard's rect together.
    QRectF artboardBounds() const;
    // The objects directly in layers that belong to an artboard: those whose bounds,
    // strokes included, meet it. With one artboard, every one of them.
    std::vector<QUuid> objectsOn(int index) const;
    // A layer's children whose centre lies in `rect`: the art an artboard carries when it moves or resizes.
    std::vector<QUuid> artCenteredIn(const QRectF &rect) const;
    // One artboard as a document of its own: its art moved so its corner is the
    // origin, and with several artboards, the art on none of the others' alone.
    VectorDocument artboardDocument(int index) const;
    // "Artboard 3": the first number no artboard uses yet.
    QString uniqueArtboardName(const QString &base = QStringLiteral("Artboard")) const;
    friend bool operator==(const VectorDocument &, const VectorDocument &) = default;

private:
    Artboard implicitArtboard() const;
    int subtreeEnd(int index) const;
    bool moveUnder(const QUuid &id, const std::optional<QUuid> &parent, int index);
};

// A text style redefined: characters and paragraphs that still match `old` take `fresh`.
void restyleText(TextContent &text, const TextStyle &old, const TextStyle &fresh);

// The colour each new layer takes in turn.
QColor nextLayerColor(int index);
