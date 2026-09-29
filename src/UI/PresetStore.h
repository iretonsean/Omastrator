#pragma once
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
inline constexpr double maximumPoints = 16384;

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
// A missing, unreadable or malformed file reads as empty; a bad entry is skipped.
Section read(const QString &section);
// Returns why it failed, or empty. Other sections stay as they are; a file that isn't
// valid JSON is moved to presets.json.bak first so nothing the user wrote is lost.
QString write(const QString &section, const Section &value);
}
