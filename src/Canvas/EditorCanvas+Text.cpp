#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineF>
#include <limits>

namespace {
// A text layer's own name: its first line, trimmed.
QString autoName(const QString &text)
{
    QString line = text.section(QLatin1Char('\n'), 0, 0).simplified();
    if (line.size() > 30)
        line = line.left(29).trimmed() + QChar(0x2026);
    return line;
}

// The path percent nearest `point`: a coarse scan, then a few rounds of local refinement.
double nearestPercent(const QPainterPath &geometry, QPointF point)
{
    if (geometry.length() <= 1e-6)
        return 0;
    constexpr int coarse = 200;
    double bestT = 0, bestDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= coarse; ++i) {
        const double t = double(i) / coarse;
        const double d = QLineF(geometry.pointAtPercent(t), point).length();
        if (d < bestDistance) {
            bestDistance = d;
            bestT = t;
        }
    }
    double span = 1.0 / coarse;
    for (int refine = 0; refine < 20; ++refine) {
        span /= 2;
        for (const double t : {std::clamp(bestT - span, 0.0, 1.0), std::clamp(bestT + span, 0.0, 1.0)}) {
            const double d = QLineF(geometry.pointAtPercent(t), point).length();
            if (d < bestDistance) {
                bestDistance = d;
                bestT = t;
            }
        }
    }
    return bestT;
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
    if (session.refuseWhenLocked())
        return;
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
        session.previewAddObject(text->object, session.drawingParent(text->object.transform.map(QPointF())));
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

// Type on a Path -------------------------------------------------------------------

void EditorCanvas::State::typeOnPathPress(QPointF view)
{
    const QPointF document = toDocument(view);
    const std::optional<QUuid> leaf = hitLeaf(document);
    if (!leaf)
        return;
    const VectorObject *object = session.document()->find(*leaf);
    if (!object)
        return;
    if (object->kind == ObjectKind::text) {
        const VectorObject copy = *object;
        session.selectTool(Tool::text);
        beginTextEditing(copy, true, document);
        return;
    }
    if (object->kind != ObjectKind::path)
        return;
    const QUuid created = session.convertPathToTypeOnPath(*leaf);
    if (created.isNull())
        return;
    const VectorObject *made = session.document()->find(created);
    if (!made)
        return;
    session.selectTool(Tool::text);
    beginTextEditing(*made, true, std::nullopt);
}

std::optional<std::pair<QPointF, QPointF>> EditorCanvas::State::pathBracketPoint(const VectorObject &object) const
{
    if (!object.text.onPath)
        return std::nullopt;
    const TextPath &onPath = *object.text.onPath;
    const QPainterPath geometry = onPath.flipped ? reversed(onPath.path).painterPath() : onPath.path.painterPath();
    if (geometry.length() <= 1e-6)
        return std::nullopt;
    const double t = std::clamp(onPath.flipped ? 1 - onPath.start : onPath.start, 0.0, 1.0);
    const QPointF point = geometry.pointAtPercent(t);
    constexpr double eps = 0.001;
    QPointF tangent = geometry.pointAtPercent(std::clamp(t + eps, 0.0, 1.0)) - geometry.pointAtPercent(std::clamp(t - eps, 0.0, 1.0));
    const double length = std::hypot(tangent.x(), tangent.y());
    tangent = length > 1e-9 ? tangent / length : QPointF(1, 0);
    return std::pair(point, tangent);
}

std::optional<QUuid> EditorCanvas::State::pathBracketAt(QPointF view) const
{
    if (!session.document())
        return std::nullopt;
    for (const QUuid &id : session.selectedTexts()) {
        const VectorObject *object = text && text->object.id == id ? &text->object : session.document()->find(id);
        if (!object)
            continue;
        const auto bracket = pathBracketPoint(*object);
        if (!bracket)
            continue;
        if (QLineF(view, toView(object->transform.map(bracket->first))).length() <= 7)
            return id;
    }
    return std::nullopt;
}

void EditorCanvas::State::dragPathBracket(QPointF view)
{
    if (!drag->started)
        return;
    if (!drag->interacting) {
        session.beginInteraction(QStringLiteral("Move Type on a Path"));
        drag->interacting = true;
    }
    const VectorObject *original = session.originalObject(drag->object);
    if (!original || !original->text.onPath)
        return;
    const QPointF local = original->transform.inverted().map(toDocument(view));
    const bool flipped = original->text.onPath->flipped;
    const QPainterPath geometry = flipped ? reversed(original->text.onPath->path).painterPath() : original->text.onPath->path.painterPath();
    const double t = nearestPercent(geometry, local);
    VectorObject changed = *original;
    changed.text.onPath->start = flipped ? 1 - t : t;
    session.previewObject(changed);
}

// Threaded text: in/out ports -------------------------------------------------------

QPointF EditorCanvas::State::outPortAt(const VectorObject &object) const
{
    return object.transform.map(QPointF(object.text.frame().right(), object.text.frame().bottom()));
}

QPointF EditorCanvas::State::inPortAt(const VectorObject &object) const
{
    return object.transform.map(QPointF(object.text.frame().left(), object.text.frame().top()));
}

std::optional<QUuid> EditorCanvas::State::outPortHitAt(QPointF view) const
{
    if (!session.document())
        return std::nullopt;
    for (const QUuid &id : session.selectedTexts()) {
        const VectorObject *object = session.document()->find(id);
        if (!object || !object->text.area)
            continue;
        if (QLineF(view, toView(outPortAt(*object))).length() <= 7)
            return id;
    }
    return std::nullopt;
}

bool EditorCanvas::State::threadLinkPress(QPointF view)
{
    if (!linkArmedFrom || !session.document())
        return false;
    const QUuid from = *linkArmedFrom;
    linkArmedFrom.reset();
    const QPointF document = toDocument(view);
    if (const std::optional<QUuid> leaf = hitLeaf(document)) {
        const VectorObject *target = session.document()->find(*leaf);
        if (target && target->kind == ObjectKind::text && target->text.area && *leaf != from)
            session.linkThread(from, *leaf);
        return true;
    }
    const VectorObject *source = session.document()->find(from);
    if (source && source->text.area)
        session.linkNewThread(from, document, *source->text.area);
    return true;
}
