#include "IO/SvgImporter.h"
#include "IO/SvgImporterParts.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <memory>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

// nanosvg is a single-header C library; its warnings are not ours.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#pragma GCC diagnostic pop

namespace {
// Bigger than any real drawing; nanosvg holds the whole file in memory.
constexpr qint64 maximumBytes = qint64(256) << 20;

LayerBlendMode blendMode(const QString &css)
{
    static const QHash<QString, LayerBlendMode> modes{
        {QStringLiteral("multiply"), LayerBlendMode::multiply},     {QStringLiteral("screen"), LayerBlendMode::screen},
        {QStringLiteral("overlay"), LayerBlendMode::overlay},       {QStringLiteral("soft-light"), LayerBlendMode::softLight},
        {QStringLiteral("darken"), LayerBlendMode::darken},         {QStringLiteral("lighten"), LayerBlendMode::lighten},
        {QStringLiteral("difference"), LayerBlendMode::difference}, {QStringLiteral("color-dodge"), LayerBlendMode::colorDodge},
        {QStringLiteral("color-burn"), LayerBlendMode::colorBurn},  {QStringLiteral("hue"), LayerBlendMode::hue},
        {QStringLiteral("saturation"), LayerBlendMode::saturation}, {QStringLiteral("color"), LayerBlendMode::color},
        {QStringLiteral("luminosity"), LayerBlendMode::luminosity}};
    return modes.value(css.toLower(), LayerBlendMode::normal);
}

const QSet<QString> shapeTags{QStringLiteral("path"),     QStringLiteral("rect"),    QStringLiteral("circle"), QStringLiteral("ellipse"),
                              QStringLiteral("line"),     QStringLiteral("polyline"), QStringLiteral("polygon")};

bool isGzip(const QByteArray &data)
{
    return data.size() >= 2 && quint8(data[0]) == 0x1f && quint8(data[1]) == 0x8b;
}

#ifdef OMASTRATOR_HAVE_ZLIB
// A gzip bomb inflates a megabyte into gigabytes; stop well short of that.
constexpr qsizetype maximumSvgzSize = 64 * 1024 * 1024;

// .svgz is a plain gzip stream; decode it whatever the file's given extension.
QByteArray gunzip(const QByteArray &data)
{
    z_stream stream{};
    // 16 + MAX_WBITS: expect a gzip header, not a raw deflate or zlib stream.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return {};
    QByteArray output;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }
        output.append(chunk.constData(), chunk.size() - int(stream.avail_out));
        if (output.size() > maximumSvgzSize) {
            inflateEnd(&stream);
            throw FileError(QStringLiteral("This .svgz is too large to import."));
        }
        if (result != Z_STREAM_END && stream.avail_in == 0)
            break;
    }
    inflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray();
}
#endif
// Elements whose children draw into their parent's place.
const QSet<QString> passThrough{QStringLiteral("a"), QStringLiteral("switch"), QStringLiteral("svg")};

// Reads the XML beside nanosvg: prepare() marks what nanosvg must report on,
// build() lays nanosvg's shapes and our own text and images into layers.
class Builder {
public:
    Builder(const QString &svg, const QString &folder, const QString &layerName) : source(svg), folder(folder), layerName(layerName) {}

    SvgSource source;
    QStringList warnings;

    void prepare()
    {
        if (source.isValid())
            prepare(source.root());
    }

