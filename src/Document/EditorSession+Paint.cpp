#include "Document/EditorSession.h"
#include <algorithm>

namespace {
// Copy Properties' clipboard: an appearance, shared by every open document.
std::optional<VectorObject> &styleClipboard()
{
    static std::optional<VectorObject> style;
    return style;
}

// Every paint in an object's stack.
void forEachPaint(VectorObject &object, const std::function<void(Paint &)> &visit)
{
    visit(object.fill);
    for (Paint &fill : object.extraFills)
        visit(fill);
    visit(object.stroke.paint);
    for (StrokeStyle &stroke : object.extraStrokes)
        visit(stroke.paint);
}

// Colours match by their eight-bit channels, as the picker and hex show them.
bool sameColor(const QColor &a, const QColor &b)
{
    return a.rgba() == b.rgba();
}

// Takes on `to`'s channels and keeps the paint's own alpha.
bool recolor(QColor &color, const QColor &from, const QColor &to)
{
    if (!sameColor(color, from))
        return false;
    const int alpha = color.alpha();
    color = to;
    color.setAlpha(alpha);
    return true;
}
}

void EditorSession::applyCharacterStyle(TextContent &to, const TextContent &from)
{
    const QString words = to.text;
    const std::optional<QSizeF> area = to.area;
    const std::map<int, double> kerns = to.kerns;
    to = from;
    to.text = words;
    to.area = area;
    to.kerns = kerns;
}

void EditorSession::setFillsOfSelection(const std::vector<Paint> &fills, const QString &editName)
{
    if (m_selection.empty()) {
        m_defaultFill = fills.empty() ? Paint::none() : fills.front();
        notify(false);
        return;
    }
    edit(editName, [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (object && object->hasPaint() && !document.isEffectivelyLocked(id))
                object->setFills(fills);
        }
    });
}

void EditorSession::setStrokesOfSelection(const std::vector<StrokeStyle> &strokes, const QString &editName)
{
    if (m_selection.empty()) {
        if (!strokes.empty())
            m_defaultStroke = strokes.front();
        else
            m_defaultStroke.paint = Paint::none();
        notify(false);
        return;
    }
    edit(editName, [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (object && object->hasPaint() && !document.isEffectivelyLocked(id))
                object->setStrokes(strokes);
        }
    });
}

std::vector<QColor> EditorSession::selectionColors() const
{
    std::vector<QColor> colors;
    if (!m_document)
        return colors;
    const auto note = [&colors](const QColor &color) {
        if (std::none_of(colors.begin(), colors.end(), [&](const QColor &each) { return sameColor(each, color); }))
            colors.push_back(color);
    };
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *found = m_document->find(id);
        if (!found || !found->hasPaint())
            continue;
        VectorObject object = *found;
        forEachPaint(object, [&](Paint &paint) {
            if (paint.kind == PaintKind::solid)
                note(paint.color);
            else if (paint.kind != PaintKind::none)
                for (const GradientStop &stop : paint.stops)
                    note(stop.color);
        });
    }
    return colors;
}

void EditorSession::replaceColor(const QColor &from, const QColor &to)
{
    if (sameColor(from, to))
        return;
    edit(QStringLiteral("Recolor"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (!object || !object->hasPaint() || document.isEffectivelyLocked(id))
                continue;
            forEachPaint(*object, [&](Paint &paint) {
                if (paint.kind == PaintKind::solid) {
                    // A hand-picked colour no longer follows its swatch.
                    if (recolor(paint.color, from, to))
                        paint.swatchId.clear();
                } else if (paint.kind != PaintKind::none) {
                    for (GradientStop &stop : paint.stops)
                        recolor(stop.color, from, to);
                    if (!paint.stops.empty())
                        paint.color = paint.stops.front().color;
                }
            });
        }
    });
}

void EditorSession::recolorSwatch(const QString &swatchId, const QColor &color)
{
    if (swatchId.isEmpty())
        return;
    for (Paint *paint : {&m_defaultFill, &m_defaultStroke.paint}) {
        if (paint->swatchId == swatchId)
            paint->color = color;
    }
    if (!m_document) {
        notify(false);
        return;
    }
    edit(QStringLiteral("Edit Swatch"), [&](VectorDocument &document) {
        for (VectorObject &object : document.objects) {
            if (!object.hasPaint())
                continue;
            forEachPaint(object, [&](Paint &paint) {
                if (paint.swatchId == swatchId && paint.kind == PaintKind::solid)
                    paint.color = color;
            });
        }
    });
}

bool EditorSession::canCopyProperties() const
{
    if (!m_document)
        return false;
    const std::vector<QUuid> leaves = selectedLeaves();
    return std::any_of(leaves.begin(), leaves.end(), [this](const QUuid &id) {
        const VectorObject *object = m_document->find(id);
        return object && object->hasPaint();
    });
}

bool EditorSession::copyProperties()
{
    if (!m_document)
        return false;
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *object = m_document->find(id);
        if (object && object->hasPaint()) {
            styleClipboard() = *object;
            notify(false);
            return true;
        }
    }
    return false;
}

bool EditorSession::canPasteProperties() const
{
    return styleClipboard().has_value() && hasSelection();
}

void EditorSession::pasteProperties()
{
    if (!canPasteProperties())
        return;
    const VectorObject style = *styleClipboard();
    edit(QStringLiteral("Paste Properties"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (!object || !object->hasPaint() || document.isEffectivelyLocked(id))
                continue;
            object->copyAppearance(style);
            // Type's style only lands on type: a path takes the paint alone.
            if (object->kind == ObjectKind::text && style.kind == ObjectKind::text)
                applyCharacterStyle(object->text, style.text);
        }
    });
}

void EditorSession::applyStyleTo(const QUuid &target)
{
    if (!m_document)
        return;
    const VectorObject *found = m_document->find(target);
    if (!found || !found->hasPaint() || m_document->isEffectivelyLocked(target))
        return;
    std::optional<VectorObject> source;
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *object = m_document->find(id);
        if (object && object->hasPaint() && id != target) {
            source = *object;
            break;
        }
    }
    edit(QStringLiteral("Eyedropper"), [&](VectorDocument &document) {
        VectorObject *object = document.find(target);
        if (source) {
            object->setFills(source->fills());
            object->setStrokes(source->strokes());
            object->opacity = source->opacity;
            if (object->kind == ObjectKind::text && source->kind == ObjectKind::text)
                applyCharacterStyle(object->text, source->text);
        } else {
            object->setFills({m_defaultFill});
            object->setStrokes({m_defaultStroke});
        }
    });
}
