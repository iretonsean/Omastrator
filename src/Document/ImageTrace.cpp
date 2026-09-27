// Ported from omadesign by Michael C Hurley, MIT (src/trace.rs):
// threshold or median-cut palette, pixel-edge contours, then Douglas–Peucker.
#include "Document/ImageTrace.h"
#include <QLineF>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace {
// Tracing cost grows with pixels; bigger images are sampled down, then scaled back.
constexpr int maximumEdge = 1600;

struct Raster {
    int width = 0;
    int height = 0;
    // RGBA, not premultiplied.
    std::vector<uint8_t> data;
    const uint8_t *pixel(size_t index) const { return data.data() + index * 4; }
    size_t count() const { return size_t(width) * size_t(height); }
};

Raster downsample(const QImage &source, double &scale)
{
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    const int longest = std::max(image.width(), image.height());
    Raster raster;
    scale = 1;
    double k = 1;
    raster.width = image.width();
    raster.height = image.height();
    if (longest > maximumEdge) {
        k = double(maximumEdge) / longest;
        raster.width = std::max(1, int(std::lround(image.width() * k)));
        raster.height = std::max(1, int(std::lround(image.height() * k)));
        scale = 1 / k;
    }
    raster.data.resize(raster.count() * 4);
    // Nearest sample at each output pixel's centre.
    const double sx = double(image.width()) / raster.width, sy = double(image.height()) / raster.height;
    for (int y = 0; y < raster.height; ++y) {
        const int y0 = std::min(image.height() - 1, int(std::floor((y + 0.5) * sy)));
        const uint8_t *row = image.constScanLine(y0);
        for (int x = 0; x < raster.width; ++x) {
            const int x0 = std::min(image.width() - 1, int(std::floor((x + 0.5) * sx)));
            std::copy_n(row + x0 * 4, 4, raster.data.data() + (size_t(y) * raster.width + x) * 4);
        }
    }
    return raster;
}

double luma(int r, int g, int b)
{
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
}

std::vector<uint8_t> monoMask(const Raster &source, const ImageTrace::Options &options)
{
    std::vector<uint8_t> mask(source.count(), 0);
    const double threshold = std::clamp(options.threshold, 0.02, 0.98);
    for (size_t index = 0; index < source.count(); ++index) {
        const uint8_t *px = source.pixel(index);
        const int alpha = px[3];
        if (alpha < 16)
            continue;
        const double y = luma(px[0], px[1], px[2]);
        if (options.ignoreWhite && y > 0.92 && alpha > 200)
            continue;
        // Antialiased edges count a little lighter as ink.
        if (y < threshold || (alpha < 250 && y < threshold + 0.15))
            mask[index] = 1;
    }
    return mask;
}

using Rgb = std::array<uint8_t, 3>;

std::pair<int, int> channelRange(const std::vector<Rgb> &points)
{
    Rgb low{255, 255, 255}, high{0, 0, 0};
    for (const Rgb &p : points) {
        for (int c = 0; c < 3; ++c) {
            low[c] = std::min(low[c], p[c]);
            high[c] = std::max(high[c], p[c]);
        }
    }
    int axis = 0, range = 0;
    for (int c = 0; c < 3; ++c) {
        const int r = std::max(0, high[c] - low[c]);
        if (r >= range) {
            range = r;
            axis = c;
        }
    }
    return {axis, range};
}

Rgb average(const std::vector<Rgb> &points)
{
    if (points.empty())
        return {0, 0, 0};
    std::array<uint64_t, 3> sum{0, 0, 0};
    for (const Rgb &p : points) {
        for (int c = 0; c < 3; ++c)
            sum[c] += p[c];
    }
    const uint64_t n = points.size();
    return {uint8_t(sum[0] / n), uint8_t(sum[1] / n), uint8_t(sum[2] / n)};
}

// Splits the bucket with the widest channel at its median until there are `k`.
std::vector<Rgb> medianCut(std::vector<Rgb> samples, size_t k)
{
    std::vector<std::vector<Rgb>> buckets;
    buckets.push_back(std::move(samples));
    while (buckets.size() < k) {
        size_t split = 0;
        int axis = 0, range = -1;
        for (size_t index = 0; index < buckets.size(); ++index) {
            const auto [a, r] = channelRange(buckets[index]);
            if (r > range) {
                split = index;
                axis = a;
                range = r;
            }
        }
        if (range < 2 || buckets[split].size() < 2)
            break;
        std::vector<Rgb> &points = buckets[split];
        const size_t middle = points.size() / 2;
        std::nth_element(points.begin(), points.begin() + ptrdiff_t(middle), points.end(),
                         [axis](const Rgb &a, const Rgb &b) { return a[size_t(axis)] < b[size_t(axis)]; });
        std::vector<Rgb> right(points.begin() + ptrdiff_t(middle), points.end());
        points.resize(middle);
        buckets.push_back(std::move(right));
    }
    std::vector<Rgb> palette;
    for (const auto &bucket : buckets) {
        if (!bucket.empty())
            palette.push_back(average(bucket));
    }
    return palette;
}

