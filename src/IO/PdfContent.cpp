#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"
#include "IO/PdfLexer.h"
#include <algorithm>
#include <cmath>

namespace Pdf {

namespace {
// VectorDocument::insert is linear now, so these bound memory and time, not a slow insert.
constexpr size_t maximumImportedObjects = 250'000;
constexpr int maximumFormRuns = 100'000;

Qt::PenCapStyle capFromInt(int value)
{
    switch (value) {
    case 1:
        return Qt::RoundCap;
    case 2:
        return Qt::SquareCap;
    default:
        return Qt::FlatCap;
    }
}
Qt::PenJoinStyle joinFromInt(int value)
{
    switch (value) {
    case 1:
        return Qt::RoundJoin;
    case 2:
        return Qt::BevelJoin;
    default:
        return Qt::MiterJoin;
    }
}
LayerBlendMode blendModeFromName(const QByteArray &name)
{
    if (name == "Multiply")
        return LayerBlendMode::multiply;
    if (name == "Screen")
        return LayerBlendMode::screen;
    if (name == "Overlay" || name == "HardLight")
        return LayerBlendMode::overlay;
    if (name == "Darken")
        return LayerBlendMode::darken;
    if (name == "Lighten")
        return LayerBlendMode::lighten;
    if (name == "ColorDodge")
        return LayerBlendMode::colorDodge;
    if (name == "ColorBurn")
        return LayerBlendMode::colorBurn;
    if (name == "SoftLight")
        return LayerBlendMode::softLight;
    if (name == "Difference" || name == "Exclusion")
        return LayerBlendMode::difference;
    if (name == "Hue")
        return LayerBlendMode::hue;
    if (name == "Saturation")
        return LayerBlendMode::saturation;
    if (name == "Color")
        return LayerBlendMode::color;
    if (name == "Luminosity")
        return LayerBlendMode::luminosity;
    return LayerBlendMode::normal;
}
QString inlineImageKey(const QByteArray &abbreviation)
{
    static const QHash<QByteArray, QString> names{
        {"BPC", QStringLiteral("BitsPerComponent")}, {"CS", QStringLiteral("ColorSpace")}, {"D", QStringLiteral("Decode")},
        {"DP", QStringLiteral("DecodeParms")},       {"F", QStringLiteral("Filter")},      {"H", QStringLiteral("Height")},
        {"IM", QStringLiteral("ImageMask")},         {"I", QStringLiteral("Interpolate")}, {"W", QStringLiteral("Width")},
        {"L", QStringLiteral("Length")},
    };
    return names.value(abbreviation, QString::fromLatin1(abbreviation));
}
}

Interpreter::Interpreter(const Document &document, VectorDocument &target, QStringList *warnings)
    : m_document(document), m_target(target), m_warnings(warnings)
{
}

void Interpreter::warnOnce(const QString &key, const QString &message)
{
    if (m_warnedOnce.contains(key))
        return;
    m_warnedOnce.insert(key);
    if (m_warnings)
        *m_warnings << message;
}

void Interpreter::runPage(const QByteArray &content, const Dict &resources, const QTransform &pageTransform, const QRectF &pageBounds,
                           const QUuid &pageLayer)
{
    if (m_tooComplex)
        return;
    m_stack.clear();
    m_ignoredSaves = 0;
    m_state = GraphicsState();
    m_state.ctm = pageTransform;
    m_state.insertionParent = pageLayer;
    m_state.clipBounds = pageBounds;
    m_path = QPainterPath();
    m_hasOpenSubpath = false;
    m_pendingClip = PendingClip();
    m_pendingLine = PendingLine();
    m_textMatrix = QTransform();
    m_lineMatrix = QTransform();
    m_ocgRestore.clear();
    m_ignoredMarks = 0;
    runContent(content, resources);
}

void Interpreter::runContent(const QByteArray &content, const Dict &resources)
{
    Lexer lexer(content, 0);
    QList<Object> operands;

    constexpr qint64 maximumOperators = 20'000'000;
    constexpr int maximumOperands = 1024;
    constexpr qsizetype maximumSavedStates = 1000;
    while (!m_tooComplex) {
        const Token peeked = lexer.peek();
        if (peeked.kind == TokenKind::end)
            break;

        if (peeked.kind == TokenKind::keyword && peeked.bytes == "BI") {
            lexer.next();
            Dict dict;
            while (true) {
                const Token keyToken = lexer.next();
                if (keyToken.kind == TokenKind::end || (keyToken.kind == TokenKind::keyword && keyToken.bytes == "ID"))
                    break;
                if (keyToken.kind != TokenKind::name)
                    continue;
                dict.insert(inlineImageKey(keyToken.bytes), parseObject(lexer));
            }
            qint64 expectedLength = -1;
            if (dict.value(QStringLiteral("Length")).isNumber())
                expectedLength = dict.value(QStringLiteral("Length")).toInt(-1);
            else if (!dict.contains(QStringLiteral("Filter"))) {
                const int width = dict.value(QStringLiteral("Width")).toInt(0);
                const int height = dict.value(QStringLiteral("Height")).toInt(0);
                const bool isMask = dict.value(QStringLiteral("ImageMask")).toBool(false);
                const int bitsPerComponent = isMask ? 1 : std::clamp<int>(int(dict.value(QStringLiteral("BitsPerComponent")).toInt(8)), 1, 32);
                const QByteArray csName = dict.value(QStringLiteral("ColorSpace")).toNameValue();
                const int components = isMask ? 1 : (csName == "RGB" || csName == "DeviceRGB") ? 3 : (csName == "CMYK" || csName == "DeviceCMYK") ? 4 : 1;
                if (width > 0 && height > 0 && width <= (1 << 24) && height <= (1 << 24)) // a larger claim would overflow
                    expectedLength = (qint64(width) * components * bitsPerComponent + 7) / 8 * height;
            }
            const QByteArray raw = lexer.captureInlineImageData(expectedLength);
            runInlineImage(dict, raw, resources);
            operands.clear();
            continue;
        }

        if (peeked.kind != TokenKind::keyword) {
            operands.append(parseObject(lexer));
            if (operands.size() > maximumOperands)
                operands.removeFirst(); // no operator takes this many; keeps operand junk from eating memory
            continue;
        }
        lexer.next();
        const QByteArray &op = peeked.bytes;
        if (++m_operatorCount > maximumOperators || m_target.objects.size() > maximumImportedObjects) {
            m_tooComplex = true;
            warnOnce(QStringLiteral("too-complex"), QStringLiteral("The file was too complex; some artwork was left out."));
            break;
        }

        if (op == "q") {
            if (m_stack.size() >= maximumSavedStates) {
                ++m_ignoredSaves;
                warnOnce(QStringLiteral("q-depth"), QStringLiteral("Deeply nested graphics states were flattened."));
            } else {
                m_stack.push(m_state);
            }
        } else if (op == "Q") {
            if (m_ignoredSaves > 0)
                --m_ignoredSaves;
            else if (!m_stack.isEmpty())
                m_state = m_stack.pop();
        } else if (op == "cm" && operands.size() == 6) {
            const QTransform m(operands[0].toReal(1), operands[1].toReal(0), operands[2].toReal(0), operands[3].toReal(1),
                                operands[4].toReal(0), operands[5].toReal(0));
            m_state.ctm = m * m_state.ctm;
        } else if (op == "w" && !operands.isEmpty()) {
            m_state.lineWidth = operands.last().toReal(1);
        } else if (op == "J" && !operands.isEmpty()) {
            m_state.cap = capFromInt(int(operands.last().toInt()));
        } else if (op == "j" && !operands.isEmpty()) {
            m_state.join = joinFromInt(int(operands.last().toInt()));
        } else if (op == "M" && !operands.isEmpty()) {
            m_state.miterLimit = operands.last().toReal(10);
        } else if (op == "d" && operands.size() >= 2) {
            m_state.dashArray.clear();
            for (const Object &item : operands[operands.size() - 2].toArray())
                m_state.dashArray.append(item.toReal());
        } else if (op == "gs" && !operands.isEmpty()) {
            applyExtGState(operands.last(), resources);
        } else if (op == "cs") {
            if (!operands.isEmpty())
                setColorSpace(false, operands.last(), resources);
        } else if (op == "CS") {
            if (!operands.isEmpty())
                setColorSpace(true, operands.last(), resources);
        } else if (op == "sc" || op == "scn") {
            setColor(false, operands);
        } else if (op == "SC" || op == "SCN") {
            setColor(true, operands);
        } else if (op == "g" && !operands.isEmpty()) {
            m_state.fillSpace = ColorSpace::deviceGray();
            m_state.fillComponents = {operands.last().toReal()};
            m_state.fillPatternName.clear();
        } else if (op == "G" && !operands.isEmpty()) {
            m_state.strokeSpace = ColorSpace::deviceGray();
            m_state.strokeComponents = {operands.last().toReal()};
            m_state.strokePatternName.clear();
        } else if (op == "rg" && operands.size() >= 3) {
            m_state.fillSpace = ColorSpace::deviceRGB();
            m_state.fillComponents = {operands[0].toReal(), operands[1].toReal(), operands[2].toReal()};
            m_state.fillPatternName.clear();
        } else if (op == "RG" && operands.size() >= 3) {
            m_state.strokeSpace = ColorSpace::deviceRGB();
            m_state.strokeComponents = {operands[0].toReal(), operands[1].toReal(), operands[2].toReal()};
            m_state.strokePatternName.clear();
        } else if (op == "k" && operands.size() >= 4) {
            m_state.fillSpace = ColorSpace::deviceCMYK();
            m_state.fillComponents = {operands[0].toReal(), operands[1].toReal(), operands[2].toReal(), operands[3].toReal()};
            m_state.fillPatternName.clear();
            warnOnce(QStringLiteral("cmyk"), QStringLiteral("CMYK colors were converted to RGB directly, without a color profile."));
        } else if (op == "K" && operands.size() >= 4) {
            m_state.strokeSpace = ColorSpace::deviceCMYK();
            m_state.strokeComponents = {operands[0].toReal(), operands[1].toReal(), operands[2].toReal(), operands[3].toReal()};
            m_state.strokePatternName.clear();
            warnOnce(QStringLiteral("cmyk"), QStringLiteral("CMYK colors were converted to RGB directly, without a color profile."));
        } else if (op == "m" && operands.size() >= 2) {
            m_currentPoint = m_state.ctm.map(QPointF(operands[operands.size() - 2].toReal(), operands.last().toReal()));
            m_subpathStart = m_currentPoint;
            m_path.moveTo(m_currentPoint);
            m_hasOpenSubpath = true;
        } else if (op == "l" && operands.size() >= 2) {
            m_currentPoint = m_state.ctm.map(QPointF(operands[operands.size() - 2].toReal(), operands.last().toReal()));
            if (!m_hasOpenSubpath) {
                m_path.moveTo(m_currentPoint);
                m_hasOpenSubpath = true;
            } else {
                m_path.lineTo(m_currentPoint);
            }
        } else if (op == "c" && operands.size() >= 6) {
            const QPointF c1 = m_state.ctm.map(QPointF(operands[0].toReal(), operands[1].toReal()));
            const QPointF c2 = m_state.ctm.map(QPointF(operands[2].toReal(), operands[3].toReal()));
            const QPointF end = m_state.ctm.map(QPointF(operands[4].toReal(), operands[5].toReal()));
            if (!m_hasOpenSubpath) {
                m_path.moveTo(c1);
                m_hasOpenSubpath = true;
            }
            m_path.cubicTo(c1, c2, end);
            m_currentPoint = end;
        } else if (op == "v" && operands.size() >= 4) {
            const QPointF c2 = m_state.ctm.map(QPointF(operands[0].toReal(), operands[1].toReal()));
            const QPointF end = m_state.ctm.map(QPointF(operands[2].toReal(), operands[3].toReal()));
            if (!m_hasOpenSubpath) {
                m_path.moveTo(m_currentPoint);
                m_hasOpenSubpath = true;
            }
            m_path.cubicTo(m_currentPoint, c2, end);
            m_currentPoint = end;
        } else if (op == "y" && operands.size() >= 4) {
            const QPointF c1 = m_state.ctm.map(QPointF(operands[0].toReal(), operands[1].toReal()));
            const QPointF end = m_state.ctm.map(QPointF(operands[2].toReal(), operands[3].toReal()));
            if (!m_hasOpenSubpath) {
                m_path.moveTo(c1);
                m_hasOpenSubpath = true;
            }
            m_path.cubicTo(c1, end, end);
            m_currentPoint = end;
        } else if (op == "h") {
            if (m_hasOpenSubpath)
                m_path.closeSubpath();
            m_currentPoint = m_subpathStart;
        } else if (op == "re" && operands.size() >= 4) {
            const double x = operands[0].toReal(), y = operands[1].toReal(), w = operands[2].toReal(), h = operands[3].toReal();
            const QPointF p0 = m_state.ctm.map(QPointF(x, y));
            m_path.moveTo(p0);
            m_path.lineTo(m_state.ctm.map(QPointF(x + w, y)));
            m_path.lineTo(m_state.ctm.map(QPointF(x + w, y + h)));
            m_path.lineTo(m_state.ctm.map(QPointF(x, y + h)));
            m_path.closeSubpath();
            m_currentPoint = p0;
            m_subpathStart = p0;
            m_hasOpenSubpath = true;
        } else if (op == "S") {
            paintPath(false, true, Qt::WindingFill, resources);
        } else if (op == "s") {
            m_path.closeSubpath();
            paintPath(false, true, Qt::WindingFill, resources);
        } else if (op == "f" || op == "F") {
            paintPath(true, false, Qt::WindingFill, resources);
        } else if (op == "f*") {
            paintPath(true, false, Qt::OddEvenFill, resources);
        } else if (op == "B") {
            paintPath(true, true, Qt::WindingFill, resources);
        } else if (op == "B*") {
            paintPath(true, true, Qt::OddEvenFill, resources);
        } else if (op == "b") {
            m_path.closeSubpath();
            paintPath(true, true, Qt::WindingFill, resources);
        } else if (op == "b*") {
            m_path.closeSubpath();
            paintPath(true, true, Qt::OddEvenFill, resources);
        } else if (op == "n") {
            paintPath(false, false, Qt::WindingFill, resources);
        } else if (op == "W") {
            m_pendingClip.active = true;
            m_pendingClip.rule = Qt::WindingFill;
        } else if (op == "W*") {
            m_pendingClip.active = true;
            m_pendingClip.rule = Qt::OddEvenFill;
        } else if (op == "Do" && !operands.isEmpty()) {
            doXObject(operands.last(), resources);
        } else if (op == "sh" && !operands.isEmpty()) {
            runShading(operands.last(), resources);
        } else if (op == "BT") {
            m_textMatrix = QTransform();
            m_lineMatrix = QTransform();
            m_pendingLine = PendingLine();
        } else if (op == "ET") {
            flushTextLine();
        } else if (op == "Tf" && operands.size() >= 2) {
            op_Tf(operands, resources);
        } else if (op == "Td" && operands.size() >= 2) {
            flushTextLine();
            m_lineMatrix = QTransform::fromTranslate(operands[operands.size() - 2].toReal(), operands.last().toReal()) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
        } else if (op == "TD" && operands.size() >= 2) {
            flushTextLine();
            m_state.leading = -operands.last().toReal();
            m_lineMatrix = QTransform::fromTranslate(operands[operands.size() - 2].toReal(), operands.last().toReal()) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
        } else if (op == "Tm" && operands.size() >= 6) {
            flushTextLine();
            m_lineMatrix = QTransform(operands[0].toReal(1), operands[1].toReal(0), operands[2].toReal(0), operands[3].toReal(1),
                                       operands[4].toReal(0), operands[5].toReal(0));
            m_textMatrix = m_lineMatrix;
        } else if (op == "T*") {
            flushTextLine();
            m_lineMatrix = QTransform::fromTranslate(0, -m_state.leading) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
        } else if (op == "Tc" && !operands.isEmpty()) {
            m_state.charSpace = operands.last().toReal();
        } else if (op == "Tw" && !operands.isEmpty()) {
            m_state.wordSpace = operands.last().toReal();
        } else if (op == "Tz" && !operands.isEmpty()) {
            m_state.hScale = operands.last().toReal(100);
        } else if (op == "TL" && !operands.isEmpty()) {
            m_state.leading = operands.last().toReal();
        } else if (op == "Ts" && !operands.isEmpty()) {
            m_state.rise = operands.last().toReal();
        } else if (op == "Tr" && !operands.isEmpty()) {
            m_state.renderMode = int(operands.last().toInt());
        } else if (op == "Tj" && !operands.isEmpty()) {
            showText(operands.last().toStringValue(), resources);
        } else if (op == "'" && !operands.isEmpty()) {
            flushTextLine();
            m_lineMatrix = QTransform::fromTranslate(0, -m_state.leading) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
            showText(operands.last().toStringValue(), resources);
        } else if (op == "\"" && operands.size() >= 3) {
            m_state.wordSpace = operands[operands.size() - 3].toReal();
            m_state.charSpace = operands[operands.size() - 2].toReal();
            flushTextLine();
            m_lineMatrix = QTransform::fromTranslate(0, -m_state.leading) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
            showText(operands.last().toStringValue(), resources);
        } else if (op == "TJ" && !operands.isEmpty()) {
            showTextArray(operands.last().toArray(), resources);
        } else if (op == "BDC") {
            beginMarkedContent(operands, resources);
        } else if (op == "BMC") {
            m_ocgRestore.push(m_state.insertionParent);
        } else if (op == "EMC") {
            endMarkedContent();
        }
        // MP, DP, BX, EX, ri, i and any unrecognised operator: no-op.
        operands.clear();
    }
}

void Interpreter::applyExtGState(const Object &nameObject, const Dict &resources)
{
    const Dict gsResources = m_document.resolve(resources.value(QStringLiteral("ExtGState"))).toDict();
    const Object gsObject = m_document.resolve(gsResources.value(QString::fromLatin1(nameObject.toNameValue())));
    if (!gsObject.isDictionary())
        return;
    const Dict &dict = gsObject.toDict();
    if (dict.contains(QStringLiteral("ca")))
        m_state.fillAlpha = m_document.resolve(dict.value(QStringLiteral("ca"))).toReal(1);
    if (dict.contains(QStringLiteral("CA")))
        m_state.strokeAlpha = m_document.resolve(dict.value(QStringLiteral("CA"))).toReal(1);
    if (dict.contains(QStringLiteral("LW")))
        m_state.lineWidth = m_document.resolve(dict.value(QStringLiteral("LW"))).toReal(m_state.lineWidth);
    if (dict.contains(QStringLiteral("BM"))) {
        const Object blendModeObject = m_document.resolve(dict.value(QStringLiteral("BM")));
        const QByteArray name =
            blendModeObject.isArray() && !blendModeObject.toArray().isEmpty() ? blendModeObject.toArray()[0].toNameValue() : blendModeObject.toNameValue();
        m_state.blendMode = blendModeFromName(name);
    }
    if (dict.contains(QStringLiteral("SMask"))) {
        const Object softMask = m_document.resolve(dict.value(QStringLiteral("SMask")));
        if (!softMask.isName() || softMask.toNameValue() != "None")
            warnOnce(QStringLiteral("smask"), QStringLiteral("A soft mask effect was left out."));
    }
}

void Interpreter::doXObject(const Object &nameObject, const Dict &resources)
{
    const Dict xObjectResources = m_document.resolve(resources.value(QStringLiteral("XObject"))).toDict();
    const Object xObject = m_document.resolve(xObjectResources.value(QString::fromLatin1(nameObject.toNameValue())));
    if (!xObject.isStream())
        return;
    const QByteArray subtype = m_document.resolve(xObject.at(QStringLiteral("Subtype"))).toNameValue();
    if (subtype == "Form")
        runForm(xObject, resources);
    else if (subtype == "Image")
        runImage(xObject, resources);
}

void Interpreter::runForm(const Object &formObject, const Dict &callerResources)
{
    if (m_formDepth > 12) {
        warnOnce(QStringLiteral("form-depth"), QStringLiteral("Deeply nested artwork was left out."));
        return;
    }
    if (m_tooComplex)
        return;
    if (++m_formRuns > maximumFormRuns || m_target.objects.size() > maximumImportedObjects) {
        m_tooComplex = true;
        warnOnce(QStringLiteral("too-complex"), QStringLiteral("The file was too complex; some artwork was left out."));
        return;
    }
    ++m_formDepth;
    const Dict &dict = formObject.toDict();
    const GraphicsState savedState = m_state;
    const QPainterPath savedPath = m_path;
    const bool savedHasOpenSubpath = m_hasOpenSubpath;
    m_path = QPainterPath();
    m_hasOpenSubpath = false;

    const Array matrixArray = m_document.resolve(dict.value(QStringLiteral("Matrix"))).toArray();
    if (matrixArray.size() == 6) {
        const QTransform matrix(matrixArray[0].toReal(1), matrixArray[1].toReal(0), matrixArray[2].toReal(0), matrixArray[3].toReal(1),
                                 matrixArray[4].toReal(0), matrixArray[5].toReal(0));
        m_state.ctm = matrix * m_state.ctm;
    }

    const Array bboxArray = m_document.resolve(dict.value(QStringLiteral("BBox"))).toArray();
    if (bboxArray.size() == 4) {
        const double x0 = bboxArray[0].toReal(), y0 = bboxArray[1].toReal(), x1 = bboxArray[2].toReal(), y1 = bboxArray[3].toReal();
        QPainterPath clipPath;
        clipPath.moveTo(m_state.ctm.map(QPointF(x0, y0)));
        clipPath.lineTo(m_state.ctm.map(QPointF(x1, y0)));
        clipPath.lineTo(m_state.ctm.map(QPointF(x1, y1)));
        clipPath.lineTo(m_state.ctm.map(QPointF(x0, y1)));
        clipPath.closeSubpath();
        m_state.insertionParent = clipGroupFor(VectorPath::fromPainterPath(clipPath), Qt::WindingFill);
        const QRectF newBounds = clipPath.boundingRect();
        m_state.clipBounds = m_state.clipBounds.isValid() ? m_state.clipBounds.intersected(newBounds) : newBounds;
    }

    Dict formResources = m_document.resolve(dict.value(QStringLiteral("Resources"))).toDict();
    if (formResources.isEmpty())
        formResources = callerResources;

    const PdfFilters::Decoded decoded = m_document.streamData(formObject);
    if (m_warnings)
        *m_warnings << decoded.warnings;
    runContent(decoded.bytes, formResources);

    m_state = savedState;
    m_path = savedPath;
    m_hasOpenSubpath = savedHasOpenSubpath;
    --m_formDepth;
}

void Interpreter::runImage(const Object &imageObject, const Dict &resources)
{
    const QImage image = decodeImageObject(imageObject, resources);
    if (image.isNull())
        return;
    VectorObject object;
    object.kind = ObjectKind::image;
    object.name = QStringLiteral("Image");
    object.image = image;
    const QTransform pixelToUnit(1.0 / image.width(), 0, 0, -1.0 / image.height(), 0, 1);
    object.transform = pixelToUnit * m_state.ctm;
    object.fill = Paint::none();
    object.stroke.paint = Paint::none();
    object.opacity = m_state.fillAlpha;
    m_target.insert(std::move(object), m_state.insertionParent);
}

void Interpreter::runInlineImage(const Dict &dict, const QByteArray &rawData, const Dict &resources)
{
    runImage(Object::stream(dict, rawData), resources);
}

void Interpreter::beginMarkedContent(const QList<Object> &operands, const Dict &resources)
{
    if (m_ocgRestore.size() >= 1000) {
        ++m_ignoredMarks;
        return;
    }
    m_ocgRestore.push(m_state.insertionParent);
    if (operands.size() < 2 || operands[operands.size() - 2].toNameValue() != "OC")
        return;

    const Object propertyOperand = operands.last();
    int ocgNumber = -1;
    Object ocg;
    if (propertyOperand.isName()) {
        const Dict properties = m_document.resolve(resources.value(QStringLiteral("Properties"))).toDict();
        const Object reference = properties.value(QString::fromLatin1(propertyOperand.toNameValue()));
        if (reference.isReference())
            ocgNumber = reference.toReference().number;
        ocg = m_document.resolve(reference);
    }
    if (ocgNumber < 0 || !ocg.isDictionary())
        return;

    const auto existing = m_ocgLayers.constFind(ocgNumber);
    QUuid layerId;
    if (existing != m_ocgLayers.constEnd()) {
        layerId = existing.value();
    } else {
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = QString::fromUtf8(m_document.resolve(ocg.at(QStringLiteral("Name"))).toStringValue());
        if (layer.name.isEmpty())
            layer.name = QStringLiteral("Layer");
        layer.layerColor = nextLayerColor(int(m_target.layers().size()));
        layer.isVisible = !isOcgHidden(ocgNumber);
        layerId = layer.id;
        m_target.objects.push_back(layer);
        m_ocgLayers.insert(ocgNumber, layerId);
    }
    m_state.insertionParent = layerId;
}

void Interpreter::endMarkedContent()
{
    if (m_ignoredMarks > 0) {
        --m_ignoredMarks;
        return;
    }
    if (m_ocgRestore.isEmpty())
        return;
    m_state.insertionParent = m_ocgRestore.pop();
}

bool Interpreter::isOcgHidden(int ocgObjectNumber) const
{
    const Object properties = m_document.resolve(m_document.catalog().at(QStringLiteral("OCProperties")));
    if (!properties.isDictionary())
        return false;
    const Object defaultConfig = m_document.resolve(properties.at(QStringLiteral("D")));
    for (const Object &entry : m_document.resolve(defaultConfig.at(QStringLiteral("OFF"))).toArray()) {
        if (entry.isReference() && entry.toReference().number == ocgObjectNumber)
            return true;
    }
    return false;
}

}
