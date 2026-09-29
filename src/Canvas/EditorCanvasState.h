#pragma once
#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Canvas/InlineTextEditor.h"
#include "Canvas/SmartGuides.h"
#include <QCursor>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QLineF>
#include <QPointer>
#include <QTimer>
#include <memory>

class QLineEdit;

class Rulers;

// The canvas's tools and gestures; each EditorCanvas+Part.cpp holds one group.
struct EditorCanvas::State {
    State(EditorCanvas &canvas, EditorSession &session);

    EditorCanvas &canvas;
    EditorSession &session;

    // Geometry --------------------------------------------------------------
    QSizeF documentSize() const;
    // View points per document unit.
    double scale() const;
    QPointF toDocument(QPointF view) const;
    QPointF toView(QPointF document) const;
    QTransform documentToView() const;
    // A distance in view points, in document units.
    double reach(double points) const { return points / std::max(scale(), 1e-9); }
    void syncViewport();

    // Drags ------------------------------------------------------------------
    enum class DragKind {
        pan,
        zoomRect,
        marquee,
        move,
        scale,
        rotate,
        scaleTool,
        nodes,
        handle,
        shape,
        pencil,
        pen,
        convert,
        textSelect,
        // Type tool: a drag draws an area type box.
        textArea,
        shapeBuilder,
        // Gradient tool: an end, a stop, or a new start-to-end drag.
        gradient,
        // Width tool: an existing point, or a new one dropped where the drag started.
        width,
        // A ruler guide moved, or one drawn out of a ruler.
        guide,
        // Direct Selection: a live corner's widget.
        corner,
        // A selected path text's start bracket.
        pathBracket,
        // The Artboard tool, or the Select tool on a selected artboard: drawing a new one, moving
        // or resizing one, or moving the fresh copy an Alt-drag made.
        artboard,
        // The Browse tool: the button is down in a Browser View's page (`object`).
        browse,
    };
    struct Drag {
        DragKind kind = DragKind::pan;
        QPointF pressView;
        QPointF pressDocument;
        QPointF lastView;
        // Past the drag distance: a click that never got there changes nothing.
        bool started = false;
        bool additive = false;
        bool duplicate = false;
        QRectF startBounds;
        int handle = -1;
        QPointF center;
        QUuid object;
        NodeRef node;
        NodePart part = NodePart::anchor;
        QPointF grabbed;
        std::vector<QPointF> points;
        SmartGuides guides;
        std::vector<QUuid> selectionBefore;
        std::vector<EditorSession::PickedNode> pickedBefore;
        // Shapes and the pen: the interaction this drag opened.
        bool interacting = false;
        // Guides: which one (-1 for a new one), its axis and where it would land.
        int guide = -1;
        Qt::Orientation guideAxis = Qt::Horizontal;
        double guidePosition = 0;
        // The Artboard tool: which one is being moved or resized, known at the press (an
        // implicit artboard's id isn't stable enough to look up again once the drag starts).
        int artboardIndex = -1;
        // A click on what was already selected: it becomes the key object if nothing moves.
        std::optional<QUuid> keyCandidate;
        // Width tool: the point's position along the path (0..1), fixed at press.
        double pathT = 0;
        // A handle drag on a Browser View is a preview of its width, never a step.
        std::optional<QUuid> previewFrame;
    };
    std::optional<Drag> drag;
    // A fresh drag of `kind` pressed at `view`.
    Drag &beginDrag(DragKind kind, QPointF view);
    std::vector<QLineF> guideLines;
    std::vector<QLineF> guideGaps;
    // Drawing tools show where the next click would snap; targets freeze until the document changes.
    std::optional<SmartGuides> hoverGuides;
    void updateHoverGuides(std::optional<QPointF> view);

    void press(QPointF view, Qt::KeyboardModifiers modifiers);
    void move(QPointF view, Qt::KeyboardModifiers modifiers, bool held);
    void release(QPointF view, Qt::KeyboardModifiers modifiers);
    void doubleClick(QPointF view, Qt::KeyboardModifiers modifiers);
    // Escape or lost focus: a drag's changes go back.
    void cancelDrag();
    bool pastDragDistance(QPointF view) const;
    void toolChanged();
    void documentChanged();

