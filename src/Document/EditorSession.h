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
#include <map>
#include <optional>
#include <vector>

class QMimeData;

// The toolbar, top to bottom. Keys follow Illustrator's.
enum class Tool {
    select,          // V
    directSelect,    // A
    pen,             // P
    pencil,          // N
    text,            // T
    typeOnPath,      // Shift-T
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
    width,           // Shift-W
    eyedropper,      // I
    hand,            // H
    zoom,            // Z
    artboard,        // Shift+O; kept last so toolInfo's index stays stable for old code
    frame,           // F: Figma's frame, after the artboard for the same reason
    browserView,     // no key: a frame that shows a web page (docs/BROWSER-VIEW.md)
    browse,          // no key: clicks, keys and the wheel go to a Browser View's page
};
inline constexpr std::array allTools{Tool::select, Tool::directSelect, Tool::pen, Tool::pencil, Tool::text, Tool::typeOnPath, Tool::line,
                                     Tool::rectangle, Tool::roundedRectangle, Tool::ellipse, Tool::polygon, Tool::star,
                                     Tool::shapeBuilder, Tool::scissors, Tool::rotate, Tool::scale, Tool::gradient, Tool::width,
                                     Tool::eyedropper, Tool::hand, Tool::zoom, Tool::artboard, Tool::frame, Tool::browserView, Tool::browse};
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

    // Lock Document (EditorSession+Lock.cpp) ----------------------------------
    // A locked document refuses every edit, undo and redo, whichever door it comes in by:
    // selecting, inspecting, measuring, exporting and sharing go on. The flag is saved in the
    // file, changes no undo step, and marks the document unsaved.
    bool isDocumentLocked() const { return m_document && m_document->locked; }
    void setDocumentLocked(bool locked);
    // What the status line says when an edit is refused.
    static QString lockedNotice();
    // The one gate for edits: true (and `editRefused()`) when the document is locked. Every
    // edit entry point asks it; so does anything that starts editing outside the session, as
    // the canvas's inline type does.
    bool refuseWhenLocked();

    // Browser Views (EditorSession+Browser.cpp) -------------------------------
    // The page moved on its own (a link, a redirect): the file is marked unsaved but
    // no undo step is made (a scroll alone marks nothing), and the recorded steps follow the new address so an undo doesn't
    // send the tab back. Locked documents take it too, since it isn't an edit.
    void setBrowserLocation(const QUuid &frame, const QUrl &url, QPointF scroll);
    // The Browser View tool: a frame over `rect` showing `url` (or "no page yet"), nested as addFrame nests. Selected.
    QUuid addBrowserView(const QRectF &rect, const QUrl &url = QUrl());
    // "Change URL": the address the user typed, one undo step. Undo goes back to the page before.
    void setBrowserUrl(const QUuid &frame, const QUrl &url);
    // A resize preview of a Browser View (docs/BROWSER-VIEW.md, section 6): an interaction that is never recorded. Committing
    // it, or any edit, undo or tool change but Browse, ends it as cancelInteraction does.
    void beginPreview(const QString &name);
    // Shows `frame` at `box`, its children following their constraints (or staying, when fixed). No step is made.
    void previewFrameBox(const QUuid &frame, const QRectF &box);
    bool isPreviewOnly() const { return m_interaction && m_interaction->discard; }
    // What is saved, exported and shared: the document without a held preview's width.
    const VectorDocument &designDocument() const { return m_interaction && m_interaction->discard ? m_interaction->before : *m_document; }
    // The frame's box before the preview began, or its box now.
    QRectF designBox(const QUuid &frame) const;
    // "Design Width": the frame's box becomes `box`, one undo step, as Transform's W does.
    void setDesignBox(const QUuid &frame, const QRectF &box);
    // The width a preview is showing becomes the design width ("Design Width"); nothing when no preview is showing.
    void setPreviewAsDesignWidth();
    // The selected Browser View's children keep their design-width place in the preview ("Fixed while previewing").
    void setFixedWhilePreviewing(bool fixed);
    // The one selected Browser View, for the menu's Browser View commands.
    std::optional<QUuid> selectedBrowserView() const;
    // The last picture, refreshed silently: not unsaved, not a step, no signal.
    void setBrowserPicture(const QUuid &frame, const QImage &picture);

    // Artboards (EditorSession+Artboards.cpp) ---------------------------------
    // The Artboard tool, the list, next/previous and select() all set this.
    int activeArtboard() const;
    void setActiveArtboard(int index);
    // A click on an artboard's name: it becomes the selection, as a frame does, so the Select
    // tool shows its handles. The object selection empties; picking any object ends it.
    void selectArtboard(int index);
    bool artboardSelected() const { return m_artboardSelected && m_selection.empty(); }
    // Placed to the right of the active one with a 20 pt gap unless `rect` is given.
    QUuid addArtboard(QRectF rect = {});
    // To the right of `index` with a 20 pt gap, copying its art.
    QUuid duplicateArtboard(int index);
    void renameArtboard(int index, const QString &name);
    // Off leaves it visible and editable but out of every export, share and page list.
    void setArtboardExported(int index, bool exported);
    // Never the last artboard; its art is untouched.
    void deleteArtboard(int index);
    // A drag: beginInteraction("Move Artboard" or "Resize Artboard"), a preview per
    // move, then commitInteraction. Art whose centre was on it follows when `artboardMovesArt`:
    // a move carries it along, and a resize applies each object's constraints, as a frame's
    // children do (Left and Top by default, so it rides the top left corner).
    void previewArtboardRect(int index, QRectF rect);
    // Object ▸ Artboards ▸ Fit to Artwork Bounds: the art overlapping it, or every
    // visible object when none does, strokes included.
    void fitArtboardToArtwork(int index);
    void switchArtboardOrientation(int index);
    // Activates and zooms to the next or previous artboard, wrapping around.
    void showArtboard(bool next);
    void fitAllArtboards();
    // Export for Screens' asset list.
    void collectForExport(const std::vector<QUuid> &ids);
    void removeFromExport(const std::vector<QUuid> &ids);
    // The Artboard tool's "Move art with artboard" option.
    bool artboardMovesArt = true;

    // Pages (EditorSession+Pages.cpp; docs/PAGES.md) -------------------------------
    QUuid currentPage() const;
    // After the current page, with one layer and one artboard; it becomes current.
    QUuid addPage(const QString &name = {});
    // After the original, "<name> Copy"; the copy becomes current.
    QUuid duplicatePage(const QUuid &id);
    void renamePage(const QUuid &id, const QString &name);
    // Never the last page. Returns whether it went.
    bool deletePage(const QUuid &id);
    void movePage(const QUuid &id, int index);
    // Keeps positions; the view stays and the selection empties.
    void moveSelectionToPage(const QUuid &id);
    // Moves whole layers (or, for a selected object, its layer) with everything on them; one step.
    void moveLayersToPage(const std::vector<QUuid> &layers, const QUuid &id);
    // Not undo steps: they commit an interaction, remember the page's view and restore the next one's.
    // Both do nothing while an AI proposal is open, which only Enter or Esc settles.
    void setCurrentPage(const QUuid &id);
    void showPage(bool next);
    // The name an AI proposal's interaction starts with.
    static QString proposalPrefix() { return QStringLiteral("AI: "); }
    bool isProposalOpen() const { return m_interaction && interactionName().startsWith(proposalPrefix()); }

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
    // Figma's Enter, Shift+Enter and Tab: into the selected groups and frames, out to their parents, and along the siblings (wrapping).
    void selectChildren();
    void selectParent();
    void selectSibling(bool next);
    void selectSame(SameAttribute attribute);
    void selectObjects(ObjectFilter filter);
    void selectAllOnSameLayers();
    // Runs the last Select menu command again.
    void reselect();
    bool canReselect() const { return bool(m_lastSelect); }
    // Leaf paths, texts and images under the selection, and frames (without their children).
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
    // Reads the stored limit again, after settings were imported.
    static void reloadHistoryLimit();
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
    // Into `parent` when given, else where new objects go.
    QUuid previewAddObject(VectorObject object, std::optional<QUuid> parent = std::nullopt);
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
    // Ungroup releases groups and frames alike (a frame's box goes).
    void ungroupSelection();
    // Object ▸ Frame Selection (Ctrl+Alt+G): the selection inside a new frame its size.
    void frameSelection();
    // The Frame tool: a frame over `rect`, inside the innermost frame that holds it. Selected.
    // A preset gives its `name`, kept as is unless an object already has it.
    QUuid addFrame(const QRectF &rect, const QString &name = {});
    // Where a frame of `size` lands when picked from a list: centred in the view, else on the active artboard, on whole points.
    QRectF framePlacement(QSizeF size) const;
    // Shift+A (docs/AUTO-LAYOUT.md): a selected frame without auto layout gets it, its direction, gap and
    // padding read from where its children are; anything else goes into a new auto-layout frame that hugs it.
    void addAutoLayout();
    // Alt+Shift+A: the selected frames lose auto layout; their children stay where they are.
    void removeAutoLayout();
    bool canRemoveAutoLayout() const;
    // The auto layout the selected frames share, for the panel; changing it sets every one's.
    std::optional<AutoLayout> selectedAutoLayout() const;
    void setAutoLayout(const AutoLayout &layout, const QString &editName);
    // Sizing and Absolute position for every selected object.
    void setLayoutSizing(Qt::Orientation axis, LayoutSizing sizing);
    void setAbsolutePosition(bool absolute);
    // Constraints for every selected object (docs/AUTO-LAYOUT.md, Figma's constraints).
    void setConstraint(Qt::Orientation axis, LayoutConstraint constraint);
    // Selected frames, and whether every one clips its content (none selected: false).
    std::vector<QUuid> selectedFrames() const;
    bool selectedFramesClip() const;
    void setClipsContent(bool clips);
    // Object ▸ Clipping Mask ▸ Make: the topmost object clips the rest.
    void makeClippingMask();
    void releaseClippingMask();
    // Object ▸ Opacity Mask ▸ Make (P2-9): the topmost object's luminance masks the rest.
    void makeOpacityMask();
    void releaseOpacityMask();
    // Whether the mask hides what falls outside its own rendered coverage.
    void setOpacityMaskClip(bool clip);
    void setOpacityMaskInverted(bool inverted);
    // The single selected mask group, for the Clip and Invert Mask menu checks.
    std::optional<QUuid> selectedMaskGroup() const;
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
    // Object ▸ Make Pixel Perfect: snaps the selection's anchors, and live rectangles' rects, to whole points.
    void makePixelPerfect();
    bool canMakePixelPerfect() const;
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

    // Type on a Path (P2-3) ---------------------------------------------------
    // Converts a path into text following it, one undo step; nullopt if `id` isn't a path.
    QUuid convertPathToTypeOnPath(const QUuid &id);
    void flipTypeOnPath(const QUuid &id);

    // Object ▸ Text Wrap (P2-4) ------------------------------------------------
    void setTextWrap(const QUuid &id, std::optional<double> offset);

    // Threaded text (P2-4) -----------------------------------------------------
    // Links `from`'s overflow into `to`, an existing area-type box; `to`'s own text, if
    // any, joins the story as a new paragraph. One undo step, "Thread Text".
    void linkThread(const QUuid &from, const QUuid &to);
    // The same, but `to` is a fresh box of `size` at `origin`.
    QUuid linkNewThread(const QUuid &from, QPointF origin, QSizeF size);
    // Splits `id`'s chain so every box keeps just the story it currently shows.
    void removeThreading(const QUuid &id);
    // Scale Strokes & Effects: scaling multiplies stroke widths too.
    bool scaleStrokes = false;
    // Scale Corners: off keeps a live rectangle's radii (clamped to the new rect) as it scales.
    bool scaleCorners = true;

    // Design system: tokens (docs/DESIGN-SYSTEMS.md) ---------------------------
    const DesignToken *token(const QString &id) const;
    // Adds a token (its name made unique) and returns its id.
    QString addToken(DesignToken token);
    // A token's value in `mode` (empty: its own value), and every use of it, in one undo step.
    void setTokenValue(const QString &id, const TokenValue &value, const QString &mode = {});
    void renameToken(const QString &id, const QString &name);
    // What used it keeps its look and loses the link.
    void deleteToken(const QString &id);
    // A pull: tokens merged in by name, their uses updated, in one step named `editName`. Returns how many changed.
    int mergeTokens(const std::vector<DesignToken> &tokens, const QString &editName, const QStringList &modes = {});
    // Modes: every token gets a value for a new one (a copy of its own), and switching restyles every use.
    void addTokenMode(const QString &mode);
    void setTokenMode(const QString &mode);
    // Links the selection to a token and applies it: "fill", "stroke", or a TokenRef key. An empty
    // target picks by kind: fill for a colour, the gap of a group for spacing, corners for a radius,
    // type for type. Returns why it couldn't, or empty.
    QString applyToken(const QString &id, const QString &target = {});
    void unlinkToken(const QString &target);
    // The token the selection's `target` follows, if every selected leaf shares one.
    QString linkedToken(const QString &target) const;
    void linkTextStyle(const QUuid &style, const QString &token);

    // Design system: components ------------------------------------------------
    // The selection, grouped if it's more than one group, becomes a main component.
    std::optional<QUuid> makeComponent(const QString &name = {});
    // An instance of `master`, centred on `center` (else beside the component), selected.
    QUuid placeInstance(const QUuid &master, std::optional<QPointF> center = std::nullopt);
    // Components from a library, each subtree's root a component: variants of a set already in the
    // document are reused, the rest go on a "Components" layer beside the artboard. Then an instance of
    // the best match for `variant` in `set`, centred on `center`. One undo step.
    QUuid placeFromLibrary(const std::vector<VectorObject> &objects, const QString &set, const std::map<QString, QString> &variant,
                           std::optional<QPointF> center = std::nullopt);
    // A copy of `master` beside it as another variant of its set, with `property` set to `value`.
    std::optional<QUuid> addVariant(const QUuid &master, const QString &property, const QString &value);
    void setVariantProperty(const QUuid &master, const QString &property, const QString &value);
    void renameComponent(const QString &set, const QString &name);
    // The selected instances switch to the variant with `property` = `value`, overrides kept.
    QString swapVariant(const QString &property, const QString &value);
    void detachInstances();
    void resetOverrides();
    std::vector<QUuid> selectedInstances() const;
    // The component selected, or the one the first selected instance uses.
    std::optional<QUuid> selectedMaster() const;
    // Instances rebuilt from their components and stale token links dropped; every edit ends with it.
    void settle();

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
    // Set once by the app: another clipboard format, tried before the plain-image
    // fallback (Figma's paste; docs/import/figma.md). `recognises` is cheap and
    // runs on every menu refresh (canPaste); `read` does the full decode, only
    // when pasting, and returns nullopt when it can't read this clipboard.
    struct ExternalPaste {
        std::vector<VectorObject> objects;
        // What the paste couldn't bring along; reported through pasteLeftOut().
        QStringList warnings;
    };
    struct ExternalPasteHandler {
        std::function<bool(const QMimeData &)> recognises;
        std::function<std::optional<ExternalPaste>(const QMimeData &)> read;
    };
    static void setExternalPasteHandler(ExternalPasteHandler handler);

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
    // A paste from another app left some things out.
    void pasteLeftOut(const QStringList &warnings);
    // The page is about to change (a switch, a new or deleted page): inline text finishes on the page it was typed on.
    void aboutToChangePage();
    // The page shown changed: a switch, a new or deleted page, or an undo across pages.
    void currentPageChanged(const QUuid &page);
    // Move to Page finished: the status line says where the objects went.
    void movedToPage(const QString &pageName);
    // An edit met a locked document and did nothing.
    void editRefused();

