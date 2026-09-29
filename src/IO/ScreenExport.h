#pragma once
#include "Document/VectorDocument.h"
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

// Export for Screens (docs/QOL-RESEARCH.md P2-10): artboards and export assets, each at
// a batch of scales and in a batch of formats, in one go.
namespace ScreenExport {
struct Settings {
    // 1, 2, 3 and so on; empty renders as just 1×.
    std::vector<double> scales{1};
    // Lowercase: "png", "jpg", "svg", "pdf", "webp".
    QStringList formats{QStringLiteral("png")};
    QString folder;
};

// One file finished, for a progress bar; `total` is fixed for the whole run.
struct Progress {
    QString name;
    int done = 0;
    int total = 0;
};

// The suffix a scale's file name takes: empty at 1×, "@2x", "@0.5x", and so on.
QString scaleSuffix(double scale);

// Artboards (by index into `document.allArtboards()`) and export assets (object ids, cropped
// the way Share's "just this selection" export is) as their own files, named "<safe
// name><suffix>.<ext>". Vector formats (svg, pdf) write once, at 1×; webp fails cleanly, file by
// file, without the plugin. Returns every path actually written, in the order they were; the
// reasons a file wasn't (too large at that scale, an unwritable folder) go to `skipped`, once each.
QStringList run(const VectorDocument &document, const std::vector<int> &artboardIndices, const std::vector<QUuid> &assetIds,
                const Settings &settings, const std::function<void(const Progress &)> &progress = {}, QStringList *skipped = nullptr);
}