constexpr uint16_t unassigned = UINT16_MAX;

// The palette and each pixel's entry in it; clear pixels get none.
std::pair<std::vector<Rgb>, std::vector<uint16_t>> palettize(const Raster &source, size_t k)
{
    std::vector<Rgb> samples;
    samples.reserve(source.count());
    for (size_t index = 0; index < source.count(); ++index) {
        const uint8_t *px = source.pixel(index);
        if (px[3] >= 16)
            samples.push_back({px[0], px[1], px[2]});
    }
    std::vector<uint16_t> indices(source.count(), unassigned);
    if (samples.empty())
        return {{}, indices};
    const std::vector<Rgb> palette = medianCut(std::move(samples), std::max<size_t>(1, k));
    for (size_t index = 0; index < source.count(); ++index) {
        const uint8_t *px = source.pixel(index);
        if (px[3] < 16)
            continue;
        uint32_t bestDistance = UINT32_MAX;
        for (size_t c = 0; c < palette.size(); ++c) {
            const int dr = px[0] - palette[c][0], dg = px[1] - palette[c][1], db = px[2] - palette[c][2];
            const uint32_t distance = uint32_t(dr * dr + dg * dg + db * db);
            if (distance < bestDistance) {
                bestDistance = distance;
                indices[index] = uint16_t(c);
            }
        }
    }
    return {palette, indices};
}

// Outgoing pixel edges at each grid corner, as bits.
enum Direction : uint8_t { down = 1, up = 2, left = 4, right = 8 };

QPoint step(uint8_t direction)
{
    switch (direction) {
    case down: return {0, 1};
    case up: return {0, -1};
    case left: return {-1, 0};
    default: return {1, 0};
    }
}

uint8_t direction(QPoint delta)
{
    if (delta.y() > 0)
        return down;
    if (delta.y() < 0)
        return up;
    return delta.x() < 0 ? left : right;
}

// Walks pixel edges with the ink on the left, so outer rings and holes wind
// oppositely. At a corner shared by diagonal pixels the walk turns left, keeping
// each ring simple.
std::vector<std::vector<QPointF>> traceMask(const std::vector<uint8_t> &mask, int w, int h)
{
    const auto ink = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && mask[size_t(y) * w + x]; };
    const int stride = w + 1;
    std::vector<uint8_t> edges(size_t(stride) * (h + 1), 0);
    const auto corner = [&](int x, int y) -> uint8_t & { return edges[size_t(y) * stride + x]; };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!ink(x, y))
                continue;
            if (!ink(x - 1, y))
                corner(x, y) |= down;
            if (!ink(x + 1, y))
                corner(x + 1, y + 1) |= up;
            if (!ink(x, y - 1))
                corner(x + 1, y) |= left;
            if (!ink(x, y + 1))
                corner(x, y + 1) |= right;
        }
    }
    std::vector<std::vector<QPointF>> loops;
    for (size_t start = 0; start < edges.size(); ++start) {
        while (edges[start]) {
            const QPoint origin(int(start % stride), int(start / stride));
            std::vector<QPoint> ring{origin};
            QPoint at = origin;
            uint8_t heading = 0;
            for (;;) {
                uint8_t &outs = corner(at.x(), at.y());
                if (!outs)
                    break;
                uint8_t chosen = 0;
                if (heading) {
                    // Left, straight, then right, in screen coordinates.
                    const QPoint d = step(heading);
                    for (const QPoint turn : {QPoint(d.y(), -d.x()), d, QPoint(-d.y(), d.x())}) {
                        if (outs & direction(turn)) {
                            chosen = direction(turn);
                            break;
                        }
                    }
                }
                if (!chosen)
                    chosen = uint8_t(outs & -outs);
                outs &= uint8_t(~chosen);
                heading = chosen;
                at += step(chosen);
                if (at == origin)
                    break;
                ring.push_back(at);
            }
            if (ring.size() < 4)
                continue;
            // Only corners stay: straight runs of pixel edges collapse.
            std::vector<QPointF> points;
            const size_t n = ring.size();
            for (size_t index = 0; index < n; ++index) {
                const QPoint a = ring[(index + n - 1) % n], b = ring[index], c = ring[(index + 1) % n];
                const QPoint ab = b - a, bc = c - b;
                if (ab.x() * bc.y() - ab.y() * bc.x() != 0)
                    points.emplace_back(b);
            }
            if (points.size() >= 3)
                loops.push_back(std::move(points));
        }
    }
    return loops;
}

