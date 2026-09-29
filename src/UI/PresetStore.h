#pragma once
#include "Document/VectorDocument.h"
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <vector>

enum class LengthUnit { pt, px, in, mm };

// Size presets the designer keeps, in $XDG_CONFIG_HOME/omastrator/presets.json:
//   {"version": 1,
//    "documents": {"saved": [{"name", "width", "height", "unit"}], "hidden": ["A3"]},
//    "frames":    {"saved": [], "hidden": []}}
// Sizes are points; `unit` is only how the size is shown. Each section holds the presets
// the user saved (newest first) and the names of built-ins they hid. The Frame tool's
// presets use the "frames" section the same way; a section this code doesn't know is kept.
namespace PresetStore {
inline constexpr const char *documents = "documents";
inline constexpr const char *frames = "frames";
inline constexpr double maximumPoints = VectorDocument::maximumArtboardSide;
// "1,000,000 points (about 350 m)", for the notes that state the limit.
QString limitDescription();

struct Entry {
    QString name;
    QSizeF points;
    LengthUnit unit = LengthUnit::px;
};
struct Section {
    std::vector<Entry> saved;
    QStringList hidden;
};

QString path();
// A missing or empty file reads as empty; so does one that can't be opened or parsed, but
// then `valid` (when given) is false. A bad entry is skipped.
Section read(const QString &section, bool *valid = nullptr);
// Returns why it failed, or empty. Other sections stay as they are. A file that exists but
// can't be read is never replaced: this refuses, so nothing the user wrote is lost.
QString write(const QString &section, const Section &value);
// The documents and frames sections as they'd be written, for Export Settings; `valid` is
// false (and the result empty) for a file that can't be read.
QJsonObject exportSections(bool *valid = nullptr);
// `sections` cleaned up as reading them would: bad entries and repeated names dropped.
QJsonObject normalised(const QJsonObject &sections);
// Import Settings: replaces each of the two sections found in `sections` (bad entries skipped)
// and keeps everything else in the file. Refuses an unreadable file like write().
QString replaceSections(const QJsonObject &sections);
}
