#include "Document/Swatches.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <algorithm>

Swatches::Swatches(QString settingsKey, QObject *parent) : QObject(parent), m_key(std::move(settingsKey))
{
    if (!m_key.isEmpty())
        m_groups = fromJson(QJsonDocument::fromJson(QSettings().value(m_key).toByteArray()).array());
}

int Swatches::add(const QString &group, const std::vector<Swatch> &swatches, bool replace)
{
    auto found = std::find_if(m_groups.begin(), m_groups.end(), [&](const SwatchGroup &each) { return each.name == group; });
    if (found == m_groups.end()) {
        m_groups.push_back({group, {}});
        found = m_groups.end() - 1;
    }
    if (replace)
        found->swatches.clear();
    int added = 0;
    for (const Swatch &swatch : swatches) {
        if (!swatch.color.isValid())
            continue;
        const QRgb rgb = swatch.color.rgba();
        if (std::any_of(found->swatches.begin(), found->swatches.end(), [&](const Swatch &each) { return each.color.rgba() == rgb; }))
            continue;
        Swatch kept = swatch;
        if (kept.name.isEmpty())
            kept.name = swatch.color.name();
        if (kept.id.isEmpty())
            kept.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        found->swatches.push_back(kept);
        ++added;
    }
    save();
    emit changed();
    return added;
}

void Swatches::remove(const QString &group, int index)
{
    for (SwatchGroup &each : m_groups) {
        if (each.name == group && index >= 0 && index < int(each.swatches.size())) {
            each.swatches.erase(each.swatches.begin() + index);
            save();
            emit changed();
            return;
        }
    }
}

void Swatches::removeGroup(const QString &group)
{
    std::erase_if(m_groups, [&](const SwatchGroup &each) { return each.name == group; });
    save();
    emit changed();
}

Swatch *Swatches::at(const QString &group, int index)
{
    for (SwatchGroup &each : m_groups) {
        if (each.name == group && index >= 0 && index < int(each.swatches.size()))
            return &each.swatches[size_t(index)];
    }
    return nullptr;
}

void Swatches::setColor(const QString &group, int index, const QColor &color)
{
    Swatch *swatch = at(group, index);
    if (!swatch || !color.isValid() || swatch->color == color)
        return;
    // A name that was only its colour follows the colour.
    if (swatch->name == swatch->color.name())
        swatch->name = color.name();
    swatch->color = color;
    const Swatch edited = *swatch;
    save();
    emit changed();
    if (edited.global)
        emit globalSwatchRecolored(edited.id, color);
}

void Swatches::setGlobal(const QString &group, int index, bool global)
{
    Swatch *swatch = at(group, index);
    if (!swatch || swatch->global == global)
        return;
    swatch->global = global;
    save();
    emit changed();
}

QJsonArray Swatches::toJson() const
{
    QJsonArray groups;
    for (const SwatchGroup &group : m_groups) {
        QJsonArray swatches;
        for (const Swatch &swatch : group.swatches) {
            QJsonObject json{{"name", swatch.name}, {"color", swatch.color.name(swatch.color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb)}};
            if (!swatch.id.isEmpty())
                json["id"] = swatch.id;
            if (swatch.global)
                json["global"] = true;
            swatches.append(json);
        }
        groups.append(QJsonObject{{"name", group.name}, {"swatches", swatches}});
    }
    return groups;
}

std::vector<SwatchGroup> Swatches::fromJson(const QJsonArray &json)
{
    std::vector<SwatchGroup> groups;
    for (const QJsonValue &value : json) {
        SwatchGroup group{value["name"].toString(), {}};
        for (const QJsonValue &swatch : value["swatches"].toArray()) {
            const QColor color = QColor::fromString(swatch["color"].toString());
            if (color.isValid()) {
                const QString id = swatch["id"].toString();
                group.swatches.push_back({swatch["name"].toString(), color, swatch["global"].toBool(),
                                          id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id});
            }
        }
        if (!group.name.isEmpty())
            groups.push_back(std::move(group));
    }
    return groups;
}

QString Swatches::nameFromKey(const QString &key)
{
    QStringList words = key.split(QRegularExpression(QStringLiteral("[_\\-\\s]+")), Qt::SkipEmptyParts);
    for (QString &word : words)
        word[0] = word[0].toUpper();
    return words.join(QLatin1Char(' '));
}

void Swatches::save() const
{
    if (!m_key.isEmpty())
        QSettings().setValue(m_key, QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}
