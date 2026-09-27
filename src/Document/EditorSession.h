#pragma once
#include "Document/DocumentHistory.h"
#include "Document/PathOperations.h"
#include "Document/ShapeBuilder.h"
#include "Document/VectorDocument.h"
#include "Rendering/CanvasViewport.h"
#include <QObject>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <array>
#include <functional>
#include <optional>
#include <vector>

// The toolbar, top to bottom. Keys follow Illustrator's.
enum class Tool {
    select,          // V
    directSelect,    // A
    pen,             // P
    pencil,          // N
    text,            // T
    line,            // backslash
    rectangle,       // M
    roundedRectangle,
    ellipse,         // L
    polygon,
    star,
    shapeBuilder,    // Shift-M
    scissors,        // C
    rotate,          // R
    scale,           // S
    gradient,        // G
    eyedropper,      // I
    hand,            // H
    zoom,            // Z
};
inline constexpr std::array allTools{Tool::select, Tool::directSelect, Tool::pen, Tool::pencil, Tool::text, Tool::line,
                                     Tool::rectangle, Tool::roundedRectangle, Tool::ellipse, Tool::polygon, Tool::star,
                                     Tool::shapeBuilder, Tool::scissors, Tool::rotate, Tool::scale, Tool::gradient, Tool::eyedropper, Tool::hand, Tool::zoom};
QString rawValue(Tool tool);
// The tool whose rawValue is `raw`.
std::optional<Tool> toolNamed(const QString &raw);
// The tool's name as the toolbar's tooltip shows it.
QString title(Tool tool);
bool isShapeTool(Tool tool);

enum class ArrangeOrder { bringToFront, bringForward, sendBackward, sendToBack };
enum class AlignEdge { left, horizontalCenter, right, top, verticalCenter, bottom };
enum class DistributeAxis { horizontal, vertical };
// Aligns to the selection's bounds (the artboard's with one object), the artboard, or the key object.
enum class AlignTarget { selection, artboard, keyObject };
// Paste: offset from the last paste, where it was copied, or above or below the selection.
enum class PastePosition { offset, inPlace, front, back };

// One document being edited: its objects, selection, tool, style and history.
// Every edit goes through here and ends with `changed()`.
class EditorSession : public QObject {
    Q_OBJECT
public:
    explicit EditorSession(QObject *parent = nullptr);

    // Document ---------------------------------------------------------------
    const std::optional<VectorDocument> &document() const { return m_document; }
    bool hasDocument() const { return m_document.has_value(); }
    // A new blank artboard; history starts over.
    void createDocument(QSizeF size);
    // An opened file; history starts over and the file is clean.
    void loadDocument(VectorDocument document);
    void closeDocument();
    void setArtboardSize(QSizeF size);
    void setArtboardBackground(const QColor &color);

    // Tools and default style ------------------------------------------------
    Tool tool() const { return m_tool; }
    void selectTool(Tool tool);
    // New objects take these; with a selection, the Properties panel edits it instead.
    const Paint &defaultFill() const { return m_defaultFill; }
    const StrokeStyle &defaultStroke() const { return m_defaultStroke; }
    void setDefaultFill(const Paint &fill);
    void setDefaultStroke(const StrokeStyle &stroke);
    // X: fill and stroke trade colours. D: black stroke, white fill.
    void swapFillAndStroke();
    void resetDefaultColors();
    TextContent defaultText;
    double cornerRadius = 12;
    int polygonSides = 6;
    int starPoints = 5;
    // Inner over outer radius.
    double starInnerRatio = 0.5;
    // Shape Builder's tool options.
    ShapeBuilderOptions shapeBuilder;

