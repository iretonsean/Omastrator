#pragma once
#include "Agent/AgentProtocol.h"
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

// Reads and checks one request's params (a bad one throws invalidParams naming the key), and writes results.
namespace AgentParams {
[[noreturn]] inline void fail(const QString &message)
{
    throw AgentProtocol::Error(AgentProtocol::invalidParams, message);
}

inline std::optional<QString> string(const QJsonObject &params, const QString &key, bool required = false)
{
    const QJsonValue value = params[key];
    if (value.isUndefined() || value.isNull()) {
        if (required)
            fail(QStringLiteral("“%1” is required.").arg(key));
        return std::nullopt;
    }
    if (!value.isString())
        fail(QStringLiteral("“%1” must be a string.").arg(key));
    if (required && value.toString().trimmed().isEmpty())
        fail(QStringLiteral("“%1” must not be empty.").arg(key));
    return value.toString();
}

inline QString requiredString(const QJsonObject &params, const QString &key)
{
    return *string(params, key, true);
}

inline std::optional<double> number(const QJsonObject &params, const QString &key)
{
    const QJsonValue value = params[key];
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        fail(QStringLiteral("“%1” must be a number.").arg(key));
    return value.toDouble();
}

inline bool boolean(const QJsonObject &params, const QString &key, bool fallback)
{
    const QJsonValue value = params[key];
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (!value.isBool())
        fail(QStringLiteral("“%1” must be true or false.").arg(key));
    return value.toBool();
}

// A fixed-length array of numbers.
inline std::optional<std::vector<double>> numbers(const QJsonObject &params, const QString &key, int count)
{
    const QJsonValue value = params[key];
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != count)
        fail(QStringLiteral("“%1” must be an array of %2 numbers.").arg(key, QString::number(count)));
    std::vector<double> result;
    for (const QJsonValue &each : array) {
        if (!each.isDouble() || !std::isfinite(each.toDouble()))
            fail(QStringLiteral("“%1” must be an array of %2 numbers.").arg(key, QString::number(count)));
        result.push_back(each.toDouble());
    }
    return result;
}

inline std::optional<QPointF> point(const QJsonObject &params, const QString &key)
{
    const auto values = numbers(params, key, 2);
    return values ? std::optional(QPointF((*values)[0], (*values)[1])) : std::nullopt;
}

inline std::optional<QUuid> uuid(const QJsonValue &value, const QString &key)
{
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    const QUuid id = QUuid::fromString(value.toString());
    if (!value.isString() || id.isNull())
        fail(QStringLiteral("“%1” must be an object id (a UUID string).").arg(key));
    return id;
}

// An array of ids, or none when the key is absent.
inline std::optional<std::vector<QUuid>> ids(const QJsonObject &params, const QString &key = QStringLiteral("ids"))
{
    const QJsonValue value = params[key];
    if (value.isUndefined() || value.isNull())
        return std::nullopt;
    if (!value.isArray())
        fail(QStringLiteral("“%1” must be an array of object ids.").arg(key));
    std::vector<QUuid> result;
    for (const QJsonValue &each : value.toArray()) {
        const QUuid id = *uuid(each.isNull() ? QJsonValue(QString()) : each, key);
        if (std::find(result.begin(), result.end(), id) == result.end())
            result.push_back(id);
    }
    return result;
}

// One of `names`, as its index.
inline std::optional<int> choice(const QJsonObject &params, const QString &key, const QStringList &names, bool required = false)
{
    const auto value = string(params, key, required);
    if (!value)
        return std::nullopt;
    const int index = int(names.indexOf(*value));
    if (index < 0)
        fail(QStringLiteral("“%1” must be one of: %2.").arg(key, names.join(QStringLiteral(", "))));
    return index;
}

// A page named by its id or its name (`key`), or none when the key is absent.
inline std::optional<QUuid> pageParam(const QJsonObject &params, const VectorDocument &document, const QString &key = QStringLiteral("page"))
{
    const auto given = string(params, key);
    if (!given)
        return std::nullopt;
    const std::vector<Page> pages = document.allPages();
    const QUuid id = QUuid::fromString(*given);
    if (!id.isNull() && document.pageIndex(id) >= 0)
        return id;
    for (const Page &page : pages) {
        if (page.name == *given)
            return page.id;
    }
    for (const Page &page : pages) {
        if (page.name.compare(*given, Qt::CaseInsensitive) == 0)
            return page.id;
    }
    QStringList names;
    for (const Page &page : pages)
        names << QStringLiteral("“%1”").arg(page.name);
    fail(QStringLiteral("There is no page “%1”. The pages are %2. document_get lists them with their ids.").arg(*given, names.join(QStringLiteral(", "))));
}

// Results: rectangles as [x, y, width, height], ids without braces.
inline QJsonArray rect(const QRectF &r)
{
    return {r.x(), r.y(), r.width(), r.height()};
}

inline QString idString(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

inline QJsonArray idArray(const std::vector<QUuid> &ids)
{
    QJsonArray array;
    for (const QUuid &id : ids)
        array.append(idString(id));
    return array;
}
}
