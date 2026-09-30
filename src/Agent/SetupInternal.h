#pragma once
#include "Agent/Setup.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <optional>

// Shared by Setup.cpp and Setup+Cli.cpp only; Setup.h is the API.
namespace SetupInternal {
extern const QString designFilter;
extern const QString barFilter;
extern const QString pagesFilter;

std::optional<QByteArray> readFile(const QString &path);
QByteArray shellJsonBase(const Setup::Environment &environment, bool *exists);

// What setup.json says setup added.
struct Record {
    QStringList files;
    // Folders setup made, removed again once empty.
    QStringList directories;
    // omastrator.design is in shell.json (setup.json calls it "island" in records written before the island was removed).
    bool design = false;
    bool bar = false;
    // omastrator.pages (the page dots) is in the bar layout.
    bool pages = false;
    bool shellJsonCreated = false;
    bool menu = false;
    bool menuComma = false;
    bool menuCreated = false;
    QString sourcePath;
    QByteArray sourceText;
    // chromium-flags.conf: the extension folder setup added to it, and whether setup made the file.
    QString flagsPath;
    QString flagsExtension;
    bool flagsCreated = false;
    // Keys setup left unbound because the user already uses them, so --remove and --restore know what was never taken.
    QStringList skippedKeys;

    static Record read(const QString &path)
    {
        Record record;
        const QJsonObject json = QJsonDocument::fromJson(readFile(path).value_or(QByteArray())).object();
        for (const QJsonValue &file : json["files"].toArray())
            record.files << file.toString();
        for (const QJsonValue &directory : json["directories"].toArray())
            record.directories << directory.toString();
        record.design = json["shellJson"]["design"].toBool() || json["shellJson"]["island"].toBool();
        record.bar = json["shellJson"]["bar"].toBool();
        record.pages = json["shellJson"]["pages"].toBool();
        record.shellJsonCreated = json["shellJson"]["created"].toBool();
        record.menu = json["menu"]["block"].toBool();
        record.menuComma = json["menu"]["comma"].toBool();
        record.menuCreated = json["menu"]["created"].toBool();
        record.sourcePath = json["hyprSource"]["path"].toString();
        record.sourceText = json["hyprSource"]["text"].toString().toUtf8();
        record.flagsPath = json["chromiumFlags"]["path"].toString();
        record.flagsExtension = json["chromiumFlags"]["extension"].toString();
        record.flagsCreated = json["chromiumFlags"]["created"].toBool();
        for (const QJsonValue &key : json["skippedKeys"].toArray())
            record.skippedKeys << key.toString();
        return record;
    }
    bool isEmpty() const
    {
        return files.isEmpty() && directories.isEmpty() && !design && !bar && !pages && !menu && sourcePath.isEmpty() && flagsPath.isEmpty();
    }
    QByteArray toJson() const
    {
        return QJsonDocument(QJsonObject{
                                 {"version", 1},
                                 {"files", QJsonArray::fromStringList(files)},
                                 {"directories", QJsonArray::fromStringList(directories)},
                                 {"shellJson", QJsonObject{{"design", design}, {"bar", bar}, {"pages", pages}, {"created", shellJsonCreated}}},
                                 {"menu", QJsonObject{{"block", menu}, {"comma", menuComma}, {"created", menuCreated}}},
                                 {"hyprSource", QJsonObject{{"path", sourcePath}, {"text", QString::fromUtf8(sourceText)}}},
                                 {"chromiumFlags", QJsonObject{{"path", flagsPath}, {"extension", flagsExtension}, {"created", flagsCreated}}},
                                 {"skippedKeys", QJsonArray::fromStringList(skippedKeys)},
                             })
            .toJson(QJsonDocument::Indented);
    }
};
}
