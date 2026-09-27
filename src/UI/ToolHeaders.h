#pragma once
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QFontComboBox>
#include <QToolButton>
#include <array>

class NumberField;
class QCheckBox;
class QComboBox;
class PaintSwatch;

namespace ToolHeaders {
// The bar a tool shows above the canvas.
ToolHeaderBar *make(EditorSession &session, Tool tool, QWidget *parent);
// Tools that share a bar share this.
Tool family(Tool tool);
}

// Shape tools: fill, stroke weight and each shape's own numbers.
class ShapeControls : public ToolHeaderBar {
    Q_OBJECT
public:
    ShapeControls(EditorSession &session, Tool tool, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
    PaintSwatch *const m_fill;
    PaintSwatch *const m_stroke;
    NumberField *const m_weight;
    NumberField *const m_radius;
    NumberField *const m_sides;
    NumberField *const m_points;
    NumberField *const m_inner;
};

// Type: family, size, bold, italic and alignment.
class TypeControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit TypeControls(EditorSession &session, QWidget *parent = nullptr);
    // The selected text's style, else the next text's.
    TextContent shownText() const;
    // Applies to selected texts and to the next one.
    void change(const std::function<void(TextContent &)> &edit);

protected:
    void changeEvent(QEvent *event) override;

private:
    QToolButton *toggle(const QString &name, const QString &text, const QString &tip);
    void applyGlyphs();
    void synchronize();

    EditorSession &m_session;
    QFontComboBox *const m_family;
    NumberField *const m_size;
    QToolButton *const m_bold;
    QToolButton *const m_italic;
    std::array<QToolButton *, 3> m_alignments;
};

// Shape Builder: Illustrator's tool options, in the bar instead of a dialog.
class ShapeBuilderControls : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit ShapeBuilderControls(EditorSession &session, QWidget *parent = nullptr);

private:
    void change(const std::function<void(ShapeBuilderOptions &)> &edit);
    void synchronize();

    EditorSession &m_session;
    QCheckBox *const m_gaps;
    NumberField *const m_gapLength;
    QCheckBox *const m_strokeSplits;
    QComboBox *const m_colorFrom;
    QComboBox *const m_selection;
    QCheckBox *const m_highlightFill;
    QCheckBox *const m_highlightStroke;
};

// Hand and Zoom: the zoom percentage.
class NavigationToolHeader : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit NavigationToolHeader(EditorSession &session, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
    NumberField *const m_zoom;
};

// Rotate and Scale: a typed amount applied to the selection.
class TransformToolHeader : public ToolHeaderBar {
    Q_OBJECT
public:
    TransformToolHeader(EditorSession &session, Tool tool, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
    NumberField *const m_amount;
    QToolButton *const m_apply;
};
