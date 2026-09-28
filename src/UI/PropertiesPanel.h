#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include "UI/PanelIcons.h"
#include "UI/PanelSection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <array>

class CharacterSection;
class ParagraphSection;
class NumberField;
class PaintStack;
class PaintSwatch;
class SelectionColors;
class QListWidget;
class QListWidgetItem;

// A fill or stroke: kind, colour, gradient end. Several different paints read Mixed.
class PaintRow : public QWidget {
    Q_OBJECT
public:
    PaintRow(EditorSession &session, bool stroke, FloatingPanel &picker, QWidget *parent);
    void synchronize();
    // The paint as the kind menu turns it.
    static Paint converted(const Paint &paint, PaintKind kind);

private:
    Paint shown() const;
    void apply(const Paint &paint);
    void pickStop(bool last);

    EditorSession &m_session;
    const bool m_stroke;
    FloatingPanel &m_picker;
    QComboBox *const m_kind;
    PaintSwatch *const m_well;
    PaintSwatch *const m_end;
    // The solid's colour, or a gradient's first stop, as six hex digits.
    QLineEdit *const m_hex;
};

// The right-hand inspector, in Figma's order: the document with nothing
// selected; else transform, character for text, appearance, stroke, align and
// pathfinder. Each section folds away and remembers it.
class PropertiesPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertiesPanel(EditorSession &session, QWidget *parent = nullptr);
    // Whether the Type tool or type being edited shows the Character section with nothing selected.
    void setEditingText(bool editing);

private:
    PanelSection *documentSection();
    PanelSection *transformSection();
    PanelSection *appearanceSection();
    PanelSection *strokeSection();
    PanelSection *alignSection();
    PanelSection *pathfinderSection();
    // Live rectangles' corners and compound paths' fill rule.
    PanelSection *shapeSection();
    void synchronizeShape();
    // Frames and auto layout (docs/AUTO-LAYOUT.md): direction, gap, padding, alignment, sizing, Absolute, Clip content.
    PanelSection *layoutSection();
    void synchronizeLayout();
    // Every selected frame's auto layout, changed by `change`.
    void changeLayout(const QString &editName, const std::function<void(AutoLayout &)> &change);
    // An instance's variant properties, Detach and Reset; a component's name and instances.
    PanelSection *componentSection();
    void synchronizeComponent();
    QToolButton *iconButton(const QString &name, const QString &tip, PanelIcon icon, const std::function<void()> &run);
    // The reference point of the selection's bounds.
    QPointF reference() const;
    void moveTo(double x, double y);
    // Scales so the bounds are `width` × `height`, keeping the reference point still.
    void resizeTo(double width, double height);
    // Per object: "+10" or "*2" on each one's own value.
    void moveEach(bool vertical, const std::function<double(double)> &change);
    void resizeEach(bool vertical, const std::function<double(double)> &change);
    void setStrokeWidth(double width);
    void applyDashes();
    // The stroke the Stroke section edits: the stack's active entry, else the one stroke.
    StrokeStyle shownStroke() const;
    void applyStroke(const StrokeStyle &stroke);
    // Fills and strokes: one row each, or their stacks; selection colours; the stroke's
    // contextual rows (alignment for closed paths, arrowheads for open ones).
    void synchronizePaint();
    void synchronize();
    // Properties ▸ Document's Artboards list: rows, selection and the right-click menu.
    void synchronizeArtboards();
    void artboardsMenu(int index, QPoint at);
    void applyIcons();
    // One height and one font for the section's fields and menus.
    void evenControls();
    void runWindowAction(const QString &name);

protected:
    void changeEvent(QEvent *event) override;