double segmentDistance(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double length = QPointF::dotProduct(ab, ab);
    if (length < 1e-12)
        return QLineF(p, a).length();
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / length, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

// Douglas–Peucker with an explicit stack: rings can be long.
std::vector<QPointF> simplify(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() < 3)
        return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack{{0, points.size() - 1}};
    while (!stack.empty()) {
        const auto [first, last] = stack.back();
        stack.pop_back();
        double farthest = 0;
        size_t index = 0;
        for (size_t i = first + 1; i < last; ++i) {
            const double d = segmentDistance(points[i], points[first], points[last]);
            if (d > farthest) {
                farthest = d;
                index = i;
            }
        }
        if (farthest > epsilon) {
            keep[index] = true;
            stack.emplace_back(first, index);
            stack.emplace_back(index, last);
        }
    }
    std::vector<QPointF> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i])
            result.push_back(points[i]);
    }
    return result;
}

std::vector<QPointF> simplifyClosed(const std::vector<QPointF> &points, double epsilon)
{
    if (points.size() < 4)
        return points;
    std::vector<QPointF> ring = points;
    ring.push_back(points.front());
    std::vector<QPointF> result = simplify(ring, epsilon);
    if (result.size() >= 2 && QLineF(result.front(), result.back()).length() < 1e-3)
        result.pop_back();
    return result.size() < 3 ? points : result;
}

double area(const std::vector<QPointF> &points)
{
    double sum = 0;
    for (size_t i = 0, n = points.size(); i < n; ++i) {
        const QPointF a = points[i], b = points[(i + 1) % n];
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return sum / 2;
}

std::optional<VectorObject> traced(const std::vector<uint8_t> &mask, const Raster &source, const ImageTrace::Options &options,
                                   const QColor &color, double scale)
{
    const double epsilon = std::max(0.2, options.smoothness);
    const double minimumArea = std::max(1.0, options.minimumArea);
    VectorObject object;
    object.kind = ObjectKind::path;
    object.name = QStringLiteral("Traced %1").arg(color.name());
    object.fill = Paint::solid(color);
    object.stroke.paint = Paint::none();
    // Holes wind the other way, but even-odd keeps them holes whatever the order.
    object.path.fillRule = Qt::OddEvenFill;
    for (const std::vector<QPointF> &loop : traceMask(mask, source.width, source.height)) {
        const std::vector<QPointF> points = simplifyClosed(loop, epsilon);
        if (points.size() < 3 || std::abs(area(points)) < minimumArea)
            continue;
        Contour contour;
        contour.closed = true;
        for (const QPointF &p : points)
            contour.nodes.emplace_back(p * scale);
        object.path.contours.push_back(std::move(contour));
    }
    if (object.path.contours.empty())
        return std::nullopt;
    return object;
}
}

namespace ImageTrace {
std::vector<VectorObject> trace(const QImage &image, const Options &options)
{
    if (image.isNull() || image.width() < 1 || image.height() < 1)
        return {};
    double scale = 1;
    const Raster source = downsample(image, scale);
    const int colors = std::clamp(options.colors, 1, 16);
    std::vector<VectorObject> result;
    if (colors == 1) {
        if (auto object = traced(monoMask(source, options), source, options, QColor(0x11, 0x11, 0x11), scale))
            result.push_back(std::move(*object));
        return result;
    }
    const auto [palette, indices] = palettize(source, size_t(colors));
    for (size_t c = 0; c < palette.size(); ++c) {
        const Rgb &rgb = palette[c];
        if (options.ignoreWhite && luma(rgb[0], rgb[1], rgb[2]) > 0.92)
            continue;
        std::vector<uint8_t> mask(source.count(), 0);
        bool any = false;
        for (size_t index = 0; index < indices.size(); ++index) {
            if (indices[index] == c) {
                mask[index] = 1;
                any = true;
            }
        }
        if (!any)
            continue;
        if (auto object = traced(mask, source, options, QColor(rgb[0], rgb[1], rgb[2]), scale))
            result.push_back(std::move(*object));
    }
    return result;
}
}

std::optional<QUuid> ImageTrace::traceInPlace(VectorDocument &document, const QUuid &image, const Options &options)
{
    const VectorObject *source = document.find(image);
    if (!source || source->kind != ObjectKind::image || !source->parentID)
        return std::nullopt;
    const std::vector<VectorObject> traced = trace(source->image, options);
    if (traced.empty())
        return std::nullopt;
    const QTransform placed = source->transform;
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = QStringLiteral("Image Trace");
    const QUuid groupID = group.id;
    document.insert(std::move(group), *source->parentID, image);
    for (const VectorObject &path : traced)
        document.insert(path, groupID);
    // Traced paths are in pixels; the image's placement puts them on the artboard.
    document.transform(groupID, placed);
    document.remove({image});
    return groupID;
}