private:
    bool pageEditRefused();
    void notify(bool documentToo = true);
    void edit(const QString &name, const std::function<void(VectorDocument &)> &change);
    // The shared tail of paste(): renumbers ids, places the objects and selects them.
    void pasteObjects(std::vector<VectorObject> objects, PastePosition position);
    void insertNew(VectorDocument &document, VectorObject object);
    QUuid addFrameObject(VectorObject frame, const QString &step);
    void restore(const DocumentHistory::Snapshot &snapshot);
    // Pages' view memory: what leaving a page keeps, and what entering one restores.
    void rememberPageView();
    bool enterPage();
    bool frameView(const QRectF &rect);
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
    // Not saved; clamped to range whenever it's read.
    int m_activeArtboard = 0;
    bool m_artboardSelected = false;
    // Artboard 1's size the last time notify() ran, to compensate the viewport when it changes.
    QSizeF m_viewportDocumentSize;
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
        // A preview that is never recorded (a Browser View's width).
        bool discard = false;
    };
    std::optional<Interaction> m_interaction;
    struct PageView {
        CanvasViewport viewport;
        int activeArtboard = 0;
        bool artboardSelected = false;
        std::vector<QUuid> selection;
    };
    // Per page, not saved. `m_shownPage` is the page these fields describe.
    std::map<QUuid, PageView> m_pageViews;
    QUuid m_shownPage;
    // Nesting of beginEdit calls, and which of those levels a lock turned away: their endEdit does nothing.
    int m_editDepth = 0;
    std::vector<int> m_refusedEditDepths;
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