    // Selection --------------------------------------------------------------
    // Objects directly under a layer, or deeper after a group is entered.
    const std::vector<QUuid> &selection() const { return m_selection; }
    bool isSelected(const QUuid &id) const;
    void select(const std::vector<QUuid> &ids);
    void toggleSelected(const QUuid &id);
    void selectAll();
    void deselectAll();
    // Everything whose bounds meet `rect`, as a marquee drag selects.
    std::vector<QUuid> objectsIn(const QRectF &rect, bool deep) const;
    QRectF selectionBounds(bool includeStroke = false) const;
    // Select menu: every other object, the next sibling up or down, and matches of the first selected leaf.
    void selectInverse();
    void selectAdjacent(bool above);
    void selectSame(SameAttribute attribute);
    void selectObjects(ObjectFilter filter);
    void selectAllOnSameLayers();
    // Runs the last Select menu command again.
    void reselect();
    bool canReselect() const { return bool(m_lastSelect); }
    // Leaf paths, texts and images under the selection.
    std::vector<QUuid> selectedLeaves() const;
    // Direct selection: the anchors picked on each path.
    struct PickedNode {
        QUuid object;
        NodeRef node;
        friend bool operator==(const PickedNode &, const PickedNode &) = default;
    };
    const std::vector<PickedNode> &pickedNodes() const { return m_pickedNodes; }
    void pickNodes(const std::vector<PickedNode> &nodes);
    // The layer new objects go in: the selection's, else the last one picked.
    std::optional<QUuid> activeLayer() const;
    void setActiveLayer(const QUuid &id);
    // Align's key object: one of two or more selected objects that stays put; cleared with the selection.
    std::optional<QUuid> keyObject() const { return m_keyObject; }
    void setKeyObject(std::optional<QUuid> id);

    // Isolation --------------------------------------------------------------
    // The groups entered, outermost first; clicks and new objects stay inside the last.
    const std::vector<QUuid> &isolation() const { return m_isolation; }
    std::optional<QUuid> isolatedGroup() const;
    // Enters `group`, keeping the entered groups that hold it.
    void isolate(const QUuid &group);
    // Back to `depth` groups entered; 0 leaves isolation. The group left stays selected.
    void exitIsolation(int depth = 0);

    // History ----------------------------------------------------------------
    // Edits between begin and end are one undo step, named for the Edit menu.
    void beginEdit(const QString &name);
    void endEdit();
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    QString undoName() const { return m_history.undoName(); }
    QString redoName() const { return m_history.redoName(); }
    void undo();
    void redo();
    bool isModified() const { return m_history.isModified(); }
    // The History panel's rows: steps that undo, oldest first, and steps that redo, next first.
    std::vector<QString> undoNames() const { return m_history.undoNames(); }
    std::vector<QString> redoNames() const { return m_history.redoNames(); }
    // Undoes (negative) or redoes that many steps at once.
    void stepHistory(int steps);
    // How many steps each document keeps; a preference shared by every session.
    static int historyLimit();
    static void setHistoryLimit(int steps);
    void markSaved();
    void markUnsaved();

    // Interactive edits: a drag previews against the objects as they were
    // when it began, and ends in one undo step or none.
    void beginInteraction(const QString &name);
    bool isInteracting() const { return m_interaction.has_value(); }
    // The open interaction's undo name, empty when none is open.
    QString interactionName() const { return m_interaction ? m_interaction->name : QString(); }
    // The selection transformed from where the interaction began.
    void previewTransform(const QTransform &transform, bool reflowAreaText = false);
    // Replaces one object wholesale, for path point drags.
    void previewObject(const VectorObject &object);
    // The object as the interaction found it.
    const VectorObject *originalObject(const QUuid &id) const;
    // Adds or removes an object within the interaction, as drawing tools preview.
    QUuid previewAddObject(VectorObject object);
    void previewRemoveObject(const QUuid &id);
    // Alt-drag: copies the selection in place and selects them; later previews move the copies.
    void previewDuplicateSelection();
    // Replaces the whole document and selection, for edits an agent proposes.
    void previewDocument(const VectorDocument &document, const std::vector<QUuid> &selection);
    void commitInteraction();
    void cancelInteraction();
    // Shape Builder: a gesture on the selection's arrangement, from where the interaction began; results stay selected.
    bool previewShapeBuild(const ShapeBuilder::Arrangement &arrangement, const ShapeBuilder::Gesture &gesture);

