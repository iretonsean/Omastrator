#include "IO/SvgImporter.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QLineF>
#include <QTransform>
#include <cmath>
#include <memory>

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

// nanosvg packs colours as 0xAABBGGRR.
QColor color(unsigned int packed)
{
    return QColor(int(packed & 0xff), int((packed >> 8) & 0xff), int((packed >> 16) & 0xff), int((packed >> 24) & 0xff));
}

QPointF fraction(const QRectF &bounds, QPointF point)
{
    const double width = bounds.width() > 1e-9 ? bounds.width() : 1;
    const double height = bounds.height() > 1e-9 ? bounds.height() : 1;
    return {(point.x() - bounds.left()) / width, (point.y() - bounds.top()) / height};
}

// Gradient ends are fractions of the object's bounds; nanosvg gives the
// inverse of the gradient's own frame in document space.
Paint paint(const NSVGpaint &source, const QRectF &bounds)
{
    switch (source.type) {
    case NSVG_PAINT_COLOR:
        return Paint::solid(color(source.color));
    case NSVG_PAINT_LINEAR_GRADIENT:
    case NSVG_PAINT_RADIAL_GRADIENT: {
        const NSVGgradient *gradient = source.gradient;
        if (!gradient || gradient->nstops < 1)
            return Paint::none();
        const float *m = gradient->xform;
        bool invertible = false;
        const QTransform frame = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]).inverted(&invertible);
        if (!invertible)
            return Paint::solid(color(gradient->stops[0].color));
        Paint result;
        for (int index = 0; index < gradient->nstops; ++index)
            result.stops.push_back({std::clamp(double(gradient->stops[index].offset), 0.0, 1.0), color(gradient->stops[index].color)});
        result.color = result.stops.front().color;
        if (source.type == NSVG_PAINT_LINEAR_GRADIENT) {
            // The gradient runs along the frame's y axis from 0 to 1.
            result.kind = PaintKind::linearGradient;
            result.start = fraction(bounds, frame.map(QPointF(0, 0)));
            result.end = fraction(bounds, frame.map(QPointF(0, 1)));
        } else {
            // Unit circle; the focal point is dropped.
            result.kind = PaintKind::radialGradient;
            const QPointF center = frame.map(QPointF(0, 0));
            const double radius = (QLineF(center, frame.map(QPointF(1, 0))).length() + QLineF(center, frame.map(QPointF(0, 1))).length()) / 2;
            result.start = fraction(bounds, center);
            result.end = fraction(bounds, center + QPointF(radius, 0));
        }
        return result;
    }
    default:
        return Paint::none();
    }
}

bool onLine(QPointF point, QPointF from, QPointF to)
{
    const QLineF line(from, to);
    const double length = line.length();
    if (length < 1e-6)
        return QLineF(point, from).length() < 1e-3;
    const QPointF d = to - from, p = point - from;
    return std::abs(d.x() * p.y() - d.y() * p.x()) / length < 1e-3;
}

// nanosvg stores every segment as a cubic, lines with handles at thirds;
// those come back as straight sides so nodes stay corners.
Contour contour(const NSVGpath *path)
{
    Contour result;
    result.closed = path->closed;
    if (path->npts < 1)
        return result;
    const float *p = path->pts;
    result.nodes.emplace_back(QPointF(p[0], p[1]));
    for (int index = 0; index + 3 < path->npts; index += 3) {
        const float *s = p + index * 2;
        const QPointF from(s[0], s[1]), c1(s[2], s[3]), c2(s[4], s[5]), to(s[6], s[7]);
        const bool straight = onLine(c1, from, to) && onLine(c2, from, to);
        if (straight && QLineF(from, to).length() < 1e-4)
            continue;
        if (!straight)
            result.nodes.back().out = c1;
        result.nodes.emplace_back(to, straight ? to : c2, to);
    }
    // A closed path ends with a segment back onto its first anchor.
    if (result.closed && result.nodes.size() > 1 && QLineF(result.nodes.back().anchor, result.nodes.front().anchor).length() < 1e-4) {
        result.nodes.front().in = result.nodes.back().in;
        result.nodes.pop_back();
    }
    for (PathNode &node : result.nodes) {
        if (node.hasIn() && node.hasOut()) {
            const QPointF a = node.anchor - node.in, b = node.out - node.anchor;
            const double cross = a.x() * b.y() - a.y() * b.x();
            node.smooth = std::abs(cross) < 1e-3 * std::max(1.0, QLineF({}, a).length() * QLineF({}, b).length());
        }
    }
    return result;
}

