#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>

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
    // Point type: the click is the first baseline's start.
    beginTextEditing(session.textObject(snapPoint(guidesExcluding({}), document), QString()), false, std::nullopt);
    clearGuides();
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
        text->object.name.clear();
        session.previewAddObject(text->object);
        text->inDocument = true;
    } else if (const VectorObject *current = session.document()->find(text->object.id)) {
        VectorObject object = *current;
        object.text.text = text->text();
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
    caretBlink.stop();
    canvas.setAttribute(Qt::WA_InputMethodEnabled, false);
    // Qt keeps a preedit around; it goes with the editor.
    QGuiApplication::inputMethod()->reset();
    const VectorObject *current = editor->inDocument && session.document() ? session.document()->find(editor->object.id) : nullptr;
    applyingText = true;
    if (current) {
        const QString firstLine = current->text.text.section(QLatin1Char('\n'), 0, 0).left(40);
        const bool autoNamed = textCreated || current->name == textAtStart.section(QLatin1Char('\n'), 0, 0).left(40);
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
    const QRectF glyphs = text->object.outline().boundingRect();
    // An empty line still has height: the caret's.
    const QRectF local = text->object.text.outline().boundingRect().united(QRectF(0, caret.top(), 1, caret.height()));
    return text->object.transform.mapRect(local).united(glyphs);
}