    // Objects ----------------------------------------------------------------
    // Adds above the selection (or on top of the active layer) and selects it.
    QUuid addObject(VectorObject object, const QString &editName);
    // A path with the default fill and stroke.
    QUuid addPath(const VectorPath &path, const QString &name);
    QUuid addText(QPointF baselineOrigin, const QString &text);
    // What addPath and addText would add, for tools that preview first.
    VectorObject pathObject(const VectorPath &path, const QString &name) const;
    VectorObject textObject(QPointF baselineOrigin, const QString &text) const;
    // An image placed at its pixel size, centred on `center`.
    QUuid placeImage(const QImage &image, const QString &name, std::optional<QPointF> center = std::nullopt);
    // Replaces an object's fields in one undo step.
    void updateObject(const VectorObject &object, const QString &editName);
    void deleteSelection();
    void duplicateSelection(QPointF offset = {10, 10});
    // Object ▸ Transform ▸ Transform Again: the last move, scale, rotate or reflect, copies too.
    void transformAgain();
    bool canTransformAgain() const { return m_lastTransform.has_value() && hasSelection(); }
    void groupSelection();
    void ungroupSelection();
    // Object ▸ Clipping Mask ▸ Make: the topmost object clips the rest.
    void makeClippingMask();
    void releaseClippingMask();
    void arrange(ArrangeOrder order);
    void align(AlignEdge edge, AlignTarget target = AlignTarget::selection);
    // Distribute: centres along `axis`, or the chosen edge of each object.
    void distribute(DistributeAxis axis);
    void distribute(AlignEdge edge);
    // Distribute Spacing: an exact gap measured from the key object (else the first), or equal gaps when nullopt.
    void distributeSpacing(DistributeAxis axis, std::optional<double> gap);
    void moveSelection(QPointF delta);
    // `reflowAreaText`: an upright scale resizes area type's box, as its handles do.
    void transformSelection(const QTransform &transform, const QString &editName, bool reflowAreaText = false);
    // Each selected object by its own transform, from its bounds; one undo step.
    void transformEach(const std::function<QTransform(const QRectF &bounds)> &transform, const QString &editName, bool reflowAreaText = false);
    // About `pivot`, else the selection's centre.
    void rotateSelection(double degrees, std::optional<QPointF> pivot = std::nullopt);
    void flipSelection(Qt::Orientation orientation);
    void scaleSelection(double sx, double sy);
    // Pathfinder on the selected leaves; the result takes the bottom one's style.
    void combineSelection(BooleanOperation operation);
    // Object ▸ Path ▸ Outline Stroke: strokes become filled paths.
    void outlineSelectedStrokes();
    // Object ▸ Path ▸ Offset Path: a copy grown by `distance`.
    void offsetSelection(double distance);
    void simplifySelection(double tolerance);
    // Object ▸ Image Trace ▸ Make: the one selected image becomes traced paths.
    void traceSelectedImage(int colors = 1);
    std::optional<QUuid> selectedImage() const;
    // Type ▸ Create Outlines.
    void convertTextToPaths();
    // Object ▸ Compound Path ▸ Make / Release.
    void makeCompoundPath();
    void releaseCompoundPath();
    void setFillOfSelection(const Paint &fill);
    void setStrokeOfSelection(const StrokeStyle &stroke);
    void setOpacityOfSelection(double opacity);
    void setBlendModeOfSelection(LayerBlendMode mode);
    // Eyedropper: the clicked object's style onto the selection, or the defaults.
    void pickStyle(const QUuid &from);