VectorObject object(const NSVGshape *shape)
{
    VectorObject object;
    object.kind = ObjectKind::path;
    object.name = QString::fromUtf8(shape->id);
    if (object.name.isEmpty())
        object.name = QStringLiteral("Path");
    object.isVisible = shape->flags & NSVG_FLAGS_VISIBLE;
    object.opacity = std::clamp(double(shape->opacity), 0.0, 1.0);
    for (const NSVGpath *path = shape->paths; path; path = path->next) {
        Contour c = contour(path);
        if (!c.nodes.empty())
            object.path.contours.push_back(std::move(c));
    }
    object.path.fillRule = shape->fillRule == NSVG_FILLRULE_EVENODD ? Qt::OddEvenFill : Qt::WindingFill;
    const QRectF bounds = object.path.bounds();
    object.fill = paint(shape->fill, bounds);
    object.stroke.paint = paint(shape->stroke, bounds);
    object.stroke.width = std::max(0.0f, shape->strokeWidth);
    object.stroke.cap = shape->strokeLineCap == NSVG_CAP_ROUND ? Qt::RoundCap
                        : shape->strokeLineCap == NSVG_CAP_SQUARE ? Qt::SquareCap : Qt::FlatCap;
    object.stroke.join = shape->strokeLineJoin == NSVG_JOIN_ROUND ? Qt::RoundJoin
                         : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? Qt::BevelJoin : Qt::MiterJoin;
    object.stroke.miterLimit = std::max(1.0f, shape->miterLimit);
    for (int index = 0; index < shape->strokeDashCount; ++index)
        object.stroke.dashes.push_back(std::max(0.0f, shape->strokeDashArray[index]));
    return object;
}
}

namespace SvgImporter {
VectorDocument parse(const QByteArray &svg)
{
    if (!svg.contains("<svg"))
        throw FileError(QStringLiteral("This is not an SVG file."));
    // nanosvg writes into its input, and wants it NUL-terminated.
    QByteArray text = svg;
    text.detach();
    const std::unique_ptr<NSVGimage, void (*)(NSVGimage *)> image(nsvgParse(text.data(), "px", 96), nsvgDelete);
    if (!image)
        throw FileError(QStringLiteral("The SVG could not be read."));
    if (!(image->width > 0 && image->height > 0) || image->width > 1e6 || image->height > 1e6)
        throw FileError(QStringLiteral("The SVG has no size: give it a viewBox, or width and height."));
    // Pixels at 96 dpi count as points one for one.
    VectorDocument document = VectorDocument::blank(QSizeF(image->width, image->height));
    const QUuid layer = document.layers().front();
    int count = 0;
    for (const NSVGshape *shape = image->shapes; shape; shape = shape->next) {
        VectorObject path = object(shape);
        if (path.path.isEmpty())
            continue;
        document.insert(std::move(path), layer);
        ++count;
    }
    qCInfo(lcIO) << "parsed SVG" << image->width << "x" << image->height << "with" << count << "shapes";
    return document;
}

VectorDocument read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to import.").arg(QFileInfo(path).fileName()));
    VectorDocument document;
    try {
        document = parse(file.readAll());
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
    // One layer, named after the file.
    const QString name = QFileInfo(path).completeBaseName();
    if (!name.isEmpty())
        document.find(document.layers().front())->name = name;
    return document;
}
}
