#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include "UI/PanelIcons.h"
#include "UI/PanelSection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>

class CharacterSection;
class ParagraphSection;
class NumberField;
class PaintSwatch;

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
    void synchronize();
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
    NumberField *m_artboardWidth = nullptr;
    NumberField *m_artboardHeight = nullptr;
    PaintSwatch *m_background = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_snap = nullptr;
    QCheckBox *m_outline = nullptr;
    NumberField *m_increment = nullptr;
    PaintRow *m_fill = nullptr;
    PaintRow *m_strokePaint = nullptr;
    NumberField *m_strokeWidth = nullptr;
    QComboBox *m_cap = nullptr;
    QComboBox *m_join = nullptr;
    QLineEdit *m_dashes = nullptr;
    QComboBox *m_alignTarget = nullptr;
    std::vector<std::pair<QToolButton *, PanelIcon>> m_iconButtons;
    std::vector<QToolButton *> m_alignButtons;
    std::vector<QToolButton *> m_distributeButtons;
    std::vector<QToolButton *> m_pathfinderButtons;
};