    // Paint and appearance ---------------------------------------------------
    // The appearance stack: every selected leaf takes these, bottom to top.
    void setFillsOfSelection(const std::vector<Paint> &fills, const QString &editName);
    void setStrokesOfSelection(const std::vector<StrokeStyle> &strokes, const QString &editName);
    // Distinct colours in the selected leaves' fills, strokes and gradient stops.
    std::vector<QColor> selectionColors() const;
    // Every use of `from` in the selected leaves becomes `to`, in one undo step.
    void replaceColor(const QColor &from, const QColor &to);
    // A global swatch changed: every paint linked to it takes `color`, in one undo step.
    void recolorSwatch(const QString &swatchId, const QColor &color);
    // Edit ▸ Copy Properties / Paste Properties: fills, strokes, opacity, blend,
    // and the character style between texts. One clipboard for every tab.
    bool copyProperties();
    void pasteProperties();
    bool canCopyProperties() const;
    bool canPasteProperties() const;
    // Eyedropper Alt-click: the selection's style, else the defaults, onto `target`.
    void applyStyleTo(const QUuid &target);
    // Direct selection: deletes the picked anchors; empty paths go.
    void deletePickedNodes();
    // Pen tool: joins the picked end anchors of one open contour.
    void closePath(const QUuid &id, int contour);

    // Path editing -----------------------------------------------------------
    // Object ▸ Path ▸ Join: two picked end anchors, else the selected open paths nearest end to nearest end.
    void joinPaths();
    bool canJoin() const;
    // Object ▸ Path ▸ Average: the picked anchors (else every anchor selected) onto one line (Horizontal: one y) or one point.
    void averagePoints(Qt::Orientations along);
    bool canAverage() const;
    // Scissors: cuts a path at an anchor, or on the segment after `from` at `t`. Closed contours open; open ones split in two.
    bool cutPath(const QUuid &id, NodeRef from, std::optional<double> t = std::nullopt);
    // Object ▸ Path ▸ Reverse Path Direction.
    void reversePaths();
    // Selected paths with more than one contour, whose fill rule the Properties panel shows.
    std::vector<QUuid> selectedCompoundPaths() const;
    void setFillRuleOfSelection(Qt::FillRule rule);

    // Live corners -------------------------------------------------------------
    // Selected rectangles that are still live.
    std::vector<QUuid> selectedShapes() const;
    // One corner, or all four with nullopt.
    void setCornerRadius(double radius, std::optional<int> corner = std::nullopt);
    void setCornerStyle(CornerStyle style, std::optional<int> corner = std::nullopt);
    // The object with `shape` and the path it makes.
    static void reshape(VectorObject &object, const LiveRectangle &shape);

