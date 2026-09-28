#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"
#include "IO/PdfFunction.h"
#include <cmath>

namespace Pdf {

Paint Interpreter::paintFromShading(const Object &shadingObjectRaw, const QRectF &targetBounds, const QTransform &shadingToDocument, bool *ok)
{
    *ok = false;
    const Object shadingObject = m_document.resolve(shadingObjectRaw);
    if (!shadingObject.isDictionary() || !targetBounds.isValid() || targetBounds.isEmpty())
        return Paint::none();
    const Dict &dict = shadingObject.toDict();
    const int type = int(m_document.resolve(dict.value(QStringLiteral("ShadingType"))).toInt(0));
    if (type != 2 && type != 3) {
        warnOnce(QStringLiteral("mesh-shading"), QStringLiteral("A mesh shading was left out."));
        return Paint::none();
    }

    const ColorSpace colorSpace = ColorSpace::load(m_document, dict.value(QStringLiteral("ColorSpace")), m_warnings);
    const Function function = Function::load(m_document, dict.value(QStringLiteral("Function")));
    const Array coords = m_document.resolve(dict.value(QStringLiteral("Coords"))).toArray();
    const Array domainArray = m_document.resolve(dict.value(QStringLiteral("Domain"))).toArray();
    const double domain0 = domainArray.size() >= 2 ? domainArray[0].toReal(0) : 0;
    const double domain1 = domainArray.size() >= 2 ? domainArray[1].toReal(1) : 1;
    if (!function.isValid())
        return Paint::none();

    const auto fraction = [&](QPointF absolute) {
        return QPointF((absolute.x() - targetBounds.left()) / targetBounds.width(), (absolute.y() - targetBounds.top()) / targetBounds.height());
    };

    Paint paint;
    if (type == 2 && coords.size() >= 4) {
        paint.kind = PaintKind::linearGradient;
        paint.start = fraction(shadingToDocument.map(QPointF(coords[0].toReal(), coords[1].toReal())));
        paint.end = fraction(shadingToDocument.map(QPointF(coords[2].toReal(), coords[3].toReal())));
    } else if (type == 3 && coords.size() >= 6) {
        // Our gradient model is one circle growing from a center to a radius;
        // a PDF radial shading's two distinct circles are approximated by the
        // ending circle alone, which matches the common r0 == 0 case exactly.
        paint.kind = PaintKind::radialGradient;
        const QPointF center = shadingToDocument.map(QPointF(coords[3].toReal(), coords[4].toReal()));
        const double scale = std::hypot(shadingToDocument.m11(), shadingToDocument.m12());
        const QPointF edge = center + QPointF(coords[5].toReal(0) * scale, 0);
        paint.start = fraction(center);
        paint.end = fraction(edge);
    } else {
        return Paint::none();
    }

    constexpr int stopCount = 16;
    for (int i = 0; i <= stopCount; ++i) {
        const double t = domain0 + (domain1 - domain0) * (double(i) / stopCount);
        GradientStop stop;
        stop.offset = double(i) / stopCount;
        stop.color = colorSpace.toColor(function.evaluate({t}));
        paint.stops.push_back(stop);
    }
    if (paint.stops.empty())
        return Paint::none();
    paint.color = paint.stops.front().color;
    *ok = true;
    return paint;
}

void Interpreter::runShading(const Object &nameObject, const Dict &resources)
{
    const Dict shadingResources = m_document.resolve(resources.value(QStringLiteral("Shading"))).toDict();
    const Object shadingObject = shadingResources.value(QString::fromLatin1(nameObject.toNameValue()));
    if (!m_state.clipBounds.isValid() || m_state.clipBounds.isEmpty()) {
        warnOnce(QStringLiteral("unbounded-shading"), QStringLiteral("A shading fill outside any clip was left out."));
        return;
    }
    bool ok = false;
    const Paint paint = paintFromShading(shadingObject, m_state.clipBounds, m_state.ctm, &ok);
    if (!ok)
        return;

    QPainterPath rectPath;
    rectPath.addRect(m_state.clipBounds);
    VectorObject object;
    object.kind = ObjectKind::path;
    object.name = QStringLiteral("Shading");
    object.path = VectorPath::fromPainterPath(rectPath);
    object.fill = paint;
    object.fill.opacity = m_state.fillAlpha;
    object.stroke.paint = Paint::none();
    m_target.insert(std::move(object), m_state.insertionParent);
}

}
