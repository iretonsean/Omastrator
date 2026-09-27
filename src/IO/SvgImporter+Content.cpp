#include "Document/FontFeatures.h"
#include "Document/PathOperations.h"
#include "IO/ImageImporter.h"
#include "IO/SvgImporterParts.h"
#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "nanosvg/nanosvg.h"
#pragma GCC diagnostic pop

namespace {
std::optional<double> first(const QString &list)
{
    const QList<double> values = SvgSyntax::numbers(list);
    return values.isEmpty() ? std::nullopt : std::optional<double>(values.front());
}

// A line's characters, each with the element it was written in.
struct Owned {
    QString text;
    std::vector<int> owners;
};

// Newlines and tabs are spaces; outside xml:space="preserve" runs collapse and ends trim.
Owned spaced(Owned line, bool preserve)
{
    for (QChar &c : line.text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t'))
            c = QLatin1Char(' ');
    }
    if (preserve)
        return line;
    Owned result;
    for (qsizetype index = 0; index < line.text.size(); ++index) {
        const QChar c = line.text[index];
        if (c == QLatin1Char(' ') && (result.text.isEmpty() || result.text.back() == QLatin1Char(' ')))
            continue;
        result.text += c;
        result.owners.push_back(line.owners[size_t(index)]);
    }
    if (result.text.endsWith(QLatin1Char(' '))) {
        result.text.chop(1);
        result.owners.pop_back();
    }
    return result;
}

QString family(const QString &list)
{
    QString name = list.section(QLatin1Char(','), 0, 0).trimmed();
    if (name.size() > 1 && (name.front() == QLatin1Char('\'') || name.front() == QLatin1Char('"')))
        name = name.mid(1, name.size() - 2).trimmed();
    if (name == QLatin1String("sans-serif"))
        return QStringLiteral("Sans Serif");
    if (name == QLatin1String("serif"))
        return QStringLiteral("Serif");
    if (name == QLatin1String("monospace"))
        return QStringLiteral("Monospace");
    return name;
}

// nanosvg's own measure, which it scaled the stroke width by.
double averageScale(const QTransform &t)
{
    return (std::hypot(t.m11(), t.m21()) + std::hypot(t.m12(), t.m22())) / 2;
}

// A span's look, as its ancestors up to the text element say.
CharacterFormat characterFormat(const SvgSource &source, int element, int textElement)
{
    CharacterFormat format;
    const QString fontFamily = family(source.inherited(element, QStringLiteral("font-family")));
    if (!fontFamily.isEmpty())
        format.family = fontFamily;
    format.size = SvgSyntax::length(source.inherited(element, QStringLiteral("font-size")), 16, 16, 16);
    if (!(format.size > 0))
        format.size = 16;
    const QString weight = source.inherited(element, QStringLiteral("font-weight"));
    const int weightValue = weight == QLatin1String("bold") || weight == QLatin1String("bolder") ? 700
        : weight == QLatin1String("lighter")                                                    ? 300
        : weight.toInt() > 0                                                                    ? weight.toInt()
                                                                                                : 400;
    const QString fontStyle = source.inherited(element, QStringLiteral("font-style"));
    const bool slanted = fontStyle == QLatin1String("italic") || fontStyle == QLatin1String("oblique");
    // Plain text keeps the default face name, which renders as the family's regular.
    if (weightValue != 400 || slanted)
        format.style = TextContent::styleFor(format.family, weightValue, slanted);
    // Letter spacing reads in pt; tracking is in 1/1000 em.
    format.tracking = std::round(SvgSyntax::length(source.inherited(element, QStringLiteral("letter-spacing")), 0, format.size) / format.size * 1e6) / 1000;
    if (source.inherited(element, QStringLiteral("font-variant")) == QLatin1String("small-caps"))
        format.textCase = TextCase::smallCaps;
    else if (source.inherited(element, QStringLiteral("text-transform")) == QLatin1String("uppercase"))
        format.textCase = TextCase::allCaps;
    const QString decoration = source.inherited(element, QStringLiteral("text-decoration"));
    format.underline = decoration.contains(QLatin1String("underline"));
    format.strikethrough = decoration.contains(QLatin1String("line-through"));
    format.features = FontFeatures::fromCss(source.inherited(element, QStringLiteral("font-feature-settings")));
    // Shifts add up from the text element down.
    for (int at = element; at >= 0; at = source.at(at).parent) {
        format.baselineShift += SvgSyntax::length(source.property(at, QStringLiteral("baseline-shift")), 0, format.size);
        if (at == textElement)
            break;
    }
    const QColor fill = QColor::fromString(source.inherited(element, QStringLiteral("fill")));
    if (fill.isValid())
        format.fill = fill;
    return format;
}

QImage decoded(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    return reader.read();
}

QImage loadImage(const QString &href, const QString &folder, QStringList &warnings)
{
    if (href.startsWith(QLatin1String("data:"))) {
        const qsizetype comma = href.indexOf(QLatin1Char(','));
        const QString header = href.mid(5, comma - 5);
        const QString payload = comma < 0 ? QString() : href.mid(comma + 1);
        if (header.startsWith(QLatin1String("image/svg"))) {
            warnings << QStringLiteral("Pictures that are themselves SVG were left out.");
            return {};
        }
        const QByteArray bytes = header.contains(QLatin1String(";base64")) ? QByteArray::fromBase64(payload.toLatin1())
                                                                          : QByteArray::fromPercentEncoding(payload.toUtf8());
        QImage image = decoded(bytes);
        if (image.isNull())
            warnings << QStringLiteral("Images that couldn’t be read were left out.");
        return image;
    }
    const QUrl url(href);
    if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
        warnings << QStringLiteral("Images linked from the web were left out.");
        return {};
    }
    QString path = url.isLocalFile() ? url.toLocalFile() : QUrl::fromPercentEncoding(href.toUtf8());
    if (QDir::isRelativePath(path)) {
        if (folder.isEmpty()) {
            warnings << QStringLiteral("Linked images were left out: they are found only when the SVG is opened from a file.");
            return {};
        }
        path = QDir(folder).filePath(path);
    }
    try {
        return ImageImporter::read(path);
    } catch (const FileError &) {
        warnings << QStringLiteral("Linked images that couldn’t be found or read were left out.");
        return {};
    }
}
}

