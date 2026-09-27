#include "IO/SvgExporter.h"
#include "Logging.h"
#include "Document/TextLayout.h"
#include <QFontDatabase>
#include <QBuffer>
#include <QLineF>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamWriter>
#include <cmath>

namespace {
// Three decimals is a thousandth of a point, finer than any output device.
QString number(double value)
{
    const double rounded = std::round(value * 1000) / 1000;
    QString text = QString::number(rounded == 0 ? 0.0 : rounded, 'f', 3);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}

QString point(QPointF p)
{
    return number(p.x()) + QLatin1Char(' ') + number(p.y());
}

QString pathData(const VectorPath &path)
{
    QStringList parts;
    for (const Contour &contour : path.contours) {
        if (contour.nodes.empty())
            continue;
        parts << QStringLiteral("M") + point(contour.nodes.front().anchor);
        auto segment = [&](const PathNode &from, const PathNode &to) {
            if (!from.hasOut() && !to.hasIn())
                parts << QStringLiteral("L") + point(to.anchor);
            else
                parts << QStringLiteral("C") + point(from.out) + QLatin1Char(' ') + point(to.in) + QLatin1Char(' ') + point(to.anchor);
        };
        for (size_t index = 1; index < contour.nodes.size(); ++index)
            segment(contour.nodes[index - 1], contour.nodes[index]);
        if (contour.closed) {
            // A straight closing side is Z's own line.
            const PathNode &last = contour.nodes.back(), &first = contour.nodes.front();
            if (contour.nodes.size() > 1 && (last.hasOut() || first.hasIn()))
                segment(last, first);
            parts << QStringLiteral("Z");
        }
    }
    return parts.join(QLatin1Char(' '));
}

QString pathData(const QPainterPath &path)
{
    QStringList parts;
    QPointF start;
    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element e = path.elementAt(index);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            start = e;
            parts << QStringLiteral("M") + point(e);
            break;
        case QPainterPath::LineToElement:
            parts << QStringLiteral("L") + point(e);
            break;
        case QPainterPath::CurveToElement:
            if (index + 2 < path.elementCount()) {
                parts << QStringLiteral("C") + point(e) + QLatin1Char(' ') + point(path.elementAt(index + 1)) + QLatin1Char(' ')
                             + point(path.elementAt(index + 2));
                index += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            break;
        }
        // Qt closes a subpath with a line back to its start.
        const bool last = index + 1 >= path.elementCount() || path.elementAt(index + 1).type == QPainterPath::MoveToElement;
        if (last && e.type != QPainterPath::MoveToElement && QLineF(path.elementAt(index), start).length() < 1e-6)
            parts << QStringLiteral("Z");
    }
    return parts.join(QLatin1Char(' '));
}

QString matrix(const QTransform &t)
{
    return QStringLiteral("matrix(%1 %2 %3 %4 %5 %6)")
        .arg(number(t.m11()), number(t.m12()), number(t.m21()), number(t.m22()), number(t.dx()), number(t.dy()));
}

QString blendName(LayerBlendMode mode)
{
    switch (mode) {
    case LayerBlendMode::multiply: return QStringLiteral("multiply");
    case LayerBlendMode::screen: return QStringLiteral("screen");
    case LayerBlendMode::overlay: return QStringLiteral("overlay");
    case LayerBlendMode::softLight: return QStringLiteral("soft-light");
    case LayerBlendMode::darken: return QStringLiteral("darken");
    case LayerBlendMode::lighten: return QStringLiteral("lighten");
    case LayerBlendMode::difference: return QStringLiteral("difference");
    case LayerBlendMode::colorDodge: return QStringLiteral("color-dodge");
    case LayerBlendMode::colorBurn: return QStringLiteral("color-burn");
    case LayerBlendMode::hue: return QStringLiteral("hue");
    case LayerBlendMode::saturation: return QStringLiteral("saturation");
    case LayerBlendMode::color: return QStringLiteral("color");
    case LayerBlendMode::luminosity: return QStringLiteral("luminosity");
    default: return {};
    }
}

