#pragma once
#include "Document/TextLayout.h"
#include "Document/VectorDocument.h"
#include <QInputMethodEvent>
#include <QPainter>
#include <QTransform>
#include <QVariant>
#include <memory>
#include <utility>

class QKeyEvent;

// Point and area type edited in place: the caret, selection and input method's preedit.
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
    // Double- and triple-click: the word or line around a position.
    void selectWord(int position);
    void selectLine(int position);
    // Where Ctrl+Left and Ctrl+Right land from `position`.
    int wordBoundary(int position, bool forward) const;
    // The text as shown: the preedit sits at the caret and pushes what follows along.
    QString displayText() const;
    VectorObject displayObject() const;
    // The caret in the text's own coordinates.
    QRectF caretRect() const;
    void draw(QPainter &painter, const QTransform &documentToView, bool caretShown, const QColor &accent) const;
    // The selected range, or the whole text when nothing is selected.
    std::pair<int, int> range() const;
    bool hasSelection() const { return caret != anchor; }
    // The text as laid out now.
    const TextLayout &layout() const;

private:
    int lineOf(int position) const;
    double xAt(int position) const;
    double baseline(int line) const;
    void insert(const QString &typed);
    void erase(bool forward);
    void eraseWord(bool forward);
    void moveTo(int position, bool extend);
    mutable std::shared_ptr<TextLayout> m_layout;
    mutable TextContent m_laidOut;
};