private:
    EditorSession &m_session;
    FloatingPanel m_picker{QStringLiteral("colorPickerPanel"), *this};
    bool m_editingText = false;
    PanelSection *m_document = nullptr;
    PanelSection *m_transform = nullptr;
    CharacterSection *m_character = nullptr;
    ParagraphSection *m_paragraph = nullptr;
    PanelSection *m_appearance = nullptr;
    PanelSection *m_stroke = nullptr;
    PanelSection *m_align = nullptr;
    PanelSection *m_pathfinder = nullptr;
    PanelSection *m_shape = nullptr;
    PanelSection *m_component = nullptr;
    PanelSection *m_layout = nullptr;
    QPushButton *m_layoutAdd = nullptr;
    QWidget *m_layoutRows = nullptr;
    QComboBox *m_layoutFlow = nullptr;
    NumberField *m_layoutGap = nullptr;
    QCheckBox *m_layoutAutoGap = nullptr;
    NumberField *m_layoutPadX = nullptr;
    NumberField *m_layoutPadY = nullptr;
    std::array<QToolButton *, 9> m_layoutAlign{};
    QWidget *m_layoutSizing = nullptr;
    QComboBox *m_layoutWidth = nullptr;
    QComboBox *m_layoutHeight = nullptr;
    QCheckBox *m_layoutAbsolute = nullptr;
    QWidget *m_layoutConstraints = nullptr;
    QComboBox *m_constraintX = nullptr;
    QComboBox *m_constraintY = nullptr;
    QCheckBox *m_layoutClip = nullptr;
    QWidget *m_componentRows = nullptr;
    NumberField *m_radius = nullptr;
    QToolButton *m_cornersLinked = nullptr;
    QWidget *m_corners = nullptr;
    std::array<NumberField *, 4> m_corner{};
    QWidget *m_fillRuleRow = nullptr;
    QComboBox *m_fillRule = nullptr;
    // Distribute Spacing's gap; nullopt spaces evenly.
    std::optional<double> m_spacing;
    NumberField *m_spacingField = nullptr;
    std::optional<QUuid> m_shownKey;
    ReferencePointPicker *m_reference = nullptr;
    NumberField *m_x = nullptr;
    NumberField *m_y = nullptr;
    NumberField *m_width = nullptr;
    NumberField *m_height = nullptr;
    NumberField *m_rotation = nullptr;
    QToolButton *m_link = nullptr;
    // While the rotation label is dragged: the point it turns about.
    std::optional<QPointF> m_rotationPivot;
    QAction *m_scaleStrokes = nullptr;
    QAction *m_scaleCorners = nullptr;
    NumberField *m_artboardWidth = nullptr;
    NumberField *m_artboardHeight = nullptr;
    PaintSwatch *m_background = nullptr;
    QListWidget *m_artboards = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_snap = nullptr;
    QCheckBox *m_outline = nullptr;
    NumberField *m_increment = nullptr;
    PaintRow *m_fill = nullptr;
    PaintRow *m_strokePaint = nullptr;
    QWidget *m_fillLine = nullptr;
    QWidget *m_strokeLine = nullptr;
    PaintStack *m_fillStack = nullptr;
    PaintStack *m_strokeStack = nullptr;
    SelectionColors *m_selectionColors = nullptr;
    QComboBox *m_strokeAlign = nullptr;
    QLabel *m_strokeAlignCaption = nullptr;
    QWidget *m_arrows = nullptr;
    QComboBox *m_startArrow = nullptr;
    QComboBox *m_endArrow = nullptr;
    NumberField *m_arrowScale = nullptr;
    QCheckBox *m_alignDashes = nullptr;
    NumberField *m_strokeWidth = nullptr;
    QComboBox *m_cap = nullptr;
    QComboBox *m_join = nullptr;
    // Width tool (P2-7): the profile a preset or a hand-dragged point set makes.
    QComboBox *m_widthProfile = nullptr;
    QLineEdit *m_dashes = nullptr;
    QComboBox *m_alignTarget = nullptr;
    std::vector<std::pair<QToolButton *, PanelIcon>> m_iconButtons;
    std::vector<QToolButton *> m_alignButtons;
    std::vector<QToolButton *> m_distributeButtons;
    std::vector<QToolButton *> m_spacingButtons;
    std::vector<QToolButton *> m_pathfinderButtons;
};