    // Snapping ---------------------------------------------------------------
    SmartGuides guidesExcluding(const std::vector<QUuid> &excluded, const QUuid &excludedBoard = {}) const;
    // Smart guides, then the grid on axes they left alone; shows the guides.
    QPointF snapPoint(const SmartGuides &guides, QPointF point, std::optional<QPointF> anchor = std::nullopt, bool constrained = false);
    QPointF snapMovement(const SmartGuides &guides, const QRectF &bounds, QPointF delta, bool constrained);
    void clearGuides();

    // Selection (V), Rotate (R), Scale (S) -----------------------------------
    std::optional<QUuid> hovered;
    // What a click on `leaf` selects: a layer's child, or one inside the isolated group.
    std::optional<QUuid> selectableTarget(const QUuid &leaf) const;
    std::optional<QUuid> hitLeaf(QPointF document) const;
    // The select tool's box in document coordinates, while it shows.
    std::optional<QRectF> selectionBox() const;
    QPointF handlePoint(const QRectF &box, int index) const;
    // A flat box's side handles would sit on its corners.
    bool handleShown(const QRectF &box, int index) const;
    std::optional<int> handleAt(QPointF view) const;
    bool inRotateZone(QPointF view) const;
    void selectPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void transformToolPress(QPointF view);
    void dragMove(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragScale(QPointF view, Qt::KeyboardModifiers modifiers);
    // A handle drag's scale of `box` about its fixed point: snapped, Shift proportional, Alt from the centre.
    QTransform handleScale(const QRectF &box, int handle, QPointF view, Qt::KeyboardModifiers modifiers);
    void dragRotate(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragScaleTool(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragMarquee(QPointF view);
    void finishMarquee();
    void updateHover(QPointF view);

    // Rulers and guides --------------------------------------------------------
    // The guide under `view` a drag would take, unless guides are hidden or locked.
    std::optional<int> guideAt(QPointF view) const;
    void guidePress(int index, QPointF view);
    // A ruler began a drag: a new guide follows the pointer until release.
    void beginRulerGuide(Qt::Orientation axis, QPointF view);
    void dragGuide(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishGuide(QPointF view);
    // Double-click: type the guide's position.
    void editGuide(int index);
    void drawGuides(QPainter &painter) const;
    void drawPixelGrid(QPainter &painter, const QRectF &artboard) const;
    // Isolation: everything but the group, faded.
    void drawIsolated(QPainter &painter, const VectorDocument &document, const QUuid &group) const;

    // Live corners (Direct Selection) ----------------------------------------------
    struct CornerWidget {
        QUuid object;
        int corner = 0;
        QPointF view;
    };
    std::vector<CornerWidget> cornerWidgets() const;
    std::optional<CornerWidget> cornerWidgetAt(QPointF view) const;
    void cornerPress(const CornerWidget &widget, QPointF view);
    void dragCorner(QPointF view, Qt::KeyboardModifiers modifiers);
    // A click without a drag: Alt cycles round, inverted and chamfer.
    void finishCorner(Qt::KeyboardModifiers modifiers);

    // Artboard (Shift-O) --------------------------------------------------------
    // The active artboard's rect in document coordinates, while there's a document.
    std::optional<QRectF> activeArtboardBox() const;
    // A resize handle of the active artboard under `view`, only under the Artboard tool.
    std::optional<int> artboardHandleAt(QPointF view) const;
    void artboardPress(QPointF view, Qt::KeyboardModifiers modifiers);
    // A resize or move drag of artboard `index`, its snap targets frozen (its own art moves with it, so isn't one).
    SmartGuides artboardGuides(int index) const;
    void beginArtboardResize(int index, int handle, QPointF view);
    void beginArtboardMove(int index, QPointF view, Qt::KeyboardModifiers modifiers);
    // Names above artboards, hit-tested for the Select tool: a click selects the artboard, a drag moves it.
    std::vector<std::pair<int, QRectF>> artboardLabels() const;
    std::optional<int> artboardLabelAt(QPointF view) const;
    void dragArtboard(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishArtboard();
    void drawArtboardTool(QPainter &painter) const;
    // Every artboard's name, drawn above its top-left corner.
    void drawArtboardLabels(QPainter &painter) const;
    // Top-level frames' names above their corner, as Figma shows them; a click on one selects its frame.
    std::vector<std::pair<QUuid, QRectF>> frameLabels() const;
    void drawFrameLabels(QPainter &painter) const;
    // A Browser View's line over the frame, as "Paused by reset" (EditorCanvas+Browser.cpp).
    void drawBrowserMessages(QPainter &painter) const;
    BrowserViewHost *browserHost = nullptr;
    // A Browser View's address bar and sign-in strip (EditorCanvas+BrowserBar.cpp), laid out in view pixels.
    struct BrowserBarLayout {
        QUuid frame;
        QRectF bar, back, forward, reload, name, address, tag;
        // The breakpoint buttons and the width each previews, ascending; the design width is among them.
        std::vector<std::pair<QRectF, int>> widths;
        int designWidth = 0;
        bool collapsed = false;
    };
    struct SignInStrip {
        QRectF strip, signIn, notNow;
    };
    std::vector<BrowserBarLayout> browserBars() const;
    void drawBrowserBars(QPainter &painter) const;
    std::optional<QUuid> browserBarAt(QPointF view) const;
    QString browserBarTip(QPointF view) const;
    // True when the press was the bar's or the strip's.
    bool browserBarPress(QPointF view);
    bool browserBarMenu(QPointF view, QPoint global);
    void openAddressEditor(const QUuid &frame);
    void closeAddressEditor();
    std::optional<SignInStrip> signInStrip() const;
    void drawSignInStrip(QPainter &painter) const;
    bool signInPress(QPointF view);
    // The breakpoint buttons (EditorCanvas+Breakpoints.cpp) ---------------------------------
    struct HeldPreview {
        QUuid frame;
        int width = 0;
    };
    // The width a button holds a frame at, until the same button, the design-width button, Esc, a new press or a tool change.
    std::optional<HeldPreview> held;
    // The width the frame is showing at while a preview is (held, or the handle drag's), rounded; nothing otherwise.
    std::optional<int> previewedWidth(const QUuid &frame) const;
    bool showsWidths(const QUuid &frame) const;
    // Lays the buttons out to the left of `right`, and answers where the rest of the bar now ends.
    double addWidthButtons(BrowserBarLayout &layout, double right, double left, const QFontMetricsF &metrics) const;
    void holdPreview(const QUuid &frame, int width);
    // False when nothing was held.
    bool endHeldPreview();
    void setDesignWidth(const QUuid &frame, int width);
    void drawWidthButtons(QPainter &painter, const BrowserBarLayout &layout) const;
    QPointer<QLineEdit> addressEdit;
    QUuid addressFrame;
    std::optional<QUuid> frameLabelAt(QPointF view) const;

    // Browse (EditorCanvas+Browse.cpp) -------------------------------------------------
    // The topmost Browser View under the point, and the box its page fills, in document units.
    std::optional<QUuid> browseFrameAt(QPointF view) const;
    QRectF browseBox(const QUuid &frame) const;
    void browsePress(QPointF view, Qt::KeyboardModifiers modifiers, bool doubleClick);
    void browseMove(QPointF view, Qt::KeyboardModifiers modifiers, bool held);
    void browseRelease(QPointF view, Qt::KeyboardModifiers modifiers);
    bool browseWheel(QWheelEvent *event);
    // True when the page took the key (or Browse claims it), so it goes no further.
    bool browseKey(QKeyEvent *event, bool down);
    bool browseInput(QInputMethodEvent *event);
    // Whether keys go to a page: a click has put focus in one.
    bool browseFocused() const { return session.tool() == Tool::browse && browseFocus.has_value(); }
    // Browse ended or lost its page: the pressed button comes up and the page forgets the pointer.
    void browseLeave();
    void browseSend(const QUuid &frame, const QString &type, QPointF view, Qt::MouseButton button, Qt::MouseButtons buttons, int clicks,
                    Qt::KeyboardModifiers modifiers);
    void setBrowseFocus(const std::optional<QUuid> &frame);
    std::optional<QUuid> browseFocus;
    std::optional<QUuid> browseHover;
    QElapsedTimer browseClickClock;
    QElapsedTimer browseMoveClock;
    QPointF browseClickView;
    int browseClicks = 0;

    // Scissors (C) ------------------------------------------------------------------
    void scissorsPress(QPointF view);

    // Direct selection (A) ----------------------------------------------------
    // Paths whose anchors show: the selected leaves.
    std::vector<QUuid> editablePaths() const;
    bool isPicked(const QUuid &object, NodeRef node) const;
    struct NodeHit {
        QUuid object;
        NodeRef node;
        NodePart part;
    };
    std::optional<NodeHit> nodeAt(QPointF view) const;
    void directPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragNodes(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragHandle(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishDirectMarquee();

    // Pen (P) and Pencil (N) --------------------------------------------------
    struct Pen {
        QUuid object;
        // The contour being drawn; new anchors go on its end.
        int contour = 0;
        // Clicked on the first anchor: the path closes on release.
        bool closing = false;
        // Resumed from its first anchor: the contour runs backwards until the pen finishes.
        bool reversed = false;
    };
    std::optional<Pen> pen;
    // What a pen click at a point would do, for the press and the cursor.
    enum class PenAction { draw, close, resume, join, convert, removeAnchor, addAnchor };
    struct PenTarget {
        PenAction action = PenAction::draw;
        QUuid object;
        NodeRef node;
        double t = 0;
    };
    PenTarget penTargetAt(QPointF view, Qt::KeyboardModifiers modifiers) const;
    // An open contour's end anchor near `view` on a path a click may continue.
    std::optional<PenTarget> penEndpointAt(QPointF view, const std::vector<QUuid> &paths) const;
    const Contour *penContour() const;
    bool nearPenStart(QPointF view) const;
    void penPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void resumePen(const PenTarget &target, QPointF view);
    void joinPen(const PenTarget &target);
    void convertPress(const PenTarget &target, QPointF view);
    void dragConvert(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragPenHandle(QPointF view, Qt::KeyboardModifiers modifiers);
    void penRelease();
    void finishPen();
    void pencilPress(QPointF view);
    void dragPencil(QPointF view);
    void finishPencil();

    // Shapes ------------------------------------------------------------------
    void shapePress(QPointF view);
    void dragShape(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragFrame(const QRectF &rect);
    void finishBrowserView();
    VectorPath shapePath(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers) const;

    // Shape Builder (Shift-M) -------------------------------------------------
    // The selection's regions, kept until the document, selection or options change.
    struct Built {
        std::vector<QUuid> leaves;
        ShapeBuilderOptions options;
        ShapeBuilder::Arrangement arrangement;
    };
    mutable std::optional<Built> built;
    const ShapeBuilder::Arrangement &arrangement() const;
    // What the pointer is over, and what the drag has touched so far.
    std::optional<int> builderRegion;
    std::optional<int> builderEdge;
    ShapeBuilder::Gesture building;
    void builderPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragBuilder(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishBuilder(Qt::KeyboardModifiers modifiers);
    void updateBuilderHover(std::optional<QPointF> view);
    // Merging with Shift draws a marquee; the regions it touches, so far.
    ShapeBuilder::Gesture builderTouched() const;
    void drawBuilder(QPainter &painter) const;

    // Type (T) ----------------------------------------------------------------
    std::unique_ptr<InlineTextEditor> text;
    // The text as editing began, to keep a hand-given name.
    QString textAtStart;
    bool textCreated = false;
    bool applyingText = false;
    QTimer caretBlink;
    bool caretShown = true;
    // The last double-click in type: a click soon after at the same spot selects the line.
    QElapsedTimer sinceDoubleClick;
    QPointF doubleClickView;
    void textPress(QPointF view);
    // A click makes point type where it was pressed; a drag makes area type.
    void finishTextArea();
    void beginTextEditing(const VectorObject &object, bool inDocument, std::optional<QPointF> caretAt);
    void applyText();
    void finishText();
    // The edited text's box in document coordinates, for clicks inside it.
    QRectF textBox() const;
    void restartCaret();

    // Type on a Path (Shift-T) -------------------------------------------------
    // A click on a path converts it; a click on existing type edits it.
    void typeOnPathPress(QPointF view);
    // Where its start bracket sits, and the path's tangent there, in the text's own coordinates.
    std::optional<std::pair<QPointF, QPointF>> pathBracketPoint(const VectorObject &object) const;
    // The selected path text whose bracket is under `view`, within a few pixels.
    std::optional<QUuid> pathBracketAt(QPointF view) const;
    void dragPathBracket(QPointF view);

    // Threaded text: in/out ports on selected area-type boxes ------------------
    // The out port arms link mode; the next click on another area box, or empty
    // canvas, links it. Cleared by Escape or picking a different tool.
    std::optional<QUuid> linkArmedFrom;
    QPointF outPortAt(const VectorObject &object) const;
    QPointF inPortAt(const VectorObject &object) const;
    std::optional<QUuid> outPortHitAt(QPointF view) const;
    // Handles a click while link mode is armed; returns true if it did.
    bool threadLinkPress(QPointF view);

    // Navigation --------------------------------------------------------------
    // View ▸ Rulers, over the canvas's top and left edges.
    Rulers *rulers = nullptr;
    void syncRulers();
    bool spaceHeld = false;
    void zoomAt(double zoom, QPointF view);
    void finishZoom(QPointF view, Qt::KeyboardModifiers modifiers);

    // Eyedropper: a click takes a style; Alt-click gives the selection's to what it hits.
    void eyedropperPress(QPointF view, Qt::KeyboardModifiers modifiers);

    // Gradient (G) --------------------------------------------------------------
    // The one painted leaf the annotator edits: the first selected.
    std::optional<QUuid> gradientTarget() const;
    // Which handle is under `view`: -1 the start, -2 the end, else a stop's index.
    std::optional<int> gradientHandleAt(QPointF view) const;
    void gradientPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragGradient(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishGradient();
    void drawGradient(QPainter &painter) const;

    // Width (Shift-W) -----------------------------------------------------------
    // The point (last selected leaf with a visible stroke) the annotator edits.
    std::optional<QUuid> widthTarget() const;
    // An existing width point's index near `view`, if any.
    std::optional<int> widthHandleAt(QPointF view) const;
    // The object and index a Delete press would remove, once set by a click or drag.
    std::optional<QUuid> widthPointObject;
    std::optional<int> widthPointIndex;
    void widthPress(QPointF view, Qt::KeyboardModifiers modifiers);
    void dragWidth(QPointF view, Qt::KeyboardModifiers modifiers);
    void finishWidth();
    void drawWidth(QPainter &painter) const;
    // Removes `widthPointObject`'s point at `widthPointIndex`, as its own undo step.
    bool deleteWidthPoint();

    // Measuring and readouts ----------------------------------------------------
    // Alt held over something else: the gaps from the selection to it, or to the artboard.
    std::optional<QRectF> measureTarget() const;
    std::vector<QLineF> measureLines() const;
    QString readout() const;
    void drawMeasurements(QPainter &painter) const;
    void drawReadout(QPainter &painter) const;
    void drawLabel(QPainter &painter, QPointF center, const QString &label) const;

    // Keyboard ------------------------------------------------------------------
    // Digits set opacity; a second digit soon after makes a two-digit value.
    int opacityDigit = -1;
    QElapsedTimer sinceDigit;
    QTimer opacityCommit;
    void typeOpacity(int digit);
    void finishOpacity();
    // Arrows move by the keyboard increment; Alt moves a copy.
    bool nudge(QKeyEvent *event);

    // Painting ----------------------------------------------------------------
    void paint(QPainter &painter);
    void drawGrid(QPainter &painter, const QRectF &artboard) const;
    void drawOverlay(QPainter &painter) const;
    void drawSelection(QPainter &painter) const;
    void drawDirectSelection(QPainter &painter) const;
    void drawPen(QPainter &painter) const;
    QColor layerColor(const QUuid &id) const;
    QColor accent() const;

    // Cursors and keys --------------------------------------------------------
    std::optional<QPointF> hover;
    Qt::KeyboardModifiers modifiers;
    Tool shownTool = Tool::select;
    int cursorKey = -1;
    void updateCursor();
    bool keyPress(QKeyEvent *event);
    bool enterSelection(bool toParent);
    bool keyRelease(QKeyEvent *event);
};
