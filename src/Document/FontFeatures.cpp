#include "Document/FontFeatures.h"
#include <QHash>
#include <QRawFont>
#include <QRegularExpression>

namespace {
quint16 word(const QByteArray &table, qsizetype at)
{
    if (at < 0 || at + 2 > table.size())
        return 0;
    return quint16((quint8(table[at]) << 8) | quint8(table[at + 1]));
}

// A GSUB or GPOS table's FeatureList: a count, then four-letter tags with offsets.
void addTags(const QByteArray &table, QSet<QString> &tags)
{
    if (table.size() < 10)
        return;
    const qsizetype list = word(table, 6);
    const int count = word(table, list);
    for (int index = 0; index < count; ++index) {
        const qsizetype record = list + 2 + qsizetype(index) * 6;
        if (record + 4 > table.size())
            break;
        tags.insert(QString::fromLatin1(table.mid(record, 4)));
    }
}
}

namespace FontFeatures {
const std::vector<Feature> &offered()
{
    static const std::vector<Feature> features{
        {"liga", "Standard ligatures", true}, {"dlig", "Discretionary ligatures"}, {"calt", "Contextual alternates", true},
        {"smcp", "Small caps"},             {"frac", "Fractions"},               {"ordn", "Ordinals"},
        {"lnum", "Lining figures"},         {"onum", "Oldstyle figures"},        {"tnum", "Tabular figures"},
        {"pnum", "Proportional figures"},
    };
    return features;
}

QStringList stylisticSets()
{
    QStringList sets;
    for (int set = 1; set <= 20; ++set)
        sets << QStringLiteral("ss%1").arg(set, 2, 10, QLatin1Char('0'));
    return sets;
}

QSet<QString> supported(const QFont &font)
{
    // Asked for often while the panel follows a selection; fonts don't change under a running app.
    static QHash<QString, QSet<QString>> known;
    const QString key = font.family() + QLatin1Char('\n') + font.styleName() + QLatin1Char('\n') + QString::number(font.weight())
        + QLatin1Char('\n') + QString::number(font.italic());
    if (const auto found = known.constFind(key); found != known.cend())
        return *found;
    QFont probe = font;
    probe.setPixelSize(16);
    const QRawFont raw = QRawFont::fromFont(probe);
    QSet<QString> tags;
    if (raw.isValid()) {
        addTags(raw.fontTable("GSUB"), tags);
        addTags(raw.fontTable("GPOS"), tags);
    }
    known.insert(key, tags);
    return tags;
}

bool isOn(const std::map<QString, int> &features, const QString &tag)
{
    if (const auto found = features.find(tag); found != features.end())
        return found->second != 0;
    for (const Feature &feature : offered()) {
        if (tag == QLatin1String(feature.tag))
            return feature.onByDefault;
    }
    return false;
}

void set(std::map<QString, int> &features, const QString &tag, bool on)
{
    features.erase(tag);
    if (isOn(features, tag) != on)
        features[tag] = on ? 1 : 0;
}

void apply([[maybe_unused]] QFont &font, [[maybe_unused]] const std::map<QString, int> &features)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    for (const auto &[tag, value] : features) {
        if (const std::optional<QFont::Tag> made = QFont::Tag::fromString(tag))
            font.setFeature(*made, quint32(value));
    }
#endif
}

QString css(const std::map<QString, int> &features)
{
    QStringList settings;
    for (const auto &[tag, value] : features)
        settings << QStringLiteral("'%1' %2").arg(tag).arg(value);
    return settings.join(QStringLiteral(", "));
}

std::map<QString, int> fromCss(const QString &settings)
{
    std::map<QString, int> features;
    static const QRegularExpression item(QStringLiteral(R"re(["']([A-Za-z0-9 ]{4})["']\s*(on|off|\d+)?)re"));
    for (auto match = item.globalMatch(settings); match.hasNext();) {
        const QRegularExpressionMatch next = match.next();
        const QString value = next.captured(2);
        features[next.captured(1)] = value.isEmpty() || value == QLatin1String("on") ? 1 : value == QLatin1String("off") ? 0 : value.toInt();
    }
    return features;
}
}