class Writer {
public:
    Writer(const VectorDocument &document, const SvgExporter::Options &options, QIODevice *device)
        : document(document), options(options), xml(device)
    {
    }

    void write()
    {
        xml.setAutoFormatting(true);
        xml.setAutoFormattingIndent(1);
        xml.writeStartDocument();
        xml.writeStartElement(QStringLiteral("svg"));
        xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
        xml.writeNamespace(QStringLiteral("http://www.w3.org/1999/xlink"), QStringLiteral("xlink"));
        xml.writeNamespace(QStringLiteral("http://www.inkscape.org/namespaces/inkscape"), QStringLiteral("inkscape"));
        xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
        const QString width = number(document.size.width()), height = number(document.size.height());
        xml.writeAttribute(QStringLiteral("width"), width);
        xml.writeAttribute(QStringLiteral("height"), height);
        xml.writeAttribute(QStringLiteral("viewBox"), QStringLiteral("0 0 %1 %2").arg(width, height));
        if (options.includeBackground && document.background.alpha() > 0) {
            xml.writeEmptyElement(QStringLiteral("rect"));
            xml.writeAttribute(QStringLiteral("width"), width);
            xml.writeAttribute(QStringLiteral("height"), height);
            writeColor(QStringLiteral("fill"), document.background);
        }
        for (const QUuid &layer : document.layers())
            writeObject(layer);
        xml.writeEndElement();
        xml.writeEndDocument();
    }

private:
    const VectorDocument &document;
    const SvgExporter::Options &options;
    QXmlStreamWriter xml;
    QSet<QString> ids;
    int nextDefinition = 1;

