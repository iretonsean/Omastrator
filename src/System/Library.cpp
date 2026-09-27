#include "System/Library.h"
#include "Document/DocumentCodec.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

// `root` and everything under it, in document order.
std::vector<VectorObject> subtree(const std::vector<VectorObject> &objects, const QUuid &root)
{
    std::vector<VectorObject> result;
    std::vector<QUuid> inside;
    for (const VectorObject &object : objects) {
        if (object.id == root || (object.parentID && std::find(inside.begin(), inside.end(), *object.parentID) != inside.end())) {
            result.push_back(object);
            inside.push_back(object.id);
        }
    }
    return result;
}
}

namespace Library {
QStringList Contents::sets() const
{
    QStringList result;
    for (const VectorObject &object : objects) {
        if (object.component && !result.contains(object.component->set))
            result.append(object.component->set);
    }
    return result;
}

std::vector<VectorObject> Contents::set(const QString &name) const
{
    std::vector<VectorObject> result;
    for (const VectorObject &object : objects) {
        if (object.component && object.component->set == name) {
            const auto part = subtree(objects, object.id);
            result.insert(result.end(), part.begin(), part.end());
        }
    }
    return result;
}

QString directory()
{
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty())
        data = QDir::homePath() + QStringLiteral("/.local/share");
    return data + QStringLiteral("/omastrator/libraries");
}

QString pathFor(const QString &name)
{
    QString file;
    for (const QChar c : name.trimmed()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            file += c.toLower();
        else if (!file.endsWith(QLatin1Char('-')))
            file += QLatin1Char('-');
    }
    if (file.isEmpty())
        file = QStringLiteral("library");
    return QDir(directory()).filePath(file + QStringLiteral(".json"));
}

QStringList names()
{
    QStringList result;
    for (const QFileInfo &info : QDir(directory()).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QJsonObject json = QJsonDocument::fromJson(contents(info.absoluteFilePath()).value_or(QByteArray())).object();
        if (json["format"].toString() == QLatin1String("omastrator-library"))
            result.append(json["name"].toString(info.completeBaseName()));
    }
    return result;
}

Contents load(const QString &name)
{
    Contents library{name, pathFor(name), {}, {}, {}};
    const QJsonObject json = QJsonDocument::fromJson(contents(library.path).value_or(QByteArray())).object();
    if (json["format"].toString() != QLatin1String("omastrator-library"))
        return library;
    library.tokens = DesignTokens::decode(json["tokens"].toArray());
    for (const QJsonValue &mode : json["modes"].toArray())
        library.modes.append(mode.toString());
    try {
        library.objects = DocumentCodec::decodeObjects(json["components"].toArray());
    } catch (const CodecError &) {
        library.objects.clear();
    }
    return library;
}

QByteArray serialize(const Contents &library)
{
    QJsonObject json{{"format", "omastrator-library"}, {"version", 1}, {"name", library.name}, {"tokens", DesignTokens::encode(library.tokens)},
                     {"components", DocumentCodec::encode(library.objects)}};
    if (!library.modes.isEmpty())
        json["modes"] = QJsonArray::fromStringList(library.modes);
    return QJsonDocument(json).toJson(QJsonDocument::Indented);
}

SyncPlan pushPlan(const QString &name, const VectorDocument &document)
{
    SyncPlan plan;
    plan.title = QStringLiteral("Save to Library");
    Contents library = load(name);
    const std::optional<QByteArray> before = contents(library.path);
    DesignTokens::merge(library.tokens, document.tokens);
    for (const QString &mode : document.tokenModes) {
        if (!library.modes.contains(mode))
            library.modes.append(mode);
    }
    // Whole sets are replaced, so a variant removed here goes there too.
    QStringList sets;
    for (const QUuid &id : Components::masters(document)) {
        if (!sets.contains(document.find(id)->component->set))
            sets.append(document.find(id)->component->set);
    }
    std::vector<VectorObject> kept;
    for (const QString &set : library.sets()) {
        if (!sets.contains(set)) {
            const auto part = library.set(set);
            kept.insert(kept.end(), part.begin(), part.end());
        }
    }
    for (const QUuid &id : Components::masters(document)) {
        std::vector<VectorObject> part = subtree(document.objects, id);
        part.front().parentID.reset();
        kept.insert(kept.end(), part.begin(), part.end());
    }
    library.objects = kept;
    if (library.tokens.empty() && library.objects.empty()) {
        plan.problem = QStringLiteral("This document has no tokens or components to save.");
        return plan;
    }
    plan.writes.push_back({library.path, before, serialize(library), {}});
    plan.destination = QStringLiteral("Your global library “%1” at %2, on this machine only. Nothing is committed or published.").arg(name, library.path);
    return plan;
}

SyncPlan pullPlan(const QString &name, const QString &documentName, std::function<QString(const Contents &)> apply)
{
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::pull;
    plan.title = QStringLiteral("Use Library Tokens");
    const Contents library = load(name);
    if (library.tokens.empty()) {
        plan.problem = QStringLiteral("The library “%1” has no tokens yet.").arg(name);
        return plan;
    }
    plan.reads = {library.path};
    plan.destination = QStringLiteral("This document, %1. No files are written and nothing is committed.").arg(documentName);
    plan.inApp = QStringLiteral("Merges %1 tokens by name into %2 as one undo step.").arg(library.tokens.size()).arg(documentName);
    plan.apply = [apply, library] { return apply(library); };
    return plan;
}
}