    VectorDocument build(const NSVGimage &image)
    {
        document.size = QSizeF(image.width, image.height);
        document.objects.clear();
        for (const NSVGshape *shape = image.shapes; shape; shape = shape->next)
            shapes.insert(QString::fromUtf8(shape->id), shape);
        if (!source.isValid()) {
            flat(image);
        } else {
            const QTransform root = rootTransform();
            for (int child : drawnChildren(source.root())) {
                if (source.at(child).tag == QLatin1String("g"))
                    buildLayer(child, root);
                else
                    buildItem(child, looseLayer(), root);
            }
        }
        if (document.layers().empty())
            looseLayer();
        warnings.removeDuplicates();
        return std::move(document);
    }

private:
    struct Clip {
        QStringList keys;
        Qt::FillRule rule = Qt::WindingFill;
    };
    QString folder, layerName;
    // Element → the id nanosvg reports it under.
    QHash<int, QString> keys;
    QHash<int, SvgImport::TextRun> texts;
    QHash<int, Clip> clips;
    QHash<QString, const NSVGshape *> shapes;
    VectorDocument document;
    std::optional<QUuid> loose;
    int layerCount = 0;

    const QString &tag(int element) const { return source.at(element).tag; }

    std::vector<int> drawnChildren(int element) const
    {
        std::vector<int> result;
        for (int child : source.at(element).children) {
            const QString &name = tag(child);
            const bool drawn = name == QLatin1String("g") || name == QLatin1String("text") || name == QLatin1String("image")
                               || name == QLatin1String("use") || name == QLatin1String("foreignObject") || shapeTags.contains(name)
                               || passThrough.contains(name);
            if (!drawn)
                continue;
            // A <switch> draws its first child it can; ours skips foreign content.
            if (tag(element) == QLatin1String("switch")) {
                if (name == QLatin1String("foreignObject"))
                    continue;
                return {child};
            }
            result.push_back(child);
        }
        return result;
    }

    // Where nanosvg reads an attribute added to the element's start tag.
    qsizetype attributeSlot(int element) const
    {
        const SvgElement &e = source.at(element);
        return e.end - (e.selfClosing ? 2 : 1);
    }

    // The element's attributes as nanosvg should see them, less `skip`.
    QString attributes(int element, const QSet<QString> &skip) const
    {
        QString result;
        for (const QXmlStreamAttribute &attribute : source.at(element).attributes) {
            const QString name = attribute.qualifiedName().toString();
            if (!skip.contains(name))
                result += SvgSyntax::attribute(name, attribute.value().toString());
        }
        return result;
    }

    void prepare(int element)
    {
        const QString &name = tag(element);
        if (element != source.root())
            prepareClip(element);
        if (name == QLatin1String("g") || passThrough.contains(name)) {
            for (int child : drawnChildren(element))
                prepare(child);
        } else if (shapeTags.contains(name)) {
            const QString key = source.uniqueID();
            keys.insert(element, key);
            source.insert(attributeSlot(element), SvgSyntax::attribute(QStringLiteral("id"), key));
        } else if (name == QLatin1String("text")) {
            prepareText(element);
        }
    }

    // nanosvg paints a rectangle over the glyphs' bounds as the text would be
    // painted, gradients and all; it stands in for the text's paint.
    void prepareText(int element)
    {
        const std::optional<SvgImport::TextRun> run = SvgImport::readText(source, element);
        if (!run)
            return;
        texts.insert(element, *run);
        static const QSet<QString> positional{QStringLiteral("id"),         QStringLiteral("x"),      QStringLiteral("y"),
                                              QStringLiteral("dx"),         QStringLiteral("dy"),     QStringLiteral("rotate"),
                                              QStringLiteral("textLength"), QStringLiteral("clip-path"), QStringLiteral("mask"),
                                              QStringLiteral("filter")};
        std::vector<int> chain;
        for (int at = run->style; at >= 0 && at != element; at = source.at(at).parent)
            chain.insert(chain.begin(), at);
        QString probe = QStringLiteral("<g") + attributes(element, positional) + QLatin1Char('>');
        QSet<QString> spanSkip = positional;
        spanSkip.insert(QStringLiteral("transform"));
        for (int span : chain)
            probe += QStringLiteral("<g") + attributes(span, spanSkip) + QLatin1Char('>');
        QRectF bounds = run->content.outline().boundingRect();
        if (!(bounds.width() > 0 && bounds.height() > 0))
            bounds = QRectF(0, -run->content.size, run->content.size, run->content.size);
        bounds.translate(run->origin);
        const QString key = source.uniqueID();
        const auto number = [](double value) { return QString::number(value, 'g', 12); };
        probe += QStringLiteral("<rect") + SvgSyntax::attribute(QStringLiteral("id"), key) + SvgSyntax::attribute(QStringLiteral("x"), number(bounds.x()))
                 + SvgSyntax::attribute(QStringLiteral("y"), number(bounds.y())) + SvgSyntax::attribute(QStringLiteral("width"), number(bounds.width()))
                 + SvgSyntax::attribute(QStringLiteral("height"), number(bounds.height())) + QStringLiteral("/>");
        probe += QStringLiteral("</g>").repeated(qsizetype(chain.size()) + 1);
        keys.insert(element, key);
        source.insert(source.at(element).begin, probe);
    }

