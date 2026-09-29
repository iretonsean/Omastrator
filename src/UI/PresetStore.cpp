#include "UI/PresetStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace {
constexpr std::pair<LengthUnit, const char *> unitNames[] = {
    {LengthUnit::pt, "pt"}, {LengthUnit::px, "px"}, {LengthUnit::in, "in"}, {LengthUnit::mm, "mm"}};

bool inRange(double value)
{
    return std::isfinite(value) && value >= 1 && value <= PresetStore::maximumPoints;
}

// A file that isn't there, or is empty, is valid and empty; one that is there but can't be
// opened or parsed as an object is not, and the caller must leave it alone.
QJsonObject readFile(const QString &file, bool *valid = nullptr)
{
    if (valid)
        *valid = true;
    if (!QFileInfo::exists(file))
        return {};
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly)) {
        if (valid)
            *valid = false;
        return {};
    }
    const QByteArray bytes = handle.readAll();
    if (bytes.trimmed().isEmpty())
        return {};
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &error);
    if (valid)
        *valid = error.error == QJsonParseError::NoError && parsed.isObject();
    return parsed.object();
}
}

namespace PresetStore {
QString path()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString home = QDir::home().filePath(QStringLiteral(".config"));
    const QString config = given.isEmpty() ? home : given;
    // A test that reaches the user's own config would read or overwrite their presets, so in
    // test mode the folder must be a temporary one.
    if (QStandardPaths::isTestModeEnabled() && !QDir::cleanPath(config).startsWith(QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/')))
        qFatal("A test is using a config folder that isn't temporary for presets.json (the real config folder); point XDG_CONFIG_HOME at a temporary folder.");
    return QDir(config).filePath(QStringLiteral("omastrator/presets.json"));
}
}

namespace {
PresetStore::Section parseSection(const QJsonObject &object)
{
    PresetStore::Section result;
    for (const QJsonValue &value : object["saved"].toArray()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry["name"].toString().trimmed();
        const double width = entry["width"].toDouble(), height = entry["height"].toDouble();
        bool taken = false;
        for (const PresetStore::Entry &other : result.saved)
            taken = taken || other.name.compare(name, Qt::CaseInsensitive) == 0;
        if (name.isEmpty() || taken || !inRange(width) || !inRange(height))
            continue;
        LengthUnit unit = LengthUnit::px;
        for (const auto &[candidate, label] : unitNames)
            if (entry["unit"].toString() == QLatin1String(label))
                unit = candidate;
        result.saved.push_back({name, QSizeF(width, height), unit});
    }
    for (const QJsonValue &value : object["hidden"].toArray())
        if (value.isString() && !result.hidden.contains(value.toString()))
            result.hidden << value.toString();
    return result;
}

QJsonObject sectionJson(const PresetStore::Section &value)
{
    QJsonArray saved;
    for (const PresetStore::Entry &entry : value.saved) {
        QString unit = QStringLiteral("px");
        for (const auto &[candidate, label] : unitNames)
            if (candidate == entry.unit)
                unit = QLatin1String(label);
        saved.append(QJsonObject{{"name", entry.name}, {"width", entry.points.width()}, {"height", entry.points.height()}, {"unit", unit}});
    }
    return QJsonObject{{"saved", saved}, {"hidden", QJsonArray::fromStringList(value.hidden)}};
}

QString saveRoot(const QString &target, const QJsonObject &root)
{
    QSaveFile file(target);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(target, file.errorString());
    return {};
}

// Reads the file, lets `change` edit it, writes it back. An unreadable file is never replaced.
template <typename Change>
QString editRoot(Change change)
{
    const QString target = PresetStore::path();
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return QStringLiteral("Could not create %1.").arg(QFileInfo(target).absolutePath());
    bool valid = true;
    QJsonObject root = readFile(target, &valid);
    if (!valid)
        return QStringLiteral("Could not save: %1 exists but can't be read as presets. Fix or move it, then try again.").arg(target);
    change(root);
    root["version"] = 1;
    return saveRoot(target, root);
}
}

namespace PresetStore {
Section read(const QString &section, bool *valid)
{
    return parseSection(readFile(path(), valid)[section].toObject());
}

QJsonObject exportSections(bool *valid)
{
    const QJsonObject root = readFile(path(), valid);
    QJsonObject result;
    for (const char *name : {documents, frames})
        result[QLatin1String(name)] = sectionJson(parseSection(root[QLatin1String(name)].toObject()));
    return result;
}

QJsonObject normalised(const QJsonObject &sections)
{
    QJsonObject result;
    for (const char *name : {documents, frames})
        if (sections.contains(QLatin1String(name)))
            result[QLatin1String(name)] = sectionJson(parseSection(sections[QLatin1String(name)].toObject()));
    return result;
}

QString replaceSections(const QJsonObject &sections)
{
    const QJsonObject clean = normalised(sections);
    return editRoot([&](QJsonObject &root) {
        for (auto section = clean.begin(); section != clean.end(); ++section)
            root[section.key()] = section.value();
    });
}

QString write(const QString &section, const Section &value)
{
    return editRoot([&](QJsonObject &root) { root[section] = sectionJson(value); });
}
}
