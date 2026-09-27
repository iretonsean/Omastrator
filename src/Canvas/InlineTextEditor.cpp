#include "Canvas/InlineTextEditor.h"
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

InlineTextEditor::InlineTextEditor(VectorObject object) : object(std::move(object))
{
    caret = anchor = int(this->object.text.text.size());
}

const TextLayout &InlineTextEditor::layout() const
{
    if (!m_layout || !(m_laidOut == object.text)) {
        m_layout = std::make_shared<TextLayout>(object.text);
        m_laidOut = object.text;
    }
    return *m_layout;
}

std::pair<int, int> InlineTextEditor::range() const
{
    if (!hasSelection())
        return {0, int(text().size())};
    return {std::min(caret, anchor), std::max(caret, anchor)};
}

int InlineTextEditor::lineOf(int position) const
{
    return std::max(0, layout().lineOf(position));
}

double InlineTextEditor::xAt(int position) const
{
    return layout().xAt(position);
}

double InlineTextEditor::baseline(int line) const
{
    const auto &all = layout().lines();
    if (all.empty())
        return object.text.area ? layout().ascent() : 0;
    return all[size_t(std::clamp(line, 0, int(all.size()) - 1))].baseline - object.text.baselineShift;
}

QRectF InlineTextEditor::caretRect() const
{
    const double ascent = layout().ascent() * object.text.verticalScale / 100, descent = layout().descent();
    const double y = baseline(lineOf(caret));
    return QRectF(xAt(caret), y - ascent, 0, ascent + descent);
}

int InlineTextEditor::positionAt(QPointF documentPoint) const
{
    return layout().positionAt(object.transform.inverted().map(documentPoint));
}

void InlineTextEditor::selectAll()
{
    anchor = 0;
    caret = int(text().size());
}

namespace {
// Word, punctuation or space: a Ctrl+arrow crosses one run of a kind, then any space.
int charClass(QChar character)
{
    if (character.isSpace())
        return 0;
    return character.isLetterOrNumber() || character == QLatin1Char('_') || character.isMark() ? 1 : 2;
}
}

int InlineTextEditor::wordBoundary(int position, bool forward) const
{
    const QString &content = text();
    int at = std::clamp(position, 0, int(content.size()));
    if (forward) {
        if (at < content.size() && charClass(content[at]) != 0) {
            const int kind = charClass(content[at]);
            while (at < content.size() && charClass(content[at]) == kind)
                ++at;
        }
        while (at < content.size() && charClass(content[at]) == 0 && content[at] != QLatin1Char('\n'))
            ++at;
        // A line end is a stop of its own.
        if (at == position && at < content.size())
            ++at;
        return at;
    }
    while (at > 0 && charClass(content[at - 1]) == 0 && content[at - 1] != QLatin1Char('\n'))
        --at;
    if (at > 0 && charClass(content[at - 1]) != 0) {
        const int kind = charClass(content[at - 1]);
        while (at > 0 && charClass(content[at - 1]) == kind)
            --at;
    } else if (at == position && at > 0) {
        --at;
    }
    return at;
}

void InlineTextEditor::selectWord(int position)
{
    const QString &content = text();
    int from = std::clamp(position, 0, int(content.size())), to = from;
    // The run under the pointer, or the one just before it at a word's end.
    const int kind = to < content.size() && content[to] != QLatin1Char('\n') ? charClass(content[to])
        : from > 0 && content[from - 1] != QLatin1Char('\n')                  ? charClass(content[from - 1])
                                                                                : -1;
    if (kind < 0) {
        anchor = caret = from;
        return;
    }
    while (from > 0 && content[from - 1] != QLatin1Char('\n') && charClass(content[from - 1]) == kind)
        --from;
    while (to < content.size() && content[to] != QLatin1Char('\n') && charClass(content[to]) == kind)
        ++to;
    anchor = from;
    caret = to;
}

void InlineTextEditor::selectLine(int position)
{
    const auto &all = layout().lines();
    if (all.empty())
        return;
    const TextLayout::Line &span = all[size_t(lineOf(std::clamp(position, 0, int(text().size()))))];
    anchor = span.start;
    caret = span.start + span.length;
}

QString InlineTextEditor::displayText() const
{
    QString shown = text();
    return preedit.isEmpty() ? shown : shown.insert(std::clamp(caret, 0, int(shown.size())), preedit);
}

