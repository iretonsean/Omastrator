#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"

namespace Pdf {

std::shared_ptr<Font> Interpreter::fontFor(const Dict &resources, const QByteArray &name)
{
    const Dict fontResources = m_document.resolve(resources.value(QStringLiteral("Font"))).toDict();
    const Object fontReference = fontResources.value(QString::fromLatin1(name));
    const QString key = fontReference.isReference() ? QStringLiteral("ref:%1").arg(fontReference.toReference().number)
                                                     : QStringLiteral("name:%1").arg(QString::fromLatin1(name));
    const auto cached = m_fontCache.constFind(key);
    if (cached != m_fontCache.constEnd())
        return cached.value();
    auto font = std::make_shared<Font>(Font::load(m_document, fontReference, m_warnings));
    m_fontCache.insert(key, font);
    return font;
}

void Interpreter::op_Tf(const QList<Object> &operands, const Dict &resources)
{
    const QByteArray name = operands[operands.size() - 2].toNameValue();
    m_state.fontSize = operands.last().toReal(12);
    const auto font = fontFor(resources, name);
    m_state.font = font;
    m_state.fontFamily = font->family;
    m_state.fontStyle = font->style;
    if (font->isMissingFamily && !font->rawBaseFont.isEmpty()) {
        warnOnce(QStringLiteral("font:") + font->rawBaseFont,
                 QStringLiteral("The font “%1” isn't installed; text using it was imported with a substitute.").arg(font->rawBaseFont));
    }
}

void Interpreter::appendToLine(const QString &text, const CharacterFormat &format)
{
    if (text.isEmpty())
        return;
    if (!m_pendingLine.valid) {
        m_pendingLine.valid = true;
        m_pendingLine.placement = m_textMatrix * m_state.ctm;
    }
    const int start = int(m_pendingLine.text.length());
    m_pendingLine.text += text;
    if (!m_pendingLine.runs.empty()) {
        TextRun &last = m_pendingLine.runs.back();
        const CharacterFormat &lastFormat = last.format;
        const bool sameFormat = lastFormat.family == format.family && lastFormat.style == format.style
            && qFuzzyCompare(lastFormat.size, format.size) && lastFormat.fill == format.fill;
        if (sameFormat) {
            last.length += int(text.length());
            return;
        }
    }
    TextRun run;
    run.start = start;
    run.length = int(text.length());
    run.format = format;
    m_pendingLine.runs.push_back(run);
}

void Interpreter::flushTextLine()
{
    if (!m_pendingLine.valid || m_pendingLine.text.isEmpty() || m_pendingLine.runs.empty()) {
        m_pendingLine = PendingLine();
        return;
    }
    VectorObject object;
    object.kind = ObjectKind::text;
    object.name = m_pendingLine.text.left(24);
    object.text.text = m_pendingLine.text;
    object.text.family = m_pendingLine.runs.front().format.family;
    object.text.style = m_pendingLine.runs.front().format.style;
    object.text.size = m_pendingLine.runs.front().format.size;
    for (auto &run : m_pendingLine.runs)
        object.text.runs.push_back(run);
    object.transform = m_pendingLine.placement;
    object.fill = Paint::solid(m_pendingLine.runs.front().format.fill.value_or(QColor(Qt::black)));
    object.stroke.paint = Paint::none();
    m_target.insert(std::move(object), m_state.insertionParent);
    m_pendingLine = PendingLine();
}

void Interpreter::showText(const QByteArray &bytes, const Dict &)
{
    if (m_state.renderMode == 7) {
        warnOnce(QStringLiteral("clip-text"), QStringLiteral("Text used to clip other artwork was left out."));
        return;
    }
    if (!m_state.font)
        return;

    bool hadUnmapped = false;
    const QString decoded = m_state.font->decode(bytes, &hadUnmapped);
    if (hadUnmapped)
        warnOnce(QStringLiteral("missing-unicode"), QStringLiteral("Some text had no Unicode mapping and was imported as “?”."));
    if (m_state.renderMode == 3) {
        warnOnce(QStringLiteral("invisible-text"),
                 QStringLiteral("Invisible text (often an OCR layer) was imported as regular visible text."));
    }

    const bool useStrokeColor = m_state.renderMode == 1 || m_state.renderMode == 5;
    const QColor color = useStrokeColor ? m_state.strokeSpace.toColor(m_state.strokeComponents) : m_state.fillSpace.toColor(m_state.fillComponents);

    CharacterFormat format;
    format.family = m_state.fontFamily;
    format.style = m_state.fontStyle;
    format.size = m_state.fontSize;
    format.baselineShift = m_state.rise;
    format.fill = color;
    appendToLine(decoded, format);
}

void Interpreter::showTextArray(const Array &array, const Dict &resources)
{
    for (const Object &item : array) {
        if (item.isString()) {
            showText(item.toStringValue(), resources);
        } else if (item.isNumber()) {
            // A large negative adjustment (1/1000 text space unit) usually
            // simulates a word gap instead of a literal space character.
            if (item.toReal() <= -150) {
                CharacterFormat format;
                format.family = m_state.fontFamily;
                format.style = m_state.fontStyle;
                format.size = m_state.fontSize;
                format.baselineShift = m_state.rise;
                format.fill = m_state.fillSpace.toColor(m_state.fillComponents);
                appendToLine(QStringLiteral(" "), format);
            }
        }
    }
}

}
