#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>

namespace {
// A text layer's own name: its first line, trimmed.
QString autoName(const QString &text)
{
    QString line = text.section(QLatin1Char('\n'), 0, 0).simplified();
    if (line.size() > 30)
        line = line.left(29).trimmed() + QChar(0x2026);
    return line;
}
}

void EditorCanvas::State::textPress(QPointF view)
{
    const QPointF document = toDocument(view);
    if (const std::optional<QUuid> leaf = hitLeaf(document)) {
        const VectorObject *object = session.document()->find(*leaf);
        if (object->kind == ObjectKind::text) {
            beginTextEditing(*object, true, document);
            return;
        }
    }
    Drag &made = beginDrag(DragKind::textArea, view);
    made.pressDocument = made.grabbed = snapPoint(guidesExcluding({}), document);
}

void EditorCanvas::State::finishTextArea()
{
    const QPointF from = drag->pressDocument;
    const QRectF box = QRectF(from, drag->grabbed).normalized();
    const bool area = drag->started && box.width() >= 4;
    drag.reset();
    clearGuides();
    // Point type: the click is the first baseline's start.
    VectorObject object = session.textObject(from, QString());
    if (area) {
        // Area type: the box's top-left is the origin, and text wraps inside it.
        object.text.area = QSizeF(box.width(), box.height() >= 4 ? box.height() : 0);
        object.transform = QTransform::fromTranslate(box.left(), box.top());
    }
    beginTextEditing(object, false, std::nullopt);
}

void EditorCanvas::State::beginTextEditing(const VectorObject &object, bool inDocument, std::optional<QPointF> caretAt)
{
    if (text)
        finishText();
    text = std::make_unique<InlineTextEditor>(object);
    text->inDocument = inDocument;
    textAtStart = object.text.text;
    textCreated = !inDocument;
    if (caretAt)
        text->caret = text->anchor = text->positionAt(*caretAt);
    if (inDocument)
        session.select({object.id});
    canvas.setAttribute(Qt::WA_InputMethodEnabled, true);
    canvas.setFocus(Qt::OtherFocusReason);
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
    restartCaret();
    emit canvas.textEditingChanged(true);
    canvas.update();
}

void EditorCanvas::State::restartCaret()
{
    // Type edits and fills follow the characters selected here.
    if (text && text->inDocument)
        session.setTextRange(EditorSession::TextRange{text->object.id, text->anchor, text->caret});
    else
        session.setTextRange(std::nullopt);
    caretShown = true;
    if (text)
        caretBlink.start();
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    canvas.update();
}

void EditorCanvas::State::applyText()
{
    if (!text || !session.document())
        return;
    applyingText = true;
    // One interaction per editing stretch; an edit elsewhere may have closed it.
    if (!session.isInteracting())
        session.beginInteraction(textCreated ? QStringLiteral("Type") : QStringLiteral("Edit Type"));
    if (!text->inDocument) {
        text->object.name = autoName(text->text());
        session.previewAddObject(text->object);
        text->inDocument = true;
    } else if (const VectorObject *current = session.document()->find(text->object.id)) {
        VectorObject object = *current;
        // Type named after itself keeps following what it says, as Illustrator's does.
        if (textCreated || current->name == autoName(current->text.text) || current->name == autoName(textAtStart))
            object.name = autoName(text->text());
        // What typing changes: the characters and what rides on them.
        object.text.text = text->text();
        object.text.kerns = text->object.text.kerns;
        object.text.runs = text->object.text.runs;
        object.text.paragraphFormats = text->object.text.paragraphFormats;
        session.previewObject(object);
    }
    applyingText = false;
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
}

void EditorCanvas::State::finishText()
{
    if (!text)
        return;
    const std::unique_ptr<InlineTextEditor> editor = std::move(text);
    session.setTextRange(std::nullopt);
    caretBlink.stop();
    canvas.setAttribute(Qt::WA_InputMethodEnabled, false);
    // Qt keeps a preedit around; it goes with the editor.
    QGuiApplication::inputMethod()->reset();
    const VectorObject *current = editor->inDocument && session.document() ? session.document()->find(editor->object.id) : nullptr;
    applyingText = true;
    if (current) {
        const QString firstLine = autoName(current->text.text);
        const bool autoNamed = textCreated || current->name == autoName(textAtStart);
        if (current->text.text.isEmpty()) {
            // Empty type is no object.
            if (!session.isInteracting())
                session.beginInteraction(QStringLiteral("Delete"));
            session.previewRemoveObject(current->id);
        } else if (autoNamed && current->name != firstLine) {
            if (!session.isInteracting())
                session.beginInteraction(QStringLiteral("Type"));
            VectorObject renamed = *current;
            renamed.name = firstLine;
            session.previewObject(renamed);
        }
    }
    if (session.isInteracting())
        session.commitInteraction();
    applyingText = false;
    emit canvas.textEditingChanged(false);
    canvas.update();
}

QRectF EditorCanvas::State::textBox() const
{
    if (!text)
        return {};
    const QRectF caret = text->caretRect();
    // An empty line still has height: the caret's.
    const QRectF local = text->object.text.frame().united(QRectF(caret.left(), caret.top(), 1, caret.height()));
    return text->object.transform.mapRect(local);
}