VectorObject InlineTextEditor::displayObject() const
{
    VectorObject shown = object;
    shown.text.text = displayText();
    return shown;
}

void InlineTextEditor::eraseWord(bool forward)
{
    if (hasSelection()) {
        insert(QString());
        return;
    }
    anchor = wordBoundary(caret, forward);
    insert(QString());
}

void InlineTextEditor::insert(const QString &typed)
{
    QString &content = object.text.text;
    const int from = std::min(caret, anchor), to = std::max(caret, anchor);
    object.text.replaceKerns(from, to, int(typed.size()));
    content.replace(from, to - from, typed);
    caret = anchor = from + int(typed.size());
}

void InlineTextEditor::erase(bool forward)
{
    if (hasSelection()) {
        insert(QString());
        return;
    }
    QString &content = object.text.text;
    if (forward && caret < content.size()) {
        // Whole surrogate pairs, never half a character.
        const int length = content[caret].isHighSurrogate() && caret + 1 < content.size() ? 2 : 1;
        object.text.replaceKerns(caret, caret + length, 0);
        content.remove(caret, length);
    } else if (!forward && caret > 0) {
        const int length = content[caret - 1].isLowSurrogate() && caret >= 2 ? 2 : 1;
        object.text.replaceKerns(caret - length, caret, 0);
        content.remove(caret - length, length);
        caret -= length;
    }
    anchor = caret;
}

void InlineTextEditor::moveTo(int position, bool extend)
{
    caret = std::clamp(position, 0, int(text().size()));
    if (!extend)
        anchor = caret;
}

bool InlineTextEditor::claims(const QKeyEvent &event)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    switch (event.key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        // Alt and the arrows are the type keys: tracking, kerning, leading.
        return !modifiers.testFlag(Qt::AltModifier);
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Escape:
        return true;
    default:
        break;
    }
    if (modifiers == Qt::ControlModifier)
        return event.key() == Qt::Key_A || event.key() == Qt::Key_C || event.key() == Qt::Key_X || event.key() == Qt::Key_V;
    // Printable keys type; menus with bare letters must wait.
    return modifiers == Qt::NoModifier && !event.text().isEmpty() && event.text().at(0).isPrint();
}

InlineTextEditor::Result InlineTextEditor::keyPress(const QKeyEvent &event)
{
    const bool extend = event.modifiers().testFlag(Qt::ShiftModifier);
    const bool control = event.modifiers().testFlag(Qt::ControlModifier);
    const int before = caret, beforeAnchor = anchor;
    const QString content = text();
    switch (event.key()) {
    case Qt::Key_Left:
        if (control)
            moveTo(wordBoundary(caret, false), extend);
        else if (!extend && hasSelection())
            moveTo(std::min(caret, anchor), false);
        else
            moveTo(caret - (caret >= 2 && content[caret - 1].isLowSurrogate() ? 2 : 1), extend);
        break;
    case Qt::Key_Right:
        if (control)
            moveTo(wordBoundary(caret, true), extend);
        else if (!extend && hasSelection())
            moveTo(std::max(caret, anchor), false);
        else
            moveTo(caret + (caret + 1 < content.size() && content[caret].isHighSurrogate() ? 2 : 1), extend);
        break;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int line = lineOf(caret) + (event.key() == Qt::Key_Up ? -1 : 1);
        const auto &all = layout().lines();
        if (line < 0)
            moveTo(0, extend);
        else if (line >= int(all.size()))
            moveTo(int(content.size()), extend);
        else
            // The nearest position above or below the caret's x.
            moveTo(layout().positionAt(QPointF(xAt(caret), all[size_t(line)].baseline)), extend);
        break;
    }
    case Qt::Key_Home:
    case Qt::Key_End: {
        const auto &all = layout().lines();
        if (control || all.empty()) {
            moveTo(event.key() == Qt::Key_Home ? 0 : int(content.size()), extend);
            break;
        }
        const TextLayout::Line &span = all[size_t(lineOf(caret))];
        // A wrapped line's end is the next line's start: stop before its break.
        const int end = span.lastInParagraph ? span.start + span.length : std::max(span.start, span.start + span.length - 1);
        moveTo(event.key() == Qt::Key_Home ? span.start : end, extend);
        break;
    }
    case Qt::Key_Backspace:
        if (control)
            eraseWord(false);
        else
            erase(false);
        return text() == content ? Result::ignored : Result::edited;
    case Qt::Key_Delete:
        if (control)
            eraseWord(true);
        else
            erase(true);
        return text() == content ? Result::ignored : Result::edited;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insert(QStringLiteral("\n"));
        return Result::edited;
    default:
        if (control && event.key() == Qt::Key_A) {
            selectAll();
            break;
        }
        if (control && (event.key() == Qt::Key_C || event.key() == Qt::Key_X)) {
            if (hasSelection())
                QGuiApplication::clipboard()->setText(content.mid(std::min(caret, anchor), std::abs(caret - anchor)));
            if (event.key() == Qt::Key_X && hasSelection()) {
                insert(QString());
                return Result::edited;
            }
            return Result::moved;
        }
        if (control && event.key() == Qt::Key_V) {
            const QString pasted = QGuiApplication::clipboard()->text();
            if (pasted.isEmpty())
                return Result::moved;
            insert(pasted);
            return Result::edited;
        }
        if (!(event.modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) && !event.text().isEmpty() && event.text().at(0).isPrint()) {
            insert(event.text());
            return Result::edited;
        }
        return Result::ignored;
    }
    return caret != before || anchor != beforeAnchor ? Result::moved : Result::ignored;
}