    // XML ids from object names: letters, digits, - _ . and unique.
    QString uniqueID(const QString &name)
    {
        QString base;
        for (const QChar c : name.trimmed())
            base += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('.')) ? c : QLatin1Char('_');
        if (base.isEmpty() || !base.front().isLetter())
            base.prepend(QLatin1Char('_'));
        QString id = base;
        for (int n = 2; ids.contains(id); ++n)
            id = QStringLiteral("%1_%2").arg(base).arg(n);
        ids.insert(id);
        return id;
    }

    QString definitionID(const char *prefix)
    {
        QString id;
        do
            id = QStringLiteral("%1%2").arg(QLatin1String(prefix)).arg(nextDefinition++);
        while (ids.contains(id));
        ids.insert(id);
        return id;
    }

    // fill and fill-opacity; stop-color pairs with stop-opacity.
    void writeColor(const QString &attribute, const QColor &color)
    {
        xml.writeAttribute(attribute, color.name(QColor::HexRgb));
        if (color.alpha() < 255) {
            const QString opacity = attribute == QLatin1String("stop-color") ? QStringLiteral("stop-opacity") : attribute + QStringLiteral("-opacity");
            xml.writeAttribute(opacity, number(color.alphaF()));
        }
    }

    // A gradient's definition, returned as url(#id); bounds are the shape's
    // in the coordinates the element draws in.
    QString writeGradient(const Paint &paint, const QRectF &bounds)
    {
        const QString id = definitionID("gradient");
        xml.writeStartElement(QStringLiteral("defs"));
        if (paint.kind == PaintKind::linearGradient) {
            xml.writeStartElement(QStringLiteral("linearGradient"));
            xml.writeAttribute(QStringLiteral("id"), id);
            xml.writeAttribute(QStringLiteral("gradientUnits"), QStringLiteral("objectBoundingBox"));
            xml.writeAttribute(QStringLiteral("x1"), number(paint.start.x()));
            xml.writeAttribute(QStringLiteral("y1"), number(paint.start.y()));
            xml.writeAttribute(QStringLiteral("x2"), number(paint.end.x()));
            xml.writeAttribute(QStringLiteral("y2"), number(paint.end.y()));
        } else {
            // The radius is a length, not a fraction per axis: place it in user space.
            auto place = [&](QPointF f) { return QPointF(bounds.left() + f.x() * bounds.width(), bounds.top() + f.y() * bounds.height()); };
            const QPointF center = place(paint.start);
            xml.writeStartElement(QStringLiteral("radialGradient"));
            xml.writeAttribute(QStringLiteral("id"), id);
            xml.writeAttribute(QStringLiteral("gradientUnits"), QStringLiteral("userSpaceOnUse"));
            xml.writeAttribute(QStringLiteral("cx"), number(center.x()));
            xml.writeAttribute(QStringLiteral("cy"), number(center.y()));
            xml.writeAttribute(QStringLiteral("r"), number(QLineF(center, place(paint.end)).length()));
        }
        for (const GradientStop &stop : paint.stops) {
            xml.writeEmptyElement(QStringLiteral("stop"));
            xml.writeAttribute(QStringLiteral("offset"), number(std::clamp(stop.offset, 0.0, 1.0)));
            writeColor(QStringLiteral("stop-color"), stop.color);
        }
        xml.writeEndElement();
        xml.writeEndElement();
        return QStringLiteral("url(#%1)").arg(id);
    }

    // Gradient definitions go out before the element that uses them.
    struct Paints {
        QString fill, stroke;
    };
    Paints writeDefinitions(const VectorObject &object, const QRectF &bounds)
    {
        Paints paints;
        if (object.fill.isVisible() && object.fill.kind != PaintKind::solid)
            paints.fill = writeGradient(object.fill, bounds);
        if (object.stroke.isVisible() && object.stroke.paint.kind != PaintKind::solid)
            paints.stroke = writeGradient(object.stroke.paint, bounds);
        return paints;
    }

    void writePaint(const VectorObject &object, const Paints &paints)
    {
        if (!object.fill.isVisible())
            xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
        else if (object.fill.kind == PaintKind::solid)
            writeColor(QStringLiteral("fill"), object.fill.color);
        else
            xml.writeAttribute(QStringLiteral("fill"), paints.fill);
        const StrokeStyle &stroke = object.stroke;
        if (!stroke.isVisible())
            return;
        if (stroke.paint.kind == PaintKind::solid)
            writeColor(QStringLiteral("stroke"), stroke.paint.color);
        else
            xml.writeAttribute(QStringLiteral("stroke"), paints.stroke);
        xml.writeAttribute(QStringLiteral("stroke-width"), number(stroke.width));
        if (stroke.cap != Qt::FlatCap)
            xml.writeAttribute(QStringLiteral("stroke-linecap"), rawValue(stroke.cap));
        if (stroke.join != Qt::MiterJoin)
            xml.writeAttribute(QStringLiteral("stroke-linejoin"), rawValue(stroke.join));
        if (stroke.join == Qt::MiterJoin && stroke.miterLimit != 4)
            xml.writeAttribute(QStringLiteral("stroke-miterlimit"), number(stroke.miterLimit));
        if (!stroke.dashes.empty()) {
            QStringList dashes;
            for (double dash : stroke.dashes)
                dashes << number(dash);
            xml.writeAttribute(QStringLiteral("stroke-dasharray"), dashes.join(QLatin1Char(' ')));
        }
    }

    void writeCommon(const VectorObject &object)
    {
        if (!object.name.isEmpty()) {
            xml.writeAttribute(QStringLiteral("id"), uniqueID(object.name));
            xml.writeAttribute(QStringLiteral("data-name"), object.name);
        }
        if (object.opacity < 1)
            xml.writeAttribute(QStringLiteral("opacity"), number(object.opacity));
        const QString blend = blendName(object.blendMode);
        if (!blend.isEmpty())
            xml.writeAttribute(QStringLiteral("style"), QStringLiteral("mix-blend-mode:%1").arg(blend));
    }

    void writeObject(const QUuid &id)
    {
        const VectorObject *object = document.find(id);
        if (!object || !object->isVisible)
            return;
        switch (object->kind) {
        case ObjectKind::layer:
        case ObjectKind::group:
            writeContainer(*object);
            break;
        case ObjectKind::path:
            writePath(*object);
            break;
        case ObjectKind::text:
            writeText(*object);
            break;
        case ObjectKind::image:
            writeImage(*object);
            break;
        }
    }

    void writeContainer(const VectorObject &object)
    {
        const std::vector<QUuid> children = document.children(object.id);
        size_t first = 0;
        QString clip;
        if (object.isClipGroup && !children.empty()) {
            // The bottom child is the clip; it draws nothing itself.
            const VectorObject *mask = document.find(children.front());
            clip = definitionID("clip");
            xml.writeStartElement(QStringLiteral("defs"));
            xml.writeStartElement(QStringLiteral("clipPath"));
            xml.writeAttribute(QStringLiteral("id"), clip);
            xml.writeEmptyElement(QStringLiteral("path"));
            const bool isPath = mask && mask->kind == ObjectKind::path;
            const QPainterPath outline = document.outline(children.front());
            xml.writeAttribute(QStringLiteral("d"), isPath ? pathData(mask->path) : pathData(outline));
            if ((isPath ? mask->path.fillRule : outline.fillRule()) == Qt::OddEvenFill)
                xml.writeAttribute(QStringLiteral("clip-rule"), QStringLiteral("evenodd"));
            xml.writeEndElement();
            xml.writeEndElement();
            first = 1;
        }
        xml.writeStartElement(QStringLiteral("g"));
        writeCommon(object);
        if (object.kind == ObjectKind::layer) {
            xml.writeAttribute(QStringLiteral("inkscape:groupmode"), QStringLiteral("layer"));
            xml.writeAttribute(QStringLiteral("inkscape:label"), object.name);
        }
        if (!clip.isEmpty())
            xml.writeAttribute(QStringLiteral("clip-path"), QStringLiteral("url(#%1)").arg(clip));
        for (size_t index = first; index < children.size(); ++index)
            writeObject(children[index]);
        xml.writeEndElement();
    }

    void writePath(const VectorObject &object)
    {
        if (object.path.isEmpty())
            return;
        const Paints paints = writeDefinitions(object, object.path.bounds());
        xml.writeEmptyElement(QStringLiteral("path"));
        writeCommon(object);
        xml.writeAttribute(QStringLiteral("d"), pathData(object.path));
        if (object.path.fillRule == Qt::OddEvenFill)
            xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("evenodd"));
        writePaint(object, paints);
    }

    void writeText(const VectorObject &object)
    {
        const TextContent &text = object.text;
        if (text.text.isEmpty())
            return;
        const QPainterPath glyphs = text.outline();
        // Paint follows the glyphs in the text's own coordinates, as on the canvas.
        const Paints paints = writeDefinitions(object, glyphs.boundingRect());
        if (options.textAsOutlines) {
            xml.writeEmptyElement(QStringLiteral("path"));
            writeCommon(object);
            xml.writeAttribute(QStringLiteral("transform"), matrix(object.transform));
            xml.writeAttribute(QStringLiteral("d"), pathData(glyphs));
            if (glyphs.fillRule() == Qt::OddEvenFill)
                xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("evenodd"));
            writePaint(object, paints);
            return;
        }
        xml.writeStartElement(QStringLiteral("text"));
        writeCommon(object);
        xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
        // Horizontal and vertical scale stretch the glyphs; lines keep their places.
        const double h = text.horizontalScale / 100, v = text.verticalScale / 100;
        xml.writeAttribute(QStringLiteral("transform"), matrix(QTransform::fromScale(h, v) * object.transform));
        xml.writeAttribute(QStringLiteral("font-family"), text.family);
        xml.writeAttribute(QStringLiteral("font-size"), number(text.size));
        const int weight = QFontDatabase::styles(text.family).contains(text.style) ? QFontDatabase::weight(text.family, text.style)
                                                                                  : (text.isBold() ? 700 : 400);
        if (weight != 400)
            xml.writeAttribute(QStringLiteral("font-weight"), QString::number(weight));
        if (text.isItalic())
            xml.writeAttribute(QStringLiteral("font-style"), QStringLiteral("italic"));
        if (text.tracking != 0)
            xml.writeAttribute(QStringLiteral("letter-spacing"), number(text.tracking / 1000) + QStringLiteral("em"));
        if (text.kerning == TextKerning::none)
            xml.writeAttribute(QStringLiteral("font-kerning"), QStringLiteral("none"));
        if (text.textCase == TextCase::smallCaps)
            xml.writeAttribute(QStringLiteral("font-variant"), QStringLiteral("small-caps"));
        else if (text.textCase == TextCase::allCaps)
            xml.writeAttribute(QStringLiteral("style"), QStringLiteral("text-transform:uppercase"));
        QStringList decorations;
        if (text.underline)
            decorations << QStringLiteral("underline");
        if (text.strikethrough)
            decorations << QStringLiteral("line-through");
        if (!decorations.isEmpty())
            xml.writeAttribute(QStringLiteral("text-decoration"), decorations.join(QLatin1Char(' ')));
        const bool area = text.area.has_value();
        if (!area && text.alignment == TextAlignment::center)
            xml.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
        else if (!area && text.alignment == TextAlignment::right)
            xml.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("end"));
        writePaint(object, paints);
        // One span per laid-out line, so area type wraps as it does on the canvas.
        const TextLayout layout(text);
        for (const TextLayout::Line &line : layout.lines()) {
            if (line.hidden)
                break;
            QString words = text.text.mid(line.start, line.length);
            // A wrapped line's trailing space would push centred and right-aligned lines.
            while (area && !line.lastInParagraph && words.endsWith(QLatin1Char(' ')))
                words.chop(1);
            xml.writeStartElement(QStringLiteral("tspan"));
            double x = 0;
            const bool justified = area && (text.alignment == TextAlignment::justifyAll || (text.alignment == TextAlignment::justify && !line.lastInParagraph));
            if (area) {
                x = text.alignment == TextAlignment::center ? (line.left + line.right) / 2 : text.alignment == TextAlignment::right ? line.right : line.left;
                if (text.alignment == TextAlignment::center || text.alignment == TextAlignment::right)
                    xml.writeAttribute(QStringLiteral("text-anchor"), text.alignment == TextAlignment::center ? QStringLiteral("middle") : QStringLiteral("end"));
            }
            xml.writeAttribute(QStringLiteral("x"), number(x / h));
            xml.writeAttribute(QStringLiteral("y"), number(line.baseline / v));
            if (justified && line.right > line.left) {
                xml.writeAttribute(QStringLiteral("textLength"), number((line.right - line.left) / h));
                xml.writeAttribute(QStringLiteral("lengthAdjust"), QStringLiteral("spacing"));
            }
            if (text.baselineShift != 0)
                xml.writeAttribute(QStringLiteral("baseline-shift"), number(text.baselineShift / v));
            QStringList shifts;
            bool kerned = false;
            for (int index = 0; index < words.size(); ++index) {
                const auto kern = text.kerns.find(line.start + index);
                const double shift = kern == text.kerns.end() || index == 0 ? 0 : kern->second / 1000 * text.size / h;
                kerned = kerned || shift != 0;
                shifts << number(shift);
            }
            if (kerned)
                xml.writeAttribute(QStringLiteral("dx"), shifts.join(QLatin1Char(' ')));
            xml.writeCharacters(words);
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    void writeImage(const VectorObject &object)
    {
        if (object.image.isNull())
            return;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        object.image.save(&buffer, "PNG");
        xml.writeEmptyElement(QStringLiteral("image"));
        writeCommon(object);
        xml.writeAttribute(QStringLiteral("width"), QString::number(object.image.width()));
        xml.writeAttribute(QStringLiteral("height"), QString::number(object.image.height()));
        xml.writeAttribute(QStringLiteral("preserveAspectRatio"), QStringLiteral("none"));
        xml.writeAttribute(QStringLiteral("transform"), matrix(object.transform));
        xml.writeAttribute(QStringLiteral("xlink:href"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
    }
};
}

namespace SvgExporter {
QByteArray serialize(const VectorDocument &document, const Options &options)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    Writer(document, options, &buffer).write();
    return bytes;
}

void write(const VectorDocument &document, const QString &path, const Options &options)
{
    const QByteArray bytes = serialize(document, options);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcIO).noquote() << "cannot write" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("The SVG could not be saved: %1").arg(file.errorString()));
    }
    qCInfo(lcIO).noquote() << "wrote SVG" << bytes.size() << "bytes to" << path;
}
}
