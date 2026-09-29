#include "UI/PresetStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace {
constexpr std::pair<LengthUnit, const char *> unitNames[] = {
    {LengthUnit::pt, "pt"}, {LengthUnit::px, "px"}, {LengthUnit::in, "in"}, {LengthUnit::mm, "mm"}};

bool inRange(double value)
{
    return std::isfinite(value) && value >= 1 && value <= PresetStore::maximumPoints;
}

QJsonObject readFile(const QString &file, bool *valid = nullptr)
{
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly)) {
        if (valid)
            *valid = true;
        return {};
    }
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(handle.readAll(), &error);
    if (valid)
        *valid = error.error == QJsonParseError::NoError && parsed.isObject();
    return parsed.object();
}
}

namespace PresetStore {
QString path()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : given;
    return QDir(config).filePath(QStringLiteral("omastrator/presets.json"));
}

Section read(const QString &section)
{
    Section result;
    const QJsonObject object = readFile(path())[section].toObject();
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
    if (!valid) {
        const QString backup = target + QStringLiteral(".bak");
        QFile::remove(backup);
        QFile::rename(target, backup);
        root = {};
    }
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
