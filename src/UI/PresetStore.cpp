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

Section read(const QString &section, bool *valid)
{
    Section result;
    const QJsonObject object = readFile(path(), valid)[section].toObject();
    for (const QJsonValue &value : object["saved"].toArray()) {
        const QJsonObject entry = value.toObject();
        const QString name = entry["name"].toString().trimmed();
        const double width = entry["width"].toDouble(), height = entry["height"].toDouble();
        bool taken = false;
        for (const Entry &other : result.saved)
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

QString write(const QString &section, const Section &value)
{
    const QString target = path();
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return QStringLiteral("Could not create %1.").arg(QFileInfo(target).absolutePath());
    bool valid = true;
    QJsonObject root = readFile(target, &valid);
    if (!valid)
        return QStringLiteral("Could not save: %1 exists but can't be read as presets. Fix or move it, then try again.").arg(target);
    QJsonArray saved;
    for (const Entry &entry : value.saved) {
        QString unit = QStringLiteral("px");
        for (const auto &[candidate, label] : unitNames)
            if (candidate == entry.unit)
                unit = QLatin1String(label);
        saved.append(QJsonObject{{"name", entry.name}, {"width", entry.points.width()}, {"height", entry.points.height()}, {"unit", unit}});
    }
    root["version"] = 1;
    root[section] = QJsonObject{{"saved", saved}, {"hidden", QJsonArray::fromStringList(value.hidden)}};
    QSaveFile file(target);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(target, file.errorString());
    return {};
}
}
