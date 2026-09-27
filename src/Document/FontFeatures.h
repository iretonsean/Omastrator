#pragma once
#include <QFont>
#include <QSet>
#include <QString>
#include <QStringList>
#include <map>
#include <vector>

// OpenType features: the ones Character's OpenType popover offers, and which
// of them a font has. Applying them needs QFont::setFeature, Qt 6.7 or later.
namespace FontFeatures {
struct Feature {
    const char *tag;
    const char *name;
    // HarfBuzz turns these on unless told otherwise.
    bool onByDefault = false;
};
// Ligatures, alternates, small caps, fractions, ordinals and the figure styles.
const std::vector<Feature> &offered();
// ss01 … ss20.
QStringList stylisticSets();
// Whether this build can apply features at all.
constexpr bool applicable()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return true;
#else
    return false;
#endif
}
// The tags the font's GSUB and GPOS tables list.
QSet<QString> supported(const QFont &font);
// On or off as the text asks, else the font's default.
bool isOn(const std::map<QString, int> &features, const QString &tag);
// Sets `tag`, leaving it out when that's the default anyway.
void set(std::map<QString, int> &features, const QString &tag, bool on);
// Puts the features on a font, where the build can.
void apply(QFont &font, const std::map<QString, int> &features);
// CSS font-feature-settings, 'liga' 0, 'ss01' 1, and back.
QString css(const std::map<QString, int> &features);
std::map<QString, int> fromCss(const QString &settings);
}
