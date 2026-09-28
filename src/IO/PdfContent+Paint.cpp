#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"
#include <algorithm>
#include <cmath>

namespace Pdf {

namespace {
double averageScale(const QTransform &t)
{
    const double det = t.m11() * t.m22() - t.m12() * t.m21();
    return std::sqrt(std::abs(det));
}
}

void Interpreter::setColorSpace(bool stroking, const Object &nameObject, const Dict &resources)
{
    const QByteArray name = nameObject.toNameValue();
    ColorSpace space;
    if (name == "DeviceGray" || name == "DeviceRGB" || name == "DeviceCMYK" || name == "Pattern") {
        space = ColorSpace::load(m_document, nameObject, m_warnings);
    } else {
        const Dict colorSpaceResources = m_document.resolve(resources.value(QStringLiteral("ColorSpace"))).toDict();
        space = ColorSpace::load(m_document, colorSpaceResources.value(QString::fromLatin1(name)), m_warnings);
    }
    if (space.isCmyk())
        warnOnce(QStringLiteral("cmyk"), QStringLiteral("CMYK colors were converted to RGB directly, without a color profile."));
    if (stroking) {
        m_state.strokeSpace = space;
        m_state.strokeComponents = space.defaultComponents();
        m_state.strokePatternName.clear();
    } else {
        m_state.fillSpace = space;
        m_state.fillComponents = space.defaultComponents();
        m_state.fillPatternName.clear();
    }
}

void Interpreter::setColor(bool stroking, const QList<Object> &operands)
{
    QList<double> numbers;
    QByteArray patternName;
    for (const Object &operand : operands) {
        if (operand.isNumber())
            numbers.append(operand.toReal());
        else if (operand.isName())
            patternName = operand.toNameValue();
    }
    if (stroking) {
        if (!patternName.isEmpty())
            m_state.strokePatternName = patternName;
        if (!numbers.isEmpty())
            m_state.strokeComponents = numbers;
    } else {
        if (!patternName.isEmpty())
            m_state.fillPatternName = patternName;
        if (!numbers.isEmpty())
            m_state.fillComponents = numbers;
    }
}

Paint Interpreter::resolvePaint(bool stroking, const QRectF &bounds, const Dict &resources)
{
    const ColorSpace &space = stroking ? m_state.strokeSpace : m_state.fillSpace;
    if (space.isPattern()) {
        const QByteArray &patternName = stroking ? m_state.strokePatternName : m_state.fillPatternName;
        const Dict patternResources = m_document.resolve(resources.value(QStringLiteral("Pattern"))).toDict();
        const Object patternObject = m_document.resolve(patternResources.value(QString::fromLatin1(patternName)));
        const int patternType = int(m_document.resolve(patternObject.at(QStringLiteral("PatternType"))).toInt(0));
        if (patternType == 2) {
            const Array matrixArray = m_document.resolve(patternObject.at(QStringLiteral("Matrix"))).toArray();
            QTransform patternMatrix;
            if (matrixArray.size() == 6)
                patternMatrix = QTransform(matrixArray[0].toReal(1), matrixArray[1].toReal(0), matrixArray[2].toReal(0),
                                            matrixArray[3].toReal(1), matrixArray[4].toReal(0), matrixArray[5].toReal(0));
            bool ok = false;
            const Paint paint =
                paintFromShading(patternObject.at(QStringLiteral("Shading")), bounds, patternMatrix * m_state.ctm, &ok);
            if (ok)
                return paint;
        } else {
            warnOnce(QStringLiteral("tiling-pattern"), QStringLiteral("Tiling patterns were filled with a flat color."));
        }
        const ColorSpace *base = space.patternBase();
        const QColor fallback = base ? base->toColor(stroking ? m_state.strokeComponents : m_state.fillComponents) : QColor(Qt::gray);
        return Paint::solid(fallback);
    }
    return Paint::solid(space.toColor(stroking ? m_state.strokeComponents : m_state.fillComponents));
}

void Interpreter::paintPath(bool fill, bool stroke, Qt::FillRule rule, const Dict &resources)
{
    if ((fill || stroke) && !m_path.isEmpty()) {
        VectorPath path = VectorPath::fromPainterPath(m_path);
        path.fillRule = rule;
        const QRectF bounds = m_path.boundingRect();

        VectorObject object;
        object.kind = ObjectKind::path;
        object.name = QStringLiteral("Path");
        object.path = std::move(path);
        object.fill = fill ? resolvePaint(false, bounds, resources) : Paint::none();
        if (fill)
            object.fill.opacity = m_state.fillAlpha;
        if (stroke) {
            object.stroke.paint = resolvePaint(true, bounds, resources);
            object.stroke.paint.opacity = m_state.strokeAlpha;
            const double scale = averageScale(m_state.ctm);
            object.stroke.width = std::max(0.01, m_state.lineWidth * scale);
            object.stroke.cap = m_state.cap;
            object.stroke.join = m_state.join;
            object.stroke.miterLimit = m_state.miterLimit;
            for (const double dash : m_state.dashArray)
                object.stroke.dashes.push_back(std::max(0.0, dash * scale));
        } else {
            object.stroke.paint = Paint::none();
        }
        object.blendMode = m_state.blendMode;
        if (object.hasVisibleFill() || object.hasVisibleStroke())
            m_target.insert(std::move(object), m_state.insertionParent);
    }
    applyPendingClip();
    m_path = QPainterPath();
    m_hasOpenSubpath = false;
}

void Interpreter::applyPendingClip()
{
    if (!m_pendingClip.active)
        return;
    m_pendingClip.active = false;
    if (m_path.isEmpty())
        return;
    VectorPath clipPath = VectorPath::fromPainterPath(m_path);
    clipPath.fillRule = m_pendingClip.rule;
    m_state.insertionParent = clipGroupFor(clipPath, m_pendingClip.rule);
    const QRectF newBounds = m_path.boundingRect();
    m_state.clipBounds = m_state.clipBounds.isValid() ? m_state.clipBounds.intersected(newBounds) : newBounds;
}

QUuid Interpreter::clipGroupFor(const VectorPath &clipPath, Qt::FillRule rule)
{
    VectorObject group;
    group.kind = ObjectKind::group;
    group.isClipGroup = true;
    group.name = QStringLiteral("Clip Group");
    const QUuid groupId = group.id;
    m_target.insert(std::move(group), m_state.insertionParent);

    VectorObject clip;
    clip.kind = ObjectKind::path;
    clip.name = QStringLiteral("Clipping Path");
    clip.path = clipPath;
    clip.path.fillRule = rule;
    clip.fill = Paint::none();
    clip.stroke.paint = Paint::none();
    m_target.insert(std::move(clip), groupId);
    return groupId;
}

}