    // Type -------------------------------------------------------------------
    // Text objects among the selected leaves.
    std::vector<QUuid> selectedTexts() const;
    // The selected texts' style, else the next text's.
    TextContent shownText() const;
    // Characters selected in type being edited in place: type edits, styles and
    // fills apply to them alone. Without one, or when it's empty, they apply to whole objects.
    struct TextRange {
        QUuid id;
        int from = 0;
        int to = 0;
    };
    const std::optional<TextRange> &textRange() const { return m_textRange; }
    void setTextRange(std::optional<TextRange> range);
    // Each distinct stretch of what type edits would change, as its own text:
    // the Character section shows a field as Mixed when these differ.
    std::vector<TextContent> shownTexts() const;
    // Text styles: made from what's shown, applied to the selection or range,
    // redefined everywhere in one step. Paragraph styles also set their paragraphs' unstyled characters.
    const TextStyle *textStyle(const QUuid &id) const;
    QUuid newTextStyle(TextStyleKind kind, const QString &name = QString());
    void applyTextStyle(const QUuid &id);
    // Detaches from the style of `kind`, keeping the look.
    void clearTextStyle(TextStyleKind kind);
    // Back to the style's own look.
    void clearTextOverrides(TextStyleKind kind);
    void redefineTextStyle(const QUuid &id);
    void renameTextStyle(const QUuid &id, const QString &name);
    void deleteTextStyle(const QUuid &id);
    // The style of `kind` everything shown shares, if one; `overridden` says whether any of it differs from the style.
    std::optional<QUuid> shownTextStyle(TextStyleKind kind, bool *overridden = nullptr) const;
    // Type ▸ Find/Replace Font: families in the document or the selection, those not installed,
    // and every use of one swapped for another, nearest face kept, in one undo step.
    QStringList usedFonts(bool selectionOnly = false) const;
    QStringList missingFonts() const;
    static bool isFontInstalled(const QString &family);
    int replaceFont(const QString &from, const QString &to, bool selectionOnly = false);
    // Selects the texts that use a family.
    void selectTextsUsing(const QString &family);
    // Restyles the selected texts and the next text in one undo step named `name`;
    // `coalesce` folds a held key's repeats into the step before.
    void updateText(const std::function<void(TextContent &)> &change, const QString &name, bool coalesce = false);
    enum class TextStep { tracking, leading, baselineShift, size };
    // Illustrator's type keys: tracking in 1/1000 em, the rest in pt.
    void stepText(TextStep step, double amount);
    // Manual kerning before the character at `index` of the text being edited.
    void kernText(const QUuid &id, int index, double amount);
    // Type ▸ Convert to Area Type / Point Type, keeping the text where it is.
    void convertTextType(bool toArea);
    // Area type's box, in its own units; resizing leaves the glyphs alone.
    void setTextArea(const QUuid &id, std::optional<QSizeF> area);
    // Scale Strokes & Effects: scaling multiplies stroke widths too.
    bool scaleStrokes = false;
    // Scale Corners: kept for live corners; paths always scale their curves.
    bool scaleCorners = true;

    // Layers panel -----------------------------------------------------------
    QUuid addLayer();
    void deleteObjects(const std::vector<QUuid> &ids);
    void rename(const QUuid &id, const QString &name);
    void setVisible(const QUuid &id, bool visible);
    void setLocked(const QUuid &id, bool locked);
    void setExpanded(const QUuid &id, bool expanded);
    // Moves `id` under `parent` at child `index` (bottom-up), one undo step.
    bool moveObject(const QUuid &id, const QUuid &parent, int index);
    // Moves a layer to `index` among the layers (bottom-up), one undo step.
    bool moveLayer(const QUuid &id, int index);
    void lockSelection();
    void unlockAll();
    void hideSelection();
    void showAll();
    // Every sibling of `id` hidden or shown, locked or unlocked, as one step; `id` itself stays open.
    void setOthersVisible(const QUuid &id, bool visible);
    void setOthersLocked(const QUuid &id, bool locked);
    // True when some sibling of `id` is visible, or unlocked.
    bool anyOtherVisible(const QUuid &id) const;
    bool anyOtherUnlocked(const QUuid &id) const;
    void setLayerColor(const QUuid &id, const QColor &color);
    // A copy of the layer and its contents, above it.
    void duplicateLayer(const QUuid &id);

    // Clipboard --------------------------------------------------------------
    void copy() const;
    void cut();
    // Offsets each paste when the clipboard came from this document.
    void paste(bool inPlace = false);
    void paste(PastePosition position);
    bool canPaste() const;

