#include "IO/SvgExporter.h"
#include "Document/StrokeGeometry.h"
#include "IO/DocumentExporter.h"
#include "Logging.h"
#include "Document/FontFeatures.h"
#include "Document/TextLayout.h"
#include <QFontDatabase>
#include <QBuffer>
#include <QLineF>
#include <QMargins>
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

    // A stack past one fill and one plain stroke: a group named for the object,
    // holding one element per visible entry, bottom to top. Aligned, dashed-to-corner
    // and arrowed strokes go out as their filled outline, which SVG draws the same.
    void writeStack(const VectorObject &object)
    {
        xml.writeStartElement(QStringLiteral("g"));
        writeCommon(object);
        VectorObject base = object;
        base.name.clear();
        base.opacity = 1;
        base.blendMode = LayerBlendMode::normal;
        base.extraFills.clear();
        base.extraStrokes.clear();
        const auto entry = [&](const Paint &paint) {
            VectorObject one = base;
            one.opacity = paint.opacity;
            one.blendMode = paint.blendMode;
            one.fill = Paint::none();
            one.stroke.paint = Paint::none();
            return one;
        };
        const bool fillable = object.kind == ObjectKind::text
            || std::any_of(object.path.contours.begin(), object.path.contours.end(), [](const Contour &c) { return c.closed || c.nodes.size() > 2; });
        for (const Paint &fill : object.fills()) {
            if (!fill.isVisible() || !fillable)
                continue;
            VectorObject one = entry(fill);
            one.fill = fill.withCompositeOf(Paint());
            writeObjectBody(one);
        }
        for (const StrokeStyle &stroke : object.strokes()) {
            if (!stroke.isVisible())
                continue;
            VectorObject one = entry(stroke.paint);
            if (stroke.isPlain()) {
                one.stroke = stroke;
                one.stroke.paint = stroke.paint.withCompositeOf(Paint());
                writeObjectBody(one);
                continue;
            }
            // The outline in document coordinates; the gradient spans the shape it strokes.
            QPainterPath shape = object.kind == ObjectKind::text ? object.transform.map(object.text.outline()) : object.path.painterPath();
            one.kind = ObjectKind::path;
            one.transform = {};
            const QPainterPath covered = StrokeGeometry::area(shape, stroke);
            one.path = VectorPath::fromPainterPath(covered);
            one.path.fillRule = covered.fillRule();
            one.fill = stroke.paint.withCompositeOf(Paint());
            writeObjectBody(one);
        }
        xml.writeEndElement();
    }

    void writeObjectBody(const VectorObject &object)
    {
        if (object.kind == ObjectKind::text)
            writeText(object);
        else
            writePath(object);
    }

    void writeObject(const QUuid &id)
    {
        const VectorObject *object = document.find(id);
        if (!object || !object->isVisible)
            return;
        if (object->kind == ObjectKind::frame) {
            writeFrame(*object);
            return;
        }
        if (object->hasPaint() && !object->hasSimpleAppearance()) {
            writeStack(*object);
            return;
        }
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
        case ObjectKind::frame:
            break;
        }
    }

    // Its box as a path: the fills alone or the strokes alone, however many.
    void writeFrameBox(const VectorObject &frame, bool fills)
    {
        VectorObject box = frame;
        box.kind = ObjectKind::path;
        box.id = QUuid::createUuid();
        box.name.clear();
        box.opacity = 1;
        box.blendMode = LayerBlendMode::normal;
        box.component.reset();
        box.instance.reset();
        if (fills)
            box.setStrokes({});
        else
            box.setFills({});
        if (!box.hasVisibleFill() && !box.hasVisibleStroke())
            return;
        if (box.hasSimpleAppearance())
            writePath(box);
        else
            writeStack(box);
    }

    // A frame: a group holding its fills, its children (clipped to the box unless clipping is off) and its strokes.
    void writeFrame(const VectorObject &frame)
    {
        xml.writeStartElement(QStringLiteral("g"));
        writeCommon(frame);
        writeFrameBox(frame, true);
        const std::vector<QUuid> children = document.children(frame.id);
        const bool pictured = frame.showsBrowserPicture() && !frame.browser->picture.isNull();
        if (!children.empty() || pictured) {
            QString clip;
            if (frame.clipsContent) {
                clip = definitionID("clip");
                xml.writeStartElement(QStringLiteral("defs"));
                xml.writeStartElement(QStringLiteral("clipPath"));
                xml.writeAttribute(QStringLiteral("id"), clip);
                xml.writeEmptyElement(QStringLiteral("path"));
                xml.writeAttribute(QStringLiteral("d"), pathData(frame.path));
                xml.writeEndElement();
                xml.writeEndElement();
            }
            xml.writeStartElement(QStringLiteral("g"));
            if (!clip.isEmpty())
                xml.writeAttribute(QStringLiteral("clip-path"), QStringLiteral("url(#%1)").arg(clip));
            if (pictured) {
                // A Browser View's last picture, stretched to the box under the children.
                VectorObject picture;
                picture.kind = ObjectKind::image;
                picture.image = frame.browser->picture;
                const QRectF box = frame.shape->rect.normalized();
                picture.transform = QTransform::fromTranslate(box.left(), box.top())
                    * QTransform::fromScale(box.width() / picture.image.width(), box.height() / picture.image.height());
                writeImage(picture);
            }
            for (const QUuid &child : children)
                writeObject(child);
            xml.writeEndElement();
        }
        writeFrameBox(frame, false);
        xml.writeEndElement();
    }

    void writeContainer(const VectorObject &object)
    {
        const std::vector<QUuid> children = document.children(object.id);
        size_t first = 0, last = children.size();
        QString clip, mask;
        if (object.isClipGroup && !children.empty()) {
            // The bottom child is the clip; it draws nothing itself.
            const VectorObject *clipObject = document.find(children.front());
            clip = definitionID("clip");
            xml.writeStartElement(QStringLiteral("defs"));
            xml.writeStartElement(QStringLiteral("clipPath"));
            xml.writeAttribute(QStringLiteral("id"), clip);
            xml.writeEmptyElement(QStringLiteral("path"));
            const bool isPath = clipObject && clipObject->kind == ObjectKind::path;
            const QPainterPath outline = document.outline(children.front());
            xml.writeAttribute(QStringLiteral("d"), isPath ? pathData(clipObject->path) : pathData(outline));
            if ((isPath ? clipObject->path.fillRule : outline.fillRule()) == Qt::OddEvenFill)
                xml.writeAttribute(QStringLiteral("clip-rule"), QStringLiteral("evenodd"));
            xml.writeEndElement();
            xml.writeEndElement();
            first = 1;
        } else if (object.mask && children.size() >= 2) {
            // P2-9: the top child's luminance masks everything under it.
            mask = writeOpacityMask(object, children.back());
            last = children.size() - 1;
        }
        xml.writeStartElement(QStringLiteral("g"));
        writeCommon(object);
        if (object.kind == ObjectKind::layer) {
            xml.writeAttribute(QStringLiteral("inkscape:groupmode"), QStringLiteral("layer"));
            xml.writeAttribute(QStringLiteral("inkscape:label"), object.name);
        }
        if (!clip.isEmpty())
            xml.writeAttribute(QStringLiteral("clip-path"), QStringLiteral("url(#%1)").arg(clip));
        if (!mask.isEmpty())
            xml.writeAttribute(QStringLiteral("mask"), QStringLiteral("url(#%1)").arg(mask));
        for (size_t index = first; index < last; ++index)
            writeObject(children[index]);
        xml.writeEndElement();
    }

    // A <mask> from `maskId`'s luminance (SVG's own rule); Invert runs it through a
    // colour-matrix filter, and Clip off backs it with white so what it never
    // covers stays visible instead of hidden.
    QString writeOpacityMask(const VectorObject &object, const QUuid &maskId)
    {
        const QString id = definitionID("mask");
        xml.writeStartElement(QStringLiteral("defs"));
        QString filter;
        if (object.mask->inverted) {
            filter = definitionID("invert");
            xml.writeStartElement(QStringLiteral("filter"));
            xml.writeAttribute(QStringLiteral("id"), filter);
            xml.writeEmptyElement(QStringLiteral("feColorMatrix"));
            xml.writeAttribute(QStringLiteral("type"), QStringLiteral("matrix"));
            xml.writeAttribute(QStringLiteral("values"), QStringLiteral("-1 0 0 0 1  0 -1 0 0 1  0 0 -1 0 1  0 0 0 1 0"));
            xml.writeEndElement();
        }
        // The region itself, in document coordinates with a margin: unset, its default
        // percentages would resolve against the whole page instead of this group.
        const QRectF bounds = document.bounds(object.id, true).marginsAdded(QMarginsF(20, 20, 20, 20));
        xml.writeStartElement(QStringLiteral("mask"));
        xml.writeAttribute(QStringLiteral("id"), id);
        xml.writeAttribute(QStringLiteral("maskUnits"), QStringLiteral("userSpaceOnUse"));
        xml.writeAttribute(QStringLiteral("x"), number(bounds.left()));
        xml.writeAttribute(QStringLiteral("y"), number(bounds.top()));
        xml.writeAttribute(QStringLiteral("width"), number(bounds.width()));
        xml.writeAttribute(QStringLiteral("height"), number(bounds.height()));
        if (!object.mask->clip) {
            xml.writeEmptyElement(QStringLiteral("rect"));
            xml.writeAttribute(QStringLiteral("x"), number(bounds.left()));
            xml.writeAttribute(QStringLiteral("y"), number(bounds.top()));
            xml.writeAttribute(QStringLiteral("width"), number(bounds.width()));
            xml.writeAttribute(QStringLiteral("height"), number(bounds.height()));
            xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("white"));
        }
        if (filter.isEmpty()) {
            writeObject(maskId);
        } else {
            xml.writeStartElement(QStringLiteral("g"));
            xml.writeAttribute(QStringLiteral("filter"), QStringLiteral("url(#%1)").arg(filter));
            writeObject(maskId);
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
        return id;
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

    static int weightOf(const CharacterFormat &format)
    {
        return QFontDatabase::styles(format.family).contains(format.style) ? QFontDatabase::weight(format.family, format.style)
                                                                           : (format.isBold() ? 700 : 400);
    }

    // Font attributes: the text element's against CSS's defaults, a run's where it differs from its text.
    void writeFont(const CharacterFormat &format, const CharacterFormat *against)
    {
        const auto differs = [&](auto member) { return against && !(format.*member == against->*member); };
        if (!against || differs(&CharacterFormat::family))
            xml.writeAttribute(QStringLiteral("font-family"), format.family);
        if (!against || differs(&CharacterFormat::size))
            xml.writeAttribute(QStringLiteral("font-size"), number(format.size));
        const int weight = weightOf(format);
        if (against ? weight != weightOf(*against) : weight != 400)
            xml.writeAttribute(QStringLiteral("font-weight"), QString::number(weight));
        if (against ? format.isItalic() != against->isItalic() : format.isItalic())
            xml.writeAttribute(QStringLiteral("font-style"), format.isItalic() ? QStringLiteral("italic") : QStringLiteral("normal"));
        if (against ? differs(&CharacterFormat::tracking) : format.tracking != 0)
            xml.writeAttribute(QStringLiteral("letter-spacing"), number(format.tracking / 1000) + QStringLiteral("em"));
        QStringList css;
        if (!against || differs(&CharacterFormat::textCase)) {
            if (format.textCase == TextCase::smallCaps)
                xml.writeAttribute(QStringLiteral("font-variant"), QStringLiteral("small-caps"));
            else if (against && against->textCase == TextCase::smallCaps)
                xml.writeAttribute(QStringLiteral("font-variant"), QStringLiteral("normal"));
            if (format.textCase == TextCase::allCaps)
                css << QStringLiteral("text-transform:uppercase");
            else if (against && against->textCase == TextCase::allCaps)
                css << QStringLiteral("text-transform:none");
        }
        if (!against || differs(&CharacterFormat::underline) || differs(&CharacterFormat::strikethrough)) {
            QStringList decorations;
            if (format.underline)
                decorations << QStringLiteral("underline");
            if (format.strikethrough)
                decorations << QStringLiteral("line-through");
            if (!decorations.isEmpty())
                xml.writeAttribute(QStringLiteral("text-decoration"), decorations.join(QLatin1Char(' ')));
            else if (against)
                xml.writeAttribute(QStringLiteral("text-decoration"), QStringLiteral("none"));
        }
        if (against ? differs(&CharacterFormat::features) : !format.features.empty())
            css << QStringLiteral("font-feature-settings:") + (format.features.empty() ? QStringLiteral("normal") : FontFeatures::css(format.features));
        if (against && format.fill && format.fill != against->fill)
            writeColor(QStringLiteral("fill"), *format.fill);
        if (!css.isEmpty())
            xml.writeAttribute(QStringLiteral("style"), css.join(QLatin1Char(';')));
    }

    // `words` with U+00AD stripped, a hyphen appended if `hyphenated`, and trailing spaces
    // chopped when `trimTrailing`; `originalIndex` keeps each kept character's absolute
    // index into `source.text` (-1 for the hyphen, which has none).
    static void buildRun(const TextContent &source, int start, int length, bool hyphenated, bool trimTrailing, QString &words,
                         std::vector<int> &originalIndex)
    {
        for (int index = 0; index < length; ++index) {
            const int absolute = start + index;
            const QChar ch = source.text.at(absolute);
            if (ch == QChar(0x00AD) || ch == QLatin1Char('\n'))
                continue;
            words += ch;
            originalIndex.push_back(absolute);
        }
        if (hyphenated) {
            words += QLatin1Char('-');
            originalIndex.push_back(-1);
        }
        while (trimTrailing && !words.isEmpty() && words.back() == QLatin1Char(' ')) {
            words.chop(1);
            originalIndex.pop_back();
        }
    }

    // The kern before each kept character, and every run's tspan, against `source`'s own format.
    void writeRuns(const TextContent &source, const QString &words, const std::vector<int> &originalIndex, double h, double v)
    {
        QStringList shifts;
        bool kerned = false;
        for (int k = 0; k < words.size(); ++k) {
            double shift = 0;
            if (k > 0 && originalIndex[size_t(k)] >= 0) {
                const auto kern = source.kerns.find(originalIndex[size_t(k)]);
                if (kern != source.kerns.end())
                    shift = kern->second / 1000 * source.formatAt(originalIndex[size_t(k)] - 1).size / h;
            }
            kerned = kerned || shift != 0;
            shifts << number(shift);
        }
        if (kerned)
            xml.writeAttribute(QStringLiteral("dx"), shifts.join(QLatin1Char(' ')));
        const bool indented = xml.autoFormatting();
        const auto formatOf = [&](int k, const CharacterFormat &fallback) {
            return originalIndex[size_t(k)] >= 0 ? source.formatAt(originalIndex[size_t(k)]) : fallback;
        };
        for (int from = 0; from < words.size();) {
            xml.setAutoFormatting(false);
            const CharacterFormat format = formatOf(from, source.character());
            int to = from + 1;
            while (to < words.size() && formatOf(to, format) == format)
                ++to;
            if (format == source.character()) {
                xml.writeCharacters(words.mid(from, to - from));
            } else {
                xml.writeStartElement(QStringLiteral("tspan"));
                writeFont(format, &source.character());
                if (format.baselineShift != source.baselineShift)
                    xml.writeAttribute(QStringLiteral("baseline-shift"), number((format.baselineShift - source.baselineShift) / v));
                xml.writeCharacters(words.mid(from, to - from));
                xml.writeEndElement();
            }
            from = to;
        }
        xml.setAutoFormatting(indented);
    }

    void writePathText(const VectorObject &object, const Paints &paints, const TextContent &source, const TextLayout &layout)
    {
        const TextPath &onPath = *object.text.onPath;
        const VectorPath directed = onPath.flipped ? reversed(onPath.path) : onPath.path;
        xml.writeStartElement(QStringLiteral("defs"));
        xml.writeEmptyElement(QStringLiteral("path"));
        const QString id = definitionID("textPath");
        xml.writeAttribute(QStringLiteral("id"), id);
        xml.writeAttribute(QStringLiteral("d"), pathData(directed));
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("text"));
        writeCommon(object);
        xml.writeAttribute(QStringLiteral("transform"), matrix(object.transform));
        writeFont(source.character(), nullptr);
        writePaint(object, paints);
        xml.writeStartElement(QStringLiteral("textPath"));
        xml.writeAttribute(QStringLiteral("href"), QStringLiteral("#%1").arg(id));
        // A closed path wraps once on the canvas; plain SVG textPath doesn't run past
        // the end of even a closed one, a known, documented gap in this export.
        const double effectiveStart = onPath.flipped ? 1 - onPath.start : onPath.start;
        xml.writeAttribute(QStringLiteral("startOffset"), number(std::clamp(effectiveStart, 0.0, 1.0) * 100) + QStringLiteral("%"));
        QString words;
        std::vector<int> originalIndex;
        for (const TextLayout::Line &line : layout.lines()) {
            if (line.hidden)
                continue;
            buildRun(source, line.start, line.length, line.hyphenated, false, words, originalIndex);
        }
        writeRuns(source, words, originalIndex, 1, 1);
        xml.writeEndElement();
        xml.writeEndElement();
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
            const auto pieces = text.fills();
            // Runs with colours of their own: a path per colour, in one group.
            if (pieces.size() > 1) {
                xml.writeStartElement(QStringLiteral("g"));
                writeCommon(object);
                xml.writeAttribute(QStringLiteral("transform"), matrix(object.transform));
                writePaint(object, paints);
            }
            for (const auto &[color, piece] : pieces) {
                xml.writeEmptyElement(QStringLiteral("path"));
                if (pieces.size() == 1) {
                    writeCommon(object);
                    xml.writeAttribute(QStringLiteral("transform"), matrix(object.transform));
                }
                xml.writeAttribute(QStringLiteral("d"), pathData(piece));
                if (piece.fillRule() == Qt::OddEvenFill)
                    xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("evenodd"));
                if (pieces.size() == 1)
                    writePaint(object, paints);
                else if (color)
                    writeColor(QStringLiteral("fill"), *color);
            }
            if (pieces.size() > 1)
                xml.writeEndElement();
            return;
        }
        const TextLayout layout(text);
        if (text.onPath) {
            writePathText(object, paints, layout.source(), layout);
            return;
        }
        xml.writeStartElement(QStringLiteral("text"));
        writeCommon(object);
        xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
        // Horizontal and vertical scale stretch the glyphs; lines keep their places.
        const double h = text.horizontalScale / 100, v = text.verticalScale / 100;
        xml.writeAttribute(QStringLiteral("transform"), matrix(QTransform::fromScale(h, v) * object.transform));
        const TextContent &source = layout.source();
        writeFont(source.character(), nullptr);
        if (text.kerning == TextKerning::none)
            xml.writeAttribute(QStringLiteral("font-kerning"), QStringLiteral("none"));
        const bool area = source.area.has_value();
        if (!area && source.alignment == TextAlignment::center)
            xml.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
        else if (!area && source.alignment == TextAlignment::right)
            xml.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("end"));
        writePaint(object, paints);
        const auto anchor = [](TextAlignment alignment) {
            return alignment == TextAlignment::center ? QStringLiteral("middle") : alignment == TextAlignment::right ? QStringLiteral("end") : QStringLiteral("start");
        };
        // One span per laid-out line (or thread frame), so wrap and threads read as on the canvas.
        for (const TextLayout::Line &line : layout.lines()) {
            if (line.hidden)
                continue;
            if (line.frame != text.flow.frame)
                continue;
            const TextAlignment alignment = source.paragraphAt(line.paragraph).alignment;
            QString words;
            std::vector<int> originalIndex;
            // A wrapped line's trailing space would push centred and right-aligned lines.
            buildRun(source, line.start, line.length, line.hyphenated, area && !line.lastInParagraph, words, originalIndex);
            xml.writeStartElement(QStringLiteral("tspan"));
            double x = 0;
            const bool justified = area && (alignment == TextAlignment::justifyAll || (isJustified(alignment) && !line.lastInParagraph));
            if (area) {
                x = alignment == TextAlignment::center ? (line.left + line.right) / 2 : alignment == TextAlignment::right ? line.right : line.left;
                if (alignment == TextAlignment::center || alignment == TextAlignment::right)
                    xml.writeAttribute(QStringLiteral("text-anchor"), anchor(alignment));
            } else if (alignment != source.alignment) {
                // A paragraph aligned apart from the rest.
                xml.writeAttribute(QStringLiteral("text-anchor"), anchor(alignment));
            }
            xml.writeAttribute(QStringLiteral("x"), number(x / h));
            xml.writeAttribute(QStringLiteral("y"), number(line.baseline / v));
            if (justified && line.right > line.left) {
                xml.writeAttribute(QStringLiteral("textLength"), number((line.right - line.left) / h));
                xml.writeAttribute(QStringLiteral("lengthAdjust"), QStringLiteral("spacing"));
            }
            if (source.baselineShift != 0)
                xml.writeAttribute(QStringLiteral("baseline-shift"), number(source.baselineShift / v));
            writeRuns(source, words, originalIndex, h, v);
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
    // Several artboards: this exports the first one that exports, alone, so single-artboard
    // output (the common case) stays byte-identical to before.
    const VectorDocument page = DocumentExporter::exportedPage(document);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    Writer(page, options, &buffer).write();
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
