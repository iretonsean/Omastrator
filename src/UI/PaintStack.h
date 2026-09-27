#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include <QLineEdit>
#include <QWidget>
#include <optional>
#include <vector>

class NumberField;
class PaintSwatch;
class QToolButton;
class QVBoxLayout;

// "#ff6600", "ff6600" and "#f60" as colours; anything else is nothing.
namespace HexColor {
std::optional<QColor> parse(const QString &text);
// Six upper-case digits without the #, as Figma shows them.
QString format(const QColor &color);
}

// Several fills or strokes on the selection, as Figma stacks them: one row per
// entry, the top of the stack first, each with an eye, a well, its hex, opacity
// or weight, and −. Rows drag by their grip to reorder; a right-click sets the
// blend mode. Every change is one undo step.
class PaintStack : public QWidget {
    Q_OBJECT
public:
    PaintStack(EditorSession &session, bool strokes, FloatingPanel &picker, QWidget *parent);

    // The stack every selected object shares, bottom to top (fills ride in `paint`);
    // with nothing selected, the default. Nullopt when the objects differ.
    static std::optional<std::vector<StrokeStyle>> shared(const EditorSession &session, bool strokes);
    // More than one entry, or one with its own opacity, blend or eye: the stack shows instead of the one row.
    static bool isStacked(const std::vector<StrokeStyle> &entries);

    void synchronize();
    // Stack indexes, bottom to top.
    void addEntry();
    void removeEntry(int index);
    void moveEntry(int from, int to);
    void setEntryHidden(int index, bool hidden);
    void setEntryOpacity(int index, double opacity);
    void setEntryBlendMode(int index, LayerBlendMode mode);
    void setEntryColor(int index, const QColor &color);
    // The stroke the Stroke section edits; the bottom one until a row is clicked.
    int activeIndex() const;
    void setActive(int index);
    const std::vector<StrokeStyle> &entries() const { return m_entries; }

signals:
    void activeChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void apply(std::vector<StrokeStyle> entries, const QString &name);
    void rebuild();
    void refresh();
    // A row per entry, rebuilt only when the count changes so a scrub keeps its field.
    struct Row {
        QWidget *widget = nullptr;
        QWidget *grip = nullptr;
        QToolButton *eye = nullptr;
        PaintSwatch *well = nullptr;
        QLineEdit *hex = nullptr;
        NumberField *value = nullptr;
    };
    Row makeRow(int index);
    int rowAt(int y) const;

    EditorSession &m_session;
    const bool m_strokes;
    FloatingPanel &m_picker;
    QVBoxLayout *const m_rows;
    std::vector<StrokeStyle> m_entries;
    std::vector<Row> m_rowParts;
    int m_active = 0;
    // A grip drag: the entry it holds.
    std::optional<int> m_dragging;
};

// Figma's Selection colors: every colour in a mixed selection, one chip each.
// Picking a new colour for a chip recolours every use of it in one step.
class SelectionColors : public QWidget {
    Q_OBJECT
public:
    SelectionColors(EditorSession &session, FloatingPanel &picker, QWidget *parent);
    void synchronize();
    // Shown when two or more painted objects are selected.
    bool isWanted() const;
    const std::vector<QColor> &colors() const { return m_colors; }

private:
    EditorSession &m_session;
    FloatingPanel &m_picker;
    QWidget *const m_chips;
    std::vector<QColor> m_colors;
};
