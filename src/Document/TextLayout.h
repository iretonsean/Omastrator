#pragma once
#include "Document/VectorDocument.h"
#include <QPainterPath>
#include <QRectF>
#include <QTextLayout>
#include <memory>
#include <vector>

// A text object laid out in its own coordinates: point type line by line,
// area type wrapped and justified in its box, frames and threads flowing rows
// past exclusions and into the next box, and type on a path along its curve.
// The canvas, the inline editor and every export read glyphs and caret positions here.
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
        // Past a fixed area's height, or past the last frame or the end of a path.
        bool hidden = false;
        // Frames: which one of flow.frames this row fell into.
        int frame = 0;
        // The width this row actually had to work with: the area, or the frame
        // interval free of exclusions. Used by shift() and the justify-centre/right offset.
        double available = 0;
        // Type on a Path: arc length along the path where this row starts.
        double pathOffset = 0;
        // A soft hyphen ends this line (drawn as a hyphen by Qt).
        bool hyphenated = false;
    };
    const std::vector<Line> &lines() const { return m_lines; }
    int lineOf(int position) const;
    double xAt(int position) const;
    // A position's x on one line, clamped to it.
    double xAt(int position, int line) const;
    // The nearest caret position to a point in the text's coordinates.
    int positionAt(QPointF local) const;
    QPainterPath outline() const;
    // The outline split by the colour runs give it; nullopt is the object's fill.
    std::vector<std::pair<std::optional<QColor>, QPainterPath>> fills() const;
    // Glyphs shaped, ligatures counting as one.
    int glyphCount() const;
    bool overflows() const { return m_overflows; }
    // Area type's box; point type's glyphs, or the caret's band when empty.
    QRectF frame() const;
    // The first line's ascent and descent, for carets on empty text.
    double ascent() const { return m_ascent; }
    double descent() const { return m_descent; }
    // The text actually laid out: the flow's shared story when threaded, else the object's own.
    const TextContent &source() const { return m_text; }
    bool isOnPath() const { return m_pathMode; }
    // A point in `line`'s ordinary (flat) coordinates warped into how it actually
    // shows: along the path, or (crossing frames) into another box's own local space.
    // Otherwise the identity.
    QPointF warp(QPointF local, int line) const;

private:
    struct Paragraph;
    QList<QTextLayout::FormatRange> characterFormats(int start, int length, const std::vector<int> &toDisplay) const;
    double leadingOf(const ParagraphFormat &format, int start, int length) const;
    double ascentOf(int start, int length) const;
    double rawWidth(const Line &line) const;
    double shift(const Line &line, double x) const;
    double rawX(const Line &line, int position) const;
    // A point already in `fromFrame`'s local space, re-expressed in `toFrame`'s.
    QPointF warpToFrame(QPointF local, int fromFrame, int toFrame) const;
    // The point on the path at arc length `length` (clamped/wrapped), and its unit tangent.
    QPointF pathPointAndTangent(double length, QPointF *tangent) const;

    const TextContent m_text;
    // The formats in use, the object's own first, and which one each character has.
    std::vector<CharacterFormat> m_formats;
    std::vector<int> m_formatOf;
    std::vector<double> m_formatAscents;
    std::vector<std::unique_ptr<Paragraph>> m_paragraphs;
    std::vector<Line> m_lines;
    // Layout units to pt: the font is laid out at a whole pixel size.
    double m_scale = 1;
    double m_horizontal = 1;
    double m_ascent = 0;
    double m_descent = 0;
    bool m_overflows = false;
    // Frames and threads (P2-4): the original object's own flow (frames, its own index in them).
    TextFlow m_flow;
    // Type on a Path (P2-3).
    bool m_pathMode = false;
    QPainterPath m_pathGeometry;
    double m_pathLength = 0;
    double m_pathStartLength = 0;
    bool m_pathClosed = false;
};
