#pragma once
#include "Document/VectorDocument.h"
#include <QPainterPath>
#include <QRectF>
#include <memory>
#include <vector>

class QTextLayout;

// A text object laid out in its own coordinates: point type line by line,
// area type wrapped and justified in its box. The canvas, the inline editor
// and every export read glyphs and caret positions from here.
class TextLayout {
public:
    explicit TextLayout(const TextContent &text);
    ~TextLayout();
    TextLayout(const TextLayout &) = delete;
    TextLayout &operator=(const TextLayout &) = delete;

    struct Line {
        // Indices into the whole text; a paragraph's newline is in no line.
        int start = 0;
        int length = 0;
        int paragraph = 0;
        double baseline = 0;
        double ascent = 0;
        double descent = 0;
        // The x of the line's first and last character edges, trailing spaces left out.
        double left = 0;
        double right = 0;
        bool lastInParagraph = true;
        // Its QTextLine within the paragraph, and point type's alignment offset.
        int index = 0;
        double offset = 0;
        // Past a fixed area's height.
        bool hidden = false;
    };
    const std::vector<Line> &lines() const { return m_lines; }
    int lineOf(int position) const;
    double xAt(int position) const;
    // A position's x on one line, clamped to it.
    double xAt(int position, int line) const;
    // The nearest caret position to a point in the text's coordinates.
    int positionAt(QPointF local) const;
    QPainterPath outline() const;
    bool overflows() const { return m_overflows; }
    // Area type's box; point type's glyphs, or the caret's band when empty.
    QRectF frame() const;
    // The first line's ascent and descent, for carets on empty text.
    double ascent() const { return m_ascent; }
    double descent() const { return m_descent; }

private:
    struct Paragraph;
    double shift(const Line &line, double x) const;
    double rawX(const Line &line, int position) const;

    const TextContent m_text;
    std::vector<std::unique_ptr<Paragraph>> m_paragraphs;
    std::vector<Line> m_lines;
    // Layout units to pt: the font is laid out at a whole pixel size.
    double m_scale = 1;
    double m_horizontal = 1;
    double m_ascent = 0;
    double m_descent = 0;
    bool m_overflows = false;
};