InlineTextEditor::Result InlineTextEditor::inputMethod(const QInputMethodEvent &event)
{
    const QString before = text();
    if (event.replacementLength() > 0 && !hasSelection()) {
        const int start = std::clamp(caret + event.replacementStart(), 0, int(before.size()));
        const int length = std::min(event.replacementLength(), int(before.size()) - start);
        anchor = start;
        caret = start + length;
    }
    if (!event.commitString().isEmpty() || event.replacementLength() > 0)
        insert(event.commitString());
    preedit = event.preeditString();
    return text() != before ? Result::edited : Result::moved;
}

QVariant InlineTextEditor::inputMethodQuery(Qt::InputMethodQuery query, const QTransform &documentToView) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return (object.transform * documentToView).mapRect(caretRect());
    case Qt::ImFont:
        return object.text.font();
    case Qt::ImCursorPosition:
        return caret;
    case Qt::ImAnchorPosition:
        return anchor;
    case Qt::ImSurroundingText:
        return text();
    case Qt::ImCurrentSelection:
        return text().mid(std::min(caret, anchor), std::abs(caret - anchor));
    default:
        return {};
    }
}

void InlineTextEditor::draw(QPainter &painter, const QTransform &documentToView, bool caretShown, const QColor &accent) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QTransform toView = object.transform * documentToView;
    const double ascent = layout().ascent(), descent = layout().descent();
    if (hasSelection()) {
        QColor tint = accent;
        tint.setAlphaF(0.3);
        const int from = std::min(caret, anchor), to = std::max(caret, anchor);
        const auto &all = layout().lines();
        for (size_t line = 0; line < all.size(); ++line) {
            const int lineEnd = all[line].start + all[line].length;
            const int start = std::max(from, all[line].start), end = std::min(to, lineEnd);
            if (all[line].hidden || start > end || (start == end && to <= lineEnd))
                continue;
            const double y = baseline(int(line));
            // A selected line break shows as a sliver past the line's end.
            const double past = end < to && all[line].lastInParagraph ? object.text.size / 4 : 0;
            QPainterPath band;
            band.addRect(QRectF(QPointF(layout().xAt(start, int(line)), y - ascent), QPointF(layout().xAt(end, int(line)) + past, y + descent)));
            painter.fillPath(toView.map(band), tint);
        }
    }
    QRectF caretBox = caretRect();
    if (!preedit.isEmpty()) {
        // The preedit sits at the caret, underlined, until it commits; what follows moves along.
        InlineTextEditor shown(displayObject());
        shown.caret = shown.anchor = caret + int(preedit.size());
        const double underline = baseline(lineOf(caret)) + descent / 2;
        const double end = shown.xAt(shown.caret);
        if (!inDocument) {
            // Nothing is in the document to draw it yet.
            painter.fillPath(toView.map(shown.object.text.outline()), accent);
        }
        painter.setPen(QPen(accent, 1));
        painter.drawLine(toView.map(QPointF(caretBox.left(), underline)), toView.map(QPointF(end, underline)));
        caretBox = shown.caretRect();
    }
    if (caretShown && !hasSelection()) {
        painter.setPen(QPen(accent, 1.5));
        painter.drawLine(toView.map(caretBox.topLeft()), toView.map(caretBox.bottomLeft()));
    }
    painter.restore();
}