    // View -------------------------------------------------------------------
    CanvasViewport viewport;
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void actualSize();
    // View ▸ Zoom to Selection: the selection's bounds fill the view, with a margin.
    void zoomToSelection();
    void zoomToRect(const QRectF &rect);
    // The canvas's own zoom, pan and size changes.
    void setZoom(double zoom, QPointF anchoredAt);
    void panView(QSizeF by);
    void resizeView(QSizeF size, double backingScale);
    // Smart guides: moves and drawn points snap to objects and the artboard.
    bool usesSmartGuides = true;
    bool showsGrid = false;
    bool snapsToGrid = false;
    double gridSpacing = 10;
    bool showsOutline = false;
    // Snap to Pixel: drawn points and moved bounds land on whole points; the pixel grid shows from 600 %.
    bool snapsToPixel = false;
    bool showsPixelGrid = true;
    void setSnapsToPixel(bool snaps);
    void setShowsPixelGrid(bool shown);
    void setShowsGrid(bool shown);
    void setSnapsToGrid(bool snaps);
    void setShowsOutline(bool shown);
    QPointF snapped(QPointF point) const;

    // Rulers and guides ------------------------------------------------------
    bool showsRulers = false;
    bool showsGuides = true;
    bool guidesLocked = false;
    void setShowsRulers(bool shown);
    void setShowsGuides(bool shown);
    void setGuidesLocked(bool locked);
    void addGuide(const Guide &guide);
    void moveGuide(int index, double position);
    void removeGuide(int index);
    void clearGuides();
    // View ▸ Guides ▸ Make Guides: selected paths become guides (a straight line one, anything else its bounds' edges).
    void makeGuides();
    bool canMakeGuides() const;
    // Release Guides: each guide becomes a line across the artboard.
    void releaseGuides();

    // Gates the menus read.
    bool hasSelection() const { return !m_selection.empty(); }
    bool canGroup() const;
    bool canUngroup() const;
    bool canCombine() const;

signals:
    // Anything changed: document, selection, tool, style, view.
    void changed();
    // The artboard or objects changed; the canvas redraws.
    void documentChanged();

private:
    void notify(bool documentToo = true);
    void edit(const QString &name, const std::function<void(VectorDocument &)> &change);
    void insertNew(VectorDocument &document, VectorObject object);
    void restore(const DocumentHistory::Snapshot &snapshot);
    void pruneSelection();
    std::vector<QUuid> selectionInOrder() const;
    std::optional<QUuid> insertionParent() const;
    std::vector<QUuid> duplicateInto(VectorDocument &document, QPointF offset) const;
    void commitTextEdit(VectorDocument next, const QString &name, bool coalesce);
    // The range within `id` type edits apply to; nullopt is the whole text.
    std::optional<std::pair<int, int>> rangeIn(const QUuid &id) const;
    // Setting the fill with characters selected colours just them.
    bool fillTextRange(const Paint &fill);
    void runSelect(const std::function<void()> &command);
    // Type's look without its words, box or kerning.
    static void applyCharacterStyle(TextContent &to, const TextContent &from);

    std::optional<VectorDocument> m_document;
    DocumentHistory m_history;
    Tool m_tool = Tool::select;
    Paint m_defaultFill = Paint::solid(Qt::white);
    StrokeStyle m_defaultStroke;
    std::vector<QUuid> m_selection;
    std::vector<PickedNode> m_pickedNodes;
    std::optional<QUuid> m_activeLayer;
    std::optional<QUuid> m_keyObject;
    std::vector<QUuid> m_isolation;
    struct Interaction {
        QString name;
        VectorDocument before;
        std::vector<QUuid> selection;
        // What previews start from: `before`, plus any copies made within the interaction.
        VectorDocument base;
        // The last previewTransform, and whether the selection was copied first.
        std::optional<QTransform> transform;
        bool duplicated = false;
    };
    std::optional<Interaction> m_interaction;
    mutable int m_pasteCount = 0;
    // When the last coalescing text step ran.
    qint64 m_lastTextStep = 0;
    std::optional<TextRange> m_textRange;
    // What Transform Again repeats. With a centre, it pivots on the selection's centre as it did there.
    struct RepeatTransform {
        QTransform transform;
        bool duplicate = false;
        std::optional<QPointF> center;
    };
    std::optional<RepeatTransform> m_lastTransform;
    std::function<void()> m_lastSelect;
};