    // The clipPath's shapes, copied where nanosvg draws them in the clipped
    // element's own coordinates.
    void prepareClip(int element)
    {
        const QString reference = source.property(element, QStringLiteral("clip-path"));
        if (reference.isEmpty() || reference == QLatin1String("none"))
            return;
        const int path = source.reference(reference);
        if (path < 0 || tag(path) != QLatin1String("clipPath")
            || source.attribute(path, QStringLiteral("clipPathUnits")) == QLatin1String("objectBoundingBox")) {
            warnings << QStringLiteral("Clipping paths that couldn’t be read were left out.");
            return;
        }
        static const QSet<QString> skip{QStringLiteral("id"),      QStringLiteral("style"),      QStringLiteral("class"),
                                        QStringLiteral("clip-path"), QStringLiteral("mask"),     QStringLiteral("filter"),
                                        QStringLiteral("display"), QStringLiteral("visibility"), QStringLiteral("opacity"),
                                        QStringLiteral("fill"),    QStringLiteral("fill-rule"),  QStringLiteral("clip-rule"),
                                        QStringLiteral("stroke")};
        Clip clip;
        QString copy = QStringLiteral("<g") + SvgSyntax::attribute(QStringLiteral("transform"), source.attribute(path, QStringLiteral("transform")))
                       + QLatin1Char('>');
        for (int child : source.at(path).children) {
            if (!shapeTags.contains(tag(child)))
                continue;
            const bool evenOdd = source.inherited(child, QStringLiteral("clip-rule")) == QLatin1String("evenodd");
            if (clip.keys.isEmpty() && evenOdd)
                clip.rule = Qt::OddEvenFill;
            const QString key = source.uniqueID();
            clip.keys << key;
            copy += QLatin1Char('<') + tag(child) + attributes(child, skip) + SvgSyntax::attribute(QStringLiteral("fill"), QStringLiteral("#000"))
                    + SvgSyntax::attribute(QStringLiteral("fill-rule"), evenOdd ? QStringLiteral("evenodd") : QStringLiteral("nonzero"))
                    + SvgSyntax::attribute(QStringLiteral("id"), key) + QStringLiteral("/>");
        }
        copy += QStringLiteral("</g>");
        if (clip.keys.isEmpty()) {
            warnings << QStringLiteral("Clipping paths that couldn’t be read were left out.");
            return;
        }
        const SvgElement &e = source.at(element);
        if (tag(element) == QLatin1String("g") && !e.selfClosing)
            source.insert(e.end, copy);
        else
            source.insert(e.begin, QStringLiteral("<g") + SvgSyntax::attribute(QStringLiteral("transform"), source.attribute(element, QStringLiteral("transform")))
                                        + QLatin1Char('>') + copy + QStringLiteral("</g>"));
        clips.insert(element, clip);
    }

