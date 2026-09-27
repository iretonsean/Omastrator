#include "UI/Share.h"
#include "IO/DocumentExporter.h"
#include "IO/SvgExporter.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <set>

namespace {
QJsonObject readStore()
{
    QFile file(Share::storePath());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QString writeStore(const QJsonObject &store)
{
    const QString path = Share::storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(store).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Couldn't save the Shared list: %1").arg(file.errorString());
    // Links are unlisted, not public: only this user reads the list.
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return {};
}
}

namespace Share {
QString suffix(Format format)
{
    switch (format) {
    case Format::png: return QStringLiteral("png");
    case Format::pdf: return QStringLiteral("pdf");
    case Format::svg: return QStringLiteral("svg");
    }
    return {};
}

QString label(Format format)
{
    switch (format) {
    case Format::png: return QStringLiteral("PNG at 2×");
    case Format::pdf: return QStringLiteral("PDF");
    case Format::svg: return QStringLiteral("SVG");
    }
    return {};
}

std::optional<Format> parseFormat(const QString &text)
{
    for (Format format : {Format::png, Format::pdf, Format::svg}) {
        if (suffix(format) == text)
            return format;
    }
    return std::nullopt;
}

QString cloudDestination(const QString &remote)
{
    return QStringLiteral("cloud:") + remote;
}

QString remoteOf(const QString &destination)
{
    return destination.startsWith(QLatin1String("cloud:")) ? destination.mid(6) : QString();
}

QJsonObject Record::toJson() const
{
    QJsonObject json{{"id", id}, {"link", link}, {"time", time.toString(Qt::ISODateWithMs)}, {"kind", kind}, {"where", where}, {"format", format},
                     {"scope", scope}};
    if (!objects.isEmpty())
        json["objects"] = QJsonArray::fromStringList(objects);
    for (const auto &[key, value] : {std::pair{"remotePath", remotePath}, {"gist", gist}, {"repository", repository}, {"tag", tag}}) {
        if (!value.isEmpty())
            json[QLatin1String(key)] = value;
    }
    return json;
}

Record Record::fromJson(const QJsonObject &json)
{
    Record record;
    record.id = json["id"].toString();
    record.link = json["link"].toString();
    record.time = QDateTime::fromString(json["time"].toString(), Qt::ISODateWithMs);
    record.kind = json["kind"].toString();
    record.where = json["where"].toString();
    record.format = json["format"].toString();
    record.scope = json["scope"].toString();
    for (const QJsonValue &object : json["objects"].toArray())
        record.objects << object.toString();
    record.remotePath = json["remotePath"].toString();
    record.gist = json["gist"].toString();
    record.repository = json["repository"].toString();
    record.tag = json["tag"].toString();
    return record;
}

QString storePath()
{
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    return QDir(config.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : config).filePath(QStringLiteral("omastrator/shares.json"));
}

DocumentShares load(const QString &key)
{
    const QJsonObject entry = readStore()["documents"].toObject()[key].toObject();
    DocumentShares shares;
    shares.format = parseFormat(entry["format"].toString());
    shares.destination = entry["destination"].toString();
    for (const QJsonValue &value : entry["shared"].toArray())
        shares.shared.push_back(Record::fromJson(value.toObject()));
    return shares;
}

QString save(const QString &key, const DocumentShares &shares)
{
    QJsonObject store = readStore();
    QJsonObject documents = store["documents"].toObject();
    QJsonObject entry;
    if (shares.format)
        entry["format"] = suffix(*shares.format);
    if (!shares.destination.isEmpty())
        entry["destination"] = shares.destination;
    QJsonArray list;
    for (const Record &record : shares.shared)
        list.append(record.toJson());
    if (!list.isEmpty())
        entry["shared"] = list;
    if (entry.isEmpty())
        documents.remove(key);
    else
        documents[key] = entry;
    store["documents"] = documents;
    return writeStore(store);
}

QString rename(const QString &from, const QString &to)
{
    QJsonObject store = readStore();
    QJsonObject documents = store["documents"].toObject();
    if (from == to || !documents.contains(from) || documents.contains(to))
        return {};
    documents[to] = documents.take(from);
    store["documents"] = documents;
    return writeStore(store);
}

bool githubConfirmed()
{
    return readStore()["githubConfirmed"].toBool();
}

QString setGithubConfirmed(bool confirmed)
{
    QJsonObject store = readStore();
    store["githubConfirmed"] = confirmed;
    return writeStore(store);
}

VectorDocument selectionDocument(const VectorDocument &document, const std::vector<QUuid> &ids)
{
    return document.croppedTo(ids);
}

void write(const VectorDocument &document, Format format, const QString &path)
{
    switch (format) {
    case Format::png: DocumentExporter::writePng(document, path, pngScale, document.background.alpha() == 0); break;
    case Format::pdf: DocumentExporter::writePdf(document, path); break;
    case Format::svg: SvgExporter::write(document, path, {false, document.background.alpha() > 0}); break;
    }
}

QString safeName(const QString &name)
{
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    QString safe = name.trimmed();
    safe.replace(unsafe, QStringLiteral("-"));
    while (safe.startsWith(QLatin1Char('-')) || safe.startsWith(QLatin1Char('.')))
        safe.remove(0, 1);
    while (safe.endsWith(QLatin1Char('-')))
        safe.chop(1);
    return safe.isEmpty() ? QStringLiteral("design") : safe.left(60);
}

QString stamp(const QDateTime &time)
{
    return time.toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
}
}
