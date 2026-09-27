#pragma once
#include "Document/VectorDocument.h"
#include <QImage>
#include <vector>

// Object ▸ Image Trace: pixels to filled paths, black and white or in colour.
// Ported from omadesign by Michael C Hurley, MIT (src/trace.rs).
namespace ImageTrace {
// Defaults suit a logo on white or a transparent PNG.
struct Options {
    // 1 is black and white by luminance; 2 to 16 are colour buckets.
    int colors = 1;
    // Black and white: pixels darker than this luminance (0 to 1) become fill.
    double threshold = 0.55;
    // Ramer–Douglas–Peucker tolerance in pixels.
    double smoothness = 1.5;
    // Skips near-white buckets so the paper does not become a rectangle.
    bool ignoreWhite = true;
    // Contours smaller than this many square pixels are dropped.
    double minimumArea = 8;
};

// One even-odd filled path per colour, in the image's pixel coordinates,
// bottom to top in palette order. Paths have no stroke.
std::vector<VectorObject> trace(const QImage &image, const Options &options = {});
}
