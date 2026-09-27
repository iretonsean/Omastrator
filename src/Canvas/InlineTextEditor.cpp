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

double InlineTextEditor::scale() const
{
    // TextContent::outline draws at a whole pixel size and scales to the real one.
    const QFont face = object.text.font();
    return object.text.size / std::max(1.0, double(face.pixelSize()));
}

std::vector<InlineTextEditor::Line> InlineTextEditor::lines() const
{
    std::vector<Line> result;
    int start = 0;
    const QString &content = text();
    for (int index = 0; index <= content.size(); ++index) {
        if (index == content.size() || content[index] == QLatin1Char('\n')) {
            result.push_back({start, index - start});
            start = index + 1;
        }
    }
    return result;
}

int InlineTextEditor::lineOf(int position) const
{
    const auto all = lines();
    for (size_t line = 0; line < all.size(); ++line) {
        if (position <= all[line].start + all[line].length)
            return int(line);
    }
    return int(all.size()) - 1;
}

double InlineTextEditor::lineX(int line) const
{
    const auto all = lines();
    const QFontMetricsF metrics(object.text.font());
    const double width = metrics.horizontalAdvance(text().mid(all[size_t(line)].start, all[size_t(line)].length));
    switch (object.text.alignment) {
    case TextAlignment::center:
        return -width / 2 * scale();
    case TextAlignment::right:
        return -width * scale();
    default:
        return 0;
    }
}

double InlineTextEditor::xAt(int position) const
{
    const int line = lineOf(position);
    const Line span = lines()[size_t(line)];
    const QFontMetricsF metrics(object.text.font());
    return lineX(line) + metrics.horizontalAdvance(text().mid(span.start, position - span.start)) * scale();
}

double InlineTextEditor::baseline(int line) const
{
    return line * object.text.size * object.text.leading;
}

QRectF InlineTextEditor::caretRect() const
{
    const QFontMetricsF metrics(object.text.font());
    const double ascent = metrics.ascent() * scale(), descent = metrics.descent() * scale();
    const double y = baseline(lineOf(caret));
    return QRectF(xAt(caret), y - ascent, 0, ascent + descent);
}

int InlineTextEditor::positionAt(QPointF documentPoint) const
{
    const QPointF local = object.transform.inverted().map(documentPoint);
    const auto all = lines();
    const double pitch = object.text.size * object.text.leading;
    // Baselines sit at the bottom of each line's band.
    const int line = std::clamp(int(std::floor((local.y() + object.text.size * 0.8) / std::max(pitch, 1e-6))), 0, int(all.size()) - 1);
    const Line span = all[size_t(line)];
    int best = span.start;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int position = span.start; position <= span.start + span.length; ++position) {
        const double distance = std::abs(xAt(position) - local.x());
        if (distance < bestDistance) {
            bestDistance = distance;
            best = position;
        }
    }
    return best;
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
    const Line span = lines()[size_t(lineOf(std::clamp(position, 0, int(text().size()))))];
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
        content.remove(caret, length);
    } else if (!forward && caret > 0) {
        const int length = content[caret - 1].isLowSurrogate() && caret >= 2 ? 2 : 1;
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
        const auto all = lines();
        if (line < 0) {
            moveTo(0, extend);
        } else if (line >= int(all.size())) {
            moveTo(int(content.size()), extend);
        } else {
            // The nearest position below or above the caret's x.
            const double x = xAt(caret);
            int best = all[size_t(line)].start;
            for (int position = best; position <= all[size_t(line)].start + all[size_t(line)].length; ++position) {
                if (std::abs(xAt(position) - x) < std::abs(xAt(best) - x))
                    best = position;
            }
            moveTo(best, extend);
        }
        break;
    }
    case Qt::Key_Home:
        moveTo(control ? 0 : lines()[size_t(lineOf(caret))].start, extend);
        break;
    case Qt::Key_End: {
        const Line span = lines()[size_t(lineOf(caret))];
        moveTo(control ? int(content.size()) : span.start + span.length, extend);
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
    const QFontMetricsF metrics(object.text.font());
    const double ascent = metrics.ascent() * scale(), descent = metrics.descent() * scale();
    if (hasSelection()) {
        QColor tint = accent;
        tint.setAlphaF(0.3);
        const int from = std::min(caret, anchor), to = std::max(caret, anchor);
        const auto all = lines();
        for (size_t line = 0; line < all.size(); ++line) {
            const int start = std::max(from, all[line].start), end = std::min(to, all[line].start + all[line].length);
            if (start > end || (start == end && to <= all[line].start + all[line].length))
                continue;
            const double y = baseline(int(line));
            QPainterPath band;
            band.addRect(QRectF(QPointF(xAt(start), y - ascent), QPointF(xAt(end) + (end < to ? metrics.averageCharWidth() * scale() / 2 : 0), y + descent)));
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
            QPainterPath glyphs;
            glyphs.addText(QPointF(caretBox.left() / scale(), baseline(lineOf(caret)) / scale()), object.text.font(), preedit);
            glyphs = QTransform::fromScale(scale(), scale()).map(glyphs);
            painter.fillPath(toView.map(glyphs), accent);
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