    // nanosvg's viewBox fit, which it has already applied to every shape.
    QTransform rootTransform() const
    {
        const QList<double> box = SvgSyntax::numbers(source.attribute(source.root(), QStringLiteral("viewBox")));
        if (box.size() < 4 || !(box[2] > 0 && box[3] > 0))
            return {};
        const QSizeF size = document.size;
        double sx = size.width() / box[2], sy = size.height() / box[3];
        QPointF offset;
        const SvgSyntax::AspectRatio ratio = SvgSyntax::aspectRatio(source.attribute(source.root(), QStringLiteral("preserveAspectRatio")));
        if (!ratio.none) {
            sx = sy = ratio.slice ? std::max(sx, sy) : std::min(sx, sy);
            offset = QPointF((size.width() - box[2] * sx) * ratio.align.x(), (size.height() - box[3] * sy) * ratio.align.y());
        }
        return QTransform::fromTranslate(-box[0], -box[1]) * QTransform::fromScale(sx, sy) * QTransform::fromTranslate(offset.x(), offset.y());
    }

    // Name, opacity, blend and visibility as the element itself sets them.
    void describe(int element, VectorObject &object, QString fallback)
    {
        object.name = source.label(element);
        if (object.name.isEmpty())
            object.name = fallback;
        const QString opacity = source.property(element, QStringLiteral("opacity"));
        if (!opacity.isEmpty()) {
            const double value = opacity.endsWith(QLatin1Char('%')) ? SvgSyntax::length(opacity, 100, 16, 1) : SvgSyntax::length(opacity, 1);
            object.opacity = std::clamp(value, 0.0, 1.0);
        }
        object.blendMode = blendMode(source.property(element, QStringLiteral("mix-blend-mode")));
        object.isVisible = !source.isHidden(element);
        // Inkscape's per-layer lock, from the XML editor / Layers panel.
        object.isLocked = source.attribute(element, QStringLiteral("sodipodi:insensitive")) == QLatin1String("true");
        const auto used = [&](const char *name) {
            const QString value = source.property(element, QLatin1String(name));
            return !value.isEmpty() && value != QLatin1String("none");
        };
        if (used("filter"))
            warnings << QStringLiteral("Filters were left out.");
        if (used("mask"))
            warnings << QStringLiteral("Masks were left out.");
    }

