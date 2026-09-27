#pragma once
#include "Document/VectorDocument.h"
#include <QInputMethodEvent>
#include <QPainter>
#include <QTransform>
#include <QVariant>

class QKeyEvent;

// Point type edited in place: the caret, selection and input method's preedit.
// The canvas applies `object` to the document as it changes.
class InlineTextEditor {
public:
    enum class Result { ignored, moved, edited };

    explicit InlineTextEditor(VectorObject object);
    // The text object as typed so far.
    VectorObject object;
    // True once the object is in the document (new type waits for a character).
    bool inDocument = false;
    int caret = 0;
    int anchor = 0;
    QString preedit;

    const QString &text() const { return object.text.text; }
    Result keyPress(const QKeyEvent &event);
    // Keys the editor takes ahead of menu shortcuts.
    static bool claims(const QKeyEvent &event);
    Result inputMethod(const QInputMethodEvent &event);
    QVariant inputMethodQuery(Qt::InputMethodQuery query, const QTransform &documentToView) const;
    // The nearest caret position to a document point.
    int positionAt(QPointF documentPoint) const;
    void selectAll();
    // The caret in the text's own coordinates.
    QRectF caretRect() const;
    void draw(QPainter &painter, const QTransform &documentToView, bool caretShown, const QColor &accent) const;

private:
    struct Line {
        int start;
        int length;
    };
    std::vector<Line> lines() const;
    int lineOf(int position) const;
    // Line's left edge and a position's x, in the text's own coordinates.
    double lineX(int line) const;
    double xAt(int position) const;
    double baseline(int line) const;
    double scale() const;
    void insert(const QString &typed);
    void erase(bool forward);
    void moveTo(int position, bool extend);
    bool hasSelection() const { return caret != anchor; }
};