namespace SvgImport {
std::optional<TextRun> readText(const SvgSource &source, int element)
{
    const bool preserve = source.inherited(element, QStringLiteral("xml:space")) == QLatin1String("preserve");
    bool hasSpans = false;
    for (int child : source.at(element).children)
        hasSpans = hasSpans || source.at(child).tag != QLatin1String("#text");
    std::optional<double> originX = first(source.attribute(element, QStringLiteral("x")));
    double baseline = first(source.attribute(element, QStringLiteral("y"))).value_or(0);
    std::vector<Owned> lines;
    Owned line;
    std::vector<double> baselines;
    int style = -1;
    bool started = false;
    QString pathHref;
    double startOffsetValue = 0;
    bool startPercent = true;
    // Spans that set y or dy start a line; the rest run on.
    std::function<void(int)> collect = [&](int parent) {
        for (int child : source.at(parent).children) {
            const SvgElement &node = source.at(child);
            if (node.tag == QLatin1String("#text")) {
                // Indentation between spans is layout, not text.
                if (parent == element && hasSpans && node.text.trimmed().isEmpty())
                    continue;
                if (!started) {
                    started = true;
                    baselines.push_back(baseline);
                }
                line.text += node.text;
                line.owners.insert(line.owners.end(), size_t(node.text.size()), parent);
                if (style < 0 && !node.text.trimmed().isEmpty())
                    style = parent;
            } else if (node.tag == QLatin1String("tspan")) {
                const std::optional<double> y = first(source.attribute(child, QStringLiteral("y")));
                const std::optional<double> dy = first(source.attribute(child, QStringLiteral("dy")));
                const bool moves = y || (dy && *dy != 0);
                const double at = y.value_or(baseline) + dy.value_or(0);
                if (!started) {
                    started = true;
                    if (!originX)
                        originX = first(source.attribute(child, QStringLiteral("x")));
                    baselines.push_back(at);
                } else if (moves) {
                    lines.push_back(line);
                    line = {};
                    baselines.push_back(at);
                }
                baseline = at;
                collect(child);
            } else if (node.tag == QLatin1String("textPath")) {
                QString href = source.attribute(child, QStringLiteral("href"));
                if (href.isEmpty())
                    href = source.attribute(child, QStringLiteral("xlink:href"));
                // Kept as "#id": SvgSource::reference() wants the leading '#'.
                if (!href.isEmpty()) {
                    pathHref = href;
                    const QString offset = source.attribute(child, QStringLiteral("startOffset"));
                    startOffsetValue = first(offset).value_or(0);
                    startPercent = offset.trimmed().endsWith(QLatin1Char('%'));
                }
                collect(child);
            } else if (node.tag == QLatin1String("a")) {
                collect(child);
            }
        }
    };
    collect(element);
    lines.push_back(line);
    QString text;
    std::vector<int> owners;
    for (size_t index = 0; index < lines.size(); ++index) {
        const Owned spacedLine = spaced(lines[index], preserve);
        if (index) {
            text += QLatin1Char('\n');
            owners.push_back(owners.empty() ? element : owners.back());
        }
        text += spacedLine.text;
        owners.insert(owners.end(), spacedLine.owners.begin(), spacedLine.owners.end());
    }
    if (text.trimmed().isEmpty())
        return std::nullopt;

    // Each span's look; the one most characters share is the text's own, the rest are runs.
    std::map<int, CharacterFormat> formats;
    std::vector<std::pair<CharacterFormat, int>> counts;
    for (const int owner : owners) {
        auto found = formats.find(owner);
        if (found == formats.end())
            found = formats.emplace(owner, characterFormat(source, owner, element)).first;
        auto counted = std::find_if(counts.begin(), counts.end(), [&](const auto &entry) { return entry.first == found->second; });
        if (counted == counts.end())
            counts.push_back({found->second, 1});
        else
            ++counted->second;
    }
    const auto most = std::max_element(counts.begin(), counts.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
    const CharacterFormat own = most == counts.end() ? characterFormat(source, style < 0 ? element : style, element) : most->first;

    TextRun run;
    run.style = style < 0 ? element : style;
    for (size_t index = 0; index < owners.size(); ++index) {
        if (formats[owners[index]] == own && !text[qsizetype(index)].isSpace()) {
            run.style = owners[index];
            break;
        }
    }
    TextContent &content = run.content;
    content.text = text;
    content.character() = own;
    content.fill.reset();
    for (size_t index = 0; index < owners.size(); ++index) {
        CharacterFormat format = formats[owners[index]];
        if (format.fill == own.fill)
            format.fill.reset();
        if (format == content.character())
            continue;
        ::TextRun *last = content.runs.empty() ? nullptr : &content.runs.back();
        if (last && last->start + last->length == int(index) && last->format == format)
            ++last->length;
        else
            content.runs.push_back({int(index), 1, format});
    }
    const QString kerning = source.inherited(run.style, QStringLiteral("font-kerning"));
    if (kerning == QLatin1String("none") || source.inherited(run.style, QStringLiteral("kerning")) == QLatin1String("0"))
        content.kerning = TextKerning::none;
    const QString anchor = source.inherited(run.style, QStringLiteral("text-anchor"));
    content.alignment = anchor == QLatin1String("middle") ? TextAlignment::center
                        : anchor == QLatin1String("end")  ? TextAlignment::right
                                                           : TextAlignment::left;
    if (baselines.size() > 1 && baselines[1] > baselines[0]) {
        const double leading = std::round((baselines[1] - baselines[0]) * 10000) / 10000;
        if (std::abs(leading - content.size * 1.2) > 1e-3)
            content.leading = leading;
    }
    run.origin = QPointF(originX.value_or(0), baselines.empty() ? baseline : baselines.front());
    run.pathHref = pathHref;
    run.startOffsetValue = startOffsetValue;
    run.startPercent = startPercent;
    return run;
}

VectorObject textObject(const SvgSource &source, int element, const TextRun &run, const QTransform &parentCTM, const NSVGshape *probe)
{
    VectorObject object;
    object.kind = ObjectKind::text;
    object.text = run.content;
    object.transform = QTransform::fromTranslate(run.origin.x(), run.origin.y()) * source.transform(element) * parentCTM;
    object.fill = Paint::solid(Qt::black);
    object.stroke.paint = Paint::none();
    bool invertible = false;
    const QTransform toLocal = object.transform.inverted(&invertible);
    if (!run.pathHref.isEmpty() && invertible) {
        const int pathElement = source.reference(run.pathHref);
        const QString d = pathElement >= 0 ? source.attribute(pathElement, QStringLiteral("d")) : QString();
        if (pathElement >= 0 && !d.isEmpty()) {
            // The path's own d, in the text's local coordinates: its element's transform,
            // then the document's, then out of the text's.
            const VectorPath geometry = SvgImport::parsePathData(d).transformed(source.transform(pathElement) * parentCTM * toLocal);
            if (!geometry.isEmpty()) {
                double start = run.startOffsetValue;
                if (run.startPercent) {
                    start /= 100;
                } else {
                    const double length = geometry.painterPath().length();
                    start = length > 1e-6 ? start / length : 0;
                }
                object.text.onPath = TextPath{geometry, std::clamp(start, 0.0, 1.0), false};
            }
        }
    }
    if (probe && invertible) {
        const double scale = averageScale(object.transform);
        applyPaint(object, probe, run.content.outline().boundingRect(), toLocal, scale > 1e-9 ? scale : 1);
    }
    object.name = run.content.text.section(QLatin1Char('\n'), 0, 0).left(40);
    return object;
}

std::optional<PlacedImage> imageObject(const SvgSource &source, int element, const QTransform &parentCTM, const QString &folder,
                                       QStringList &warnings)
{
    QString href = source.attribute(element, QStringLiteral("xlink:href"));
    if (href.isEmpty())
        href = source.attribute(element, QStringLiteral("href"));
    if (href.isEmpty())
        return std::nullopt;
    const QImage image = loadImage(href, folder, warnings);
    if (image.isNull())
        return std::nullopt;
    const QSizeF pixels = image.size();
    const QString widthText = source.attribute(element, QStringLiteral("width"));
    const QString heightText = source.attribute(element, QStringLiteral("height"));
    const double x = SvgSyntax::length(source.attribute(element, QStringLiteral("x")));
    const double y = SvgSyntax::length(source.attribute(element, QStringLiteral("y")));
    // Missing or auto sizes are the picture's own.
    const double width = SvgSyntax::length(widthText, pixels.width());
    const double height = SvgSyntax::length(heightText, pixels.height());
    if (!(width > 0 && height > 0))
        return std::nullopt;
    const SvgSyntax::AspectRatio ratio = SvgSyntax::aspectRatio(source.attribute(element, QStringLiteral("preserveAspectRatio")));
    double sx = width / pixels.width(), sy = height / pixels.height();
    QPointF offset;
    if (!ratio.none) {
        sx = sy = ratio.slice ? std::max(sx, sy) : std::min(sx, sy);
        offset = QPointF((width - pixels.width() * sx) * ratio.align.x(), (height - pixels.height() * sy) * ratio.align.y());
    }
    const QTransform place = source.transform(element) * parentCTM;
    PlacedImage result;
    VectorObject &object = result.object;
    object.kind = ObjectKind::image;
    object.image = image;
    object.transform = QTransform::fromScale(sx, sy) * QTransform::fromTranslate(x + offset.x(), y + offset.y()) * place;
    object.fill = Paint::none();
    object.stroke.paint = Paint::none();
    object.name = QStringLiteral("Image");
    if (ratio.slice && (pixels.width() * sx > width + 1e-6 || pixels.height() * sy > height + 1e-6))
        result.crop = Shapes::rectangle(QRectF(x, y, width, height)).transformed(place);
    return result;
}
}