    QUuid add(VectorObject object, const QUuid &parent)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent);
        return id;
    }

    QUuid addLayer(VectorObject layer)
    {
        layer.kind = ObjectKind::layer;
        layer.parentID.reset();
        layer.layerColor = nextLayerColor(layerCount++);
        document.objects.push_back(std::move(layer));
        return document.objects.back().id;
    }

    // Top-level shapes, text and images; runs between layers get one each.
    QUuid looseLayer()
    {
        if (!loose) {
            VectorObject layer;
            layer.name = layerName;
            loose = addLayer(std::move(layer));
        }
        return *loose;
    }

    // A clip group under `parent`, its clipping path already in; or `parent` when the clip is unreadable.
    QUuid clipGroup(int element, const QUuid &parent, VectorObject group)
    {
        VectorObject path;
        path.kind = ObjectKind::path;
        path.name = QStringLiteral("Clipping Path");
        path.fill = Paint::none();
        path.stroke.paint = Paint::none();
        const Clip clip = clips.value(element);
        for (const QString &key : clip.keys) {
            if (const NSVGshape *shape = shapes.value(key)) {
                const VectorPath outline = SvgImport::pathObject(shape).path;
                path.path.contours.insert(path.path.contours.end(), outline.contours.begin(), outline.contours.end());
            }
        }
        path.path.fillRule = clip.rule;
        if (path.path.isEmpty()) {
            warnings << QStringLiteral("Clipping paths that couldn’t be read were left out.");
            return group.kind == ObjectKind::group ? add(std::move(group), parent) : parent;
        }
        group.kind = ObjectKind::group;
        group.isClipGroup = true;
        const QUuid id = add(std::move(group), parent);
        add(std::move(path), id);
        return id;
    }

    // An object in its place, inside a clip group of its own when it is clipped.
    void place(int element, VectorObject object, const QUuid &parent)
    {
        if (!clips.contains(element)) {
            add(std::move(object), parent);
            return;
        }
        VectorObject group;
        group.name = QStringLiteral("Clip Group");
        add(std::move(object), clipGroup(element, parent, std::move(group)));
    }

    void buildLayer(int element, const QTransform &root)
    {
        VectorObject layer;
        describe(element, layer, QStringLiteral("Layer %1").arg(layerCount + 1));
        QUuid parent = addLayer(std::move(layer));
        loose.reset();
        // A layer can't clip; a clip group inside it does.
        if (clips.contains(element)) {
            VectorObject group;
            group.name = QStringLiteral("Clip Group");
            parent = clipGroup(element, parent, std::move(group));
        }
        const QTransform ctm = source.transform(element) * root;
        for (int child : drawnChildren(element))
            buildItem(child, parent, ctm);
    }

    void buildItem(int element, const QUuid &parent, const QTransform &parentCTM)
    {
        const QString &name = tag(element);
        if (name == QLatin1String("g")) {
            VectorObject group;
            group.kind = ObjectKind::group;
            describe(element, group, QStringLiteral("Group"));
            const QUuid id = clips.contains(element) ? clipGroup(element, parent, std::move(group)) : add(std::move(group), parent);
            const QTransform ctm = source.transform(element) * parentCTM;
            for (int child : drawnChildren(element))
                buildItem(child, id, ctm);
        } else if (passThrough.contains(name)) {
            const QTransform ctm = source.transform(element) * parentCTM;
            for (int child : drawnChildren(element))
                buildItem(child, parent, ctm);
        } else if (shapeTags.contains(name)) {
            const NSVGshape *shape = shapes.value(keys.value(element));
            if (!shape)
                return;
            VectorObject path = SvgImport::pathObject(shape);
            if (path.path.isEmpty())
                return;
            if (shape->fill.type == NSVG_PAINT_UNDEF || shape->stroke.type == NSVG_PAINT_UNDEF)
                warnings << QStringLiteral("Patterns were left out.");
            describe(element, path, QStringLiteral("Path"));
            // Opacity that only a <style> sheet sets reaches us through nanosvg.
            if (source.inherited(element, QStringLiteral("opacity")).isEmpty())
                path.opacity = std::clamp(double(shape->opacity), 0.0, 1.0);
            place(element, std::move(path), parent);
        } else if (name == QLatin1String("text")) {
            const auto run = texts.constFind(element);
            if (run == texts.cend())
                return;
            VectorObject text = SvgImport::textObject(source, element, *run, parentCTM, shapes.value(keys.value(element)));
            describe(element, text, text.name);
            place(element, std::move(text), parent);
        } else if (name == QLatin1String("image")) {
            std::optional<SvgImport::PlacedImage> image = SvgImport::imageObject(source, element, parentCTM, folder, warnings);
            if (!image)
                return;
            describe(element, image->object, image->object.name);
            if (image->crop && !clips.contains(element)) {
                // A sliced picture is cropped to its box.
                VectorObject group;
                group.kind = ObjectKind::group;
                group.name = QStringLiteral("Clip Group");
                group.isClipGroup = true;
                const QUuid id = add(std::move(group), parent);
                VectorObject crop;
                crop.name = QStringLiteral("Clipping Path");
                crop.path = *image->crop;
                crop.fill = Paint::none();
                crop.stroke.paint = Paint::none();
                add(std::move(crop), id);
                add(std::move(image->object), id);
            } else {
                place(element, std::move(image->object), parent);
            }
        } else if (name == QLatin1String("use")) {
            warnings << QStringLiteral("Linked copies (<use>) were left out.");
        } else if (name == QLatin1String("foreignObject")) {
            warnings << QStringLiteral("Embedded HTML was left out.");
        }
    }

    // Malformed XML that nanosvg still reads: its shapes, in one layer.
    void flat(const NSVGimage &image)
    {
        const QUuid layer = looseLayer();
        for (const NSVGshape *shape = image.shapes; shape; shape = shape->next) {
            VectorObject path = SvgImport::pathObject(shape);
            if (path.path.isEmpty())
                continue;
            path.name = QString::fromUtf8(shape->id);
            if (path.name.isEmpty())
                path.name = QStringLiteral("Path");
            path.isVisible = shape->flags & NSVG_FLAGS_VISIBLE;
            path.opacity = std::clamp(double(shape->opacity), 0.0, 1.0);
            add(std::move(path), layer);
        }
        warnings << QStringLiteral("The SVG’s structure couldn’t be read, so its shapes are in one layer and its text and images were left out.");
    }
};

thread_local QStringList lastWarningList;

VectorDocument import(const QByteArray &svgOrGzip, const QString &folder, const QString &layerName, QStringList *warnings)
{
    lastWarningList.clear();
    QByteArray svg = svgOrGzip;
    if (isGzip(svg)) {
#ifdef OMASTRATOR_HAVE_ZLIB
        svg = gunzip(svg);
        if (svg.isEmpty())
            throw FileError(QStringLiteral("This .svgz could not be decompressed."));
#else
        throw FileError(QStringLiteral("Reading a compressed .svgz needs zlib, which this build doesn’t have."));
#endif
    }
    if (!svg.contains("<svg"))
        throw FileError(QStringLiteral("This is not an SVG file."));
    QString decoded = QString::fromUtf8(svg);
    if (decoded.startsWith(QChar(0xfeff)))
        decoded.remove(0, 1);
    Builder builder(decoded, folder, layerName);
    builder.prepare();
    // nanosvg writes into its input, and wants it NUL-terminated.
    QByteArray text = builder.source.isValid() ? builder.source.rewritten().toUtf8() : svg;
    text.detach();
    const std::unique_ptr<NSVGimage, void (*)(NSVGimage *)> image(nsvgParse(text.data(), "px", 96), nsvgDelete);
    if (!image)
        throw FileError(QStringLiteral("The SVG could not be read."));
    if (!(image->width > 0 && image->height > 0) || image->width > 1e6 || image->height > 1e6)
        throw FileError(QStringLiteral("The SVG has no size: give it a viewBox, or width and height."));
    // Pixels at 96 dpi count as points one for one.
    VectorDocument document = builder.build(*image);
    lastWarningList = builder.warnings;
    if (warnings)
        *warnings = builder.warnings;
    qCInfo(lcIO) << "parsed SVG" << image->width << "x" << image->height << "into" << document.objects.size() << "objects;"
                 << builder.warnings.size() << "warnings";
    return document;
}
}

namespace SvgImport {
// A path's own `d`, parsed alone: wrapped in a bare <svg> so nanosvg reads just it.
VectorPath parsePathData(const QString &d)
{
    QByteArray svg = (QStringLiteral("<svg><path d='") + d + QStringLiteral("'/></svg>")).toUtf8();
    svg.detach();
    const std::unique_ptr<NSVGimage, void (*)(NSVGimage *)> image(nsvgParse(svg.data(), "px", 96), nsvgDelete);
    if (!image || !image->shapes)
        return {};
    return pathGeometry(image->shapes);
}
}

namespace SvgImporter {
VectorDocument parse(const QByteArray &svg, QStringList *warnings)
{
    return import(svg, {}, QStringLiteral("Layer 1"), warnings);
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    lastWarningList.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to import.").arg(QFileInfo(path).fileName()));
    // Loose shapes go in a layer named after the file.
    QString name = QFileInfo(path).completeBaseName();
    if (name.isEmpty())
        name = QStringLiteral("Layer 1");
    try {
        return import(file.readAll(), QFileInfo(path).absolutePath(), name, warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

QStringList lastWarnings()
{
    return lastWarningList;
}
}
